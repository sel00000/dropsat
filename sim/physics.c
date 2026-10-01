/*
 * 물리 모델
 *   궤도      : 원궤도 + J2 영년섭동 (태양동기 조건으로 경사각 결정)
 *   태양      : 저정밀 천문력 (Astronomical Almanac), 원통형 지구 그림자
 *   지자기장  : 경사 쌍극자 (지자기 북극 80.8°N 72.8°W)
 *   자세      : 강체 오일러 방정식 + 쿼터니언, RK4
 *               외란 = 잔류자기 쌍극자, 중력경사 토크
 *   전력      : 3접합 GaAs 셀, MPPT, 2S 리튬이온 (개방전압 곡선 + 내부저항 + CC/CV 충전)
 *   열        : 4절점 집중정수 (구조체 / 내부 전자장비 / 배터리 / 탑재체 칩)
 */
#include <math.h>
#include <string.h>

#include "sim.h"

sim_t S;

/* ---------------- 난수 (xorshift64*, 시드 고정으로 재현 가능) ---------------- */

double rng_u(void)
{
    S.rng ^= S.rng >> 12;
    S.rng ^= S.rng << 25;
    S.rng ^= S.rng >> 27;
    return (double)((S.rng * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
}

double rng_n(void)
{
    double u1 = rng_u();
    const double u2 = rng_u();
    if (u1 < 1e-300) {
        u1 = 1e-300;
    }
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

/* ---------------- 시각·태양 ---------------- */

static double jd_utc(int y, int mo, int d, int h, int mi, double s)
{
    return 367.0 * y - floor(7.0 * (y + floor((mo + 9) / 12.0)) / 4.0) + floor(275.0 * mo / 9.0) + d +
           1721013.5 + (h + mi / 60.0 + s / 3600.0) / 24.0;
}

static double gmst_rad(double jd)
{
    double g = fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0), 360.0);
    if (g < 0.0) {
        g += 360.0;
    }
    return g * D2R;
}

static void sun_eci(double jd, v3 *u, double *au)
{
    const double n = jd - 2451545.0;
    const double L = (280.460 + 0.9856474 * n) * D2R;
    const double g = (357.528 + 0.9856003 * n) * D2R;
    const double lam = L + (1.915 * sin(g) + 0.020 * sin(2.0 * g)) * D2R;
    const double eps = (23.439 - 4.0e-7 * n) * D2R;
    *u = v3_(cos(lam), cos(eps) * sin(lam), sin(eps) * sin(lam));
    *au = 1.00014 - 0.01671 * cos(g) - 0.00014 * cos(2.0 * g);
}

v3 eci_to_ecef(v3 r, double th)
{
    const double c = cos(th), s = sin(th);
    return v3_(c * r.x + s * r.y, -s * r.x + c * r.y, r.z);
}

v3 ecef_to_eci(v3 r, double th)
{
    const double c = cos(th), s = sin(th);
    return v3_(c * r.x - s * r.y, s * r.x + c * r.y, r.z);
}

/* ---------------- 지자기장 ---------------- */

static v3 bfield_eci(v3 r_eci, double gmst)
{
    const double plat = 80.8 * D2R, plon = -72.8 * D2R;
    /* 쌍극자 모멘트는 지자기 "남"극 방향 */
    const v3 m = v3_(-cos(plat) * cos(plon), -cos(plat) * sin(plon), -sin(plat));
    const v3 re = eci_to_ecef(r_eci, gmst);
    const double rn = v3_norm(re);
    const v3 rh = v3_scale(re, 1.0 / rn);
    const double b0 = 30.0e-6 * pow(R_E / rn, 3.0);
    const v3 b = v3_scale(v3_sub(v3_scale(rh, 3.0 * v3_dot(m, rh)), m), b0);
    return ecef_to_eci(b, gmst);
}

/* ---------------- 궤도 ---------------- */

static void orbit_at(double t)
{
    const double u = S.u0 + S.u_dot * t;
    const double O = S.raan0 + S.raan_dot * t;
    const double cu = cos(u), su = sin(u), cO = cos(O), sO = sin(O), ci = cos(S.inc), si = sin(S.inc);
    S.r = v3_(S.a * (cu * cO - su * ci * sO), S.a * (cu * sO + su * ci * cO), S.a * su * si);
    const double vv = S.a * S.u_dot;
    S.v = v3_(vv * (-su * cO - cu * ci * sO), vv * (-su * sO + cu * ci * cO), vv * cu * si);
}

static void environment(void)
{
    const double jd = S.jd0 + S.t / 86400.0;
    orbit_at(S.t);
    S.gmst = gmst_rad(jd);
    sun_eci(jd, &S.sun, &S.sun_au);
    const double d = v3_dot(S.r, S.sun);
    S.eclipse = d < 0.0 && v3_norm(v3_sub(S.r, v3_scale(S.sun, d))) < R_E;
    S.b_eci = bfield_eci(S.r, S.gmst);
}

/* ---------------- 자세 동역학 ---------------- */

static void att_deriv(quat q, v3 w, quat *dq, v3 *dw)
{
    const v3 bb = q_rot_inv(q, S.b_eci);
    const v3 m = v3_add(S.m_applied, S.m_res);
    v3 tau = v3_cross(m, bb);                                    /* 자기 토크 */
    const double rn = v3_norm(S.r);
    const v3 rb = q_rot_inv(q, v3_scale(S.r, 1.0 / rn));
    const v3 Ir = v3_(S.I.x * rb.x, S.I.y * rb.y, S.I.z * rb.z);
    tau = v3_add(tau, v3_scale(v3_cross(rb, Ir), 3.0 * MU_E / (rn * rn * rn)));   /* 중력경사 */
    const v3 Iw = v3_(S.I.x * w.x, S.I.y * w.y, S.I.z * w.z);
    const v3 gy = v3_cross(w, Iw);
    *dw = v3_((tau.x - gy.x) / S.I.x, (tau.y - gy.y) / S.I.y, (tau.z - gy.z) / S.I.z);
    const quat wq = {0.0, w.x, w.y, w.z};
    const quat d = q_mul(q, wq);
    dq->w = 0.5 * d.w;
    dq->x = 0.5 * d.x;
    dq->y = 0.5 * d.y;
    dq->z = 0.5 * d.z;
}

static quat q_axpy(quat q, quat d, double h)
{
    quat r = {q.w + h * d.w, q.x + h * d.x, q.y + h * d.y, q.z + h * d.z};
    return r;
}

static void att_rk4(double h)
{
    quat k1q, k2q, k3q, k4q;
    v3 k1w, k2w, k3w, k4w;
    const quat q0 = S.q;
    const v3 w0 = S.w;
    att_deriv(q0, w0, &k1q, &k1w);
    att_deriv(q_axpy(q0, k1q, h / 2), v3_add(w0, v3_scale(k1w, h / 2)), &k2q, &k2w);
    att_deriv(q_axpy(q0, k2q, h / 2), v3_add(w0, v3_scale(k2w, h / 2)), &k3q, &k3w);
    att_deriv(q_axpy(q0, k3q, h), v3_add(w0, v3_scale(k3w, h)), &k4q, &k4w);
    quat q = q0;
    q = q_axpy(q, k1q, h / 6);
    q = q_axpy(q, k2q, h / 3);
    q = q_axpy(q, k3q, h / 3);
    q = q_axpy(q, k4q, h / 6);
    S.q = q_normalize(q);
    S.w = v3_add(w0, v3_scale(v3_add(v3_add(k1w, v3_scale(k2w, 2)), v3_add(v3_scale(k3w, 2), k4w)), h / 6));
}

/* ---------------- 외피 (2U: 기체 Z축이 긴 축) ---------------- */

typedef struct {
    v3 n;
    double cells;       /* 3접합 셀 개수 (30.18 cm²) */
    double area;        /* 면적 [m²] */
} face_t;

#define CELL_A   30.18e-4
#define ALPHA_S  0.80   /* 태양광 흡수율 */
#define EPS_IR   0.80   /* 적외선 방사율 */
#define A_TOTAL  0.1108

static const face_t FACES[6] = {
    {{1, 0, 0}, 4, 0.0227}, {{-1, 0, 0}, 4, 0.0227},
    {{0, 1, 0}, 4, 0.0227}, {{0, -1, 0}, 4, 0.0227},
    {{0, 0, 1}, 2, 0.0100}, {{0, 0, -1}, 0, 0.0100},   /* -Z: 안테나·킬스위치 면, 셀 없음 */
};

static double solar_power(v3 sun_b)
{
    if (S.eclipse) {
        S.panel_v = 0.2;
        return 0.0;
    }
    const double flux = S0 / (S.sun_au * S.sun_au);
    const double eta = 0.295 * (1.0 - 0.0025 * (S.Ts - 28.0));
    double p = 0.0, best = 0.0;
    for (int i = 0; i < 6; i++) {
        const double c = v3_dot(FACES[i].n, sun_b);
        if (c > 0.0 && FACES[i].cells > 0.0) {
            p += flux * FACES[i].cells * CELL_A * eta * c;
            if (c > best) {
                best = c;
            }
        }
    }
    S.panel_v = best > 0.02 ? 4.6 + 0.6 * best : 0.2;
    return p * 0.92 * 0.93;      /* 배선·불일치 손실, MPPT 효율 */
}

/* ---------------- 부하 ---------------- */

typedef struct {
    double total;               /* 버스에서 끌어가는 전력 */
    double s, e, b, p;          /* 각 열절점에서 열로 바뀌는 몫 */
} loads_t;

bool hv_effective(void)
{
    return S.hv_cmd && S.rail[RAIL_PAYLOAD];
}

static loads_t compute_loads(void)
{
    loads_t L = {0, 0, 0, 0, 0};
    L.e += (S.cpu_up ? 0.30 : 0.05) + 0.15 + 0.20;              /* OBC, EPS 자체, UHF 수신기 */
    if (S.rail[RAIL_ADCS]) {
        L.e += 0.08;                                              /* 자력계·자이로 */
        const v3 m = S.m_applied;
        L.s += 0.25 * (m.x * m.x + m.y * m.y + m.z * m.z) / (0.2 * 0.2);   /* 토커 코일 */
    }
    double txf = S.tx_air / SIM_DT;
    if (txf > 1.0) {
        txf = 1.0;
    }
    L.e += 2.0 * txf;                                             /* 송신 3 W 중 열 2 W */
    const double p_rf = 1.0 * txf;                                /* 전파로 나가는 1 W */
    if (S.burnwire) {
        L.s += 2.5;
    }
    if (S.rail[RAIL_BATT_HEATER]) {
        L.b += 1.0;
    }
    if (S.rail[RAIL_PAYLOAD]) {
        L.p += 0.15;
        if (S.pl_heater) {
            L.p += 1.5;
        }
        if (hv_effective()) {
            L.p += 0.6;
        }
    }
    L.total = L.s + L.e + L.b + L.p + p_rf;
    return L;
}

/* ---------------- 배터리 (실제 셀: FSW의 데이터시트 표와 약간 다르다) ---------------- */

static const double OCV_TRUE[11] = {3.02, 3.47, 3.56, 3.63, 3.69, 3.745, 3.80, 3.88, 3.96, 4.06, 4.19};

static double ocv_cell(double soc)
{
    if (soc <= 0.0) {
        return OCV_TRUE[0];
    }
    if (soc >= 1.0) {
        return OCV_TRUE[10];
    }
    const double x = soc * 10.0;
    const int i = (int)x;
    return OCV_TRUE[i] + (x - i) * (OCV_TRUE[i + 1] - OCV_TRUE[i]);
}

static double pack_current(double ocv, double R, double p)
{
    const double disc = ocv * ocv + 4.0 * R * p;
    if (disc <= 0.0) {
        return -ocv / (2.0 * R);
    }
    return (-ocv + sqrt(disc)) / (2.0 * R);
}

static void power_step(double dt, const loads_t *L, v3 sun_b)
{
    S.p_avail = solar_power(sun_b);
    S.p_load = L->total;
    const double ocv = 2.0 * ocv_cell(S.soc);
    S.r_pack = 0.10 * (1.0 + 0.015 * fmax(0.0, 20.0 - S.Tb));
    double i;
    if (S.p_avail >= L->total) {
        double i_max = 0.0;
        if (S.charge_en) {
            i_max = fmin(1.5, fmax(0.0, (8.30 - ocv) / S.r_pack));   /* CC 1.5 A / CV 8.30 V */
        }
        i = fmin(pack_current(ocv, S.r_pack, S.p_avail - L->total), i_max);
        S.p_used = L->total + i * (ocv + i * S.r_pack);
    } else {
        i = pack_current(ocv, S.r_pack, S.p_avail - L->total);
        S.p_used = S.p_avail;
    }
    S.i_batt = i;
    S.v_batt = ocv + i * S.r_pack;
    S.soc += i * dt / (3600.0 * S.cap_ah) * (i > 0.0 ? 0.995 : 1.0);
    S.soc = fmin(1.0, fmax(0.0, S.soc));
    S.e_solar_wh += S.p_used * dt / 3600.0;
    S.e_load_wh += L->total * dt / 3600.0;
}

/* ---------------- 열 ---------------- */

#define C_S   1600.0   /* 열용량 [J/K] */
#define C_E   500.0
#define C_B   110.0
#define C_P   60.0
#define G_SE  0.25     /* 열전도 [W/K] */
#define G_EB  0.05
#define G_EP  0.04

static void thermal_step(double dt, const loads_t *L, v3 sun_b)
{
    const double flux = S0 / (S.sun_au * S.sun_au);
    const v3 rh = v3_unit(S.r);
    const double cos_psi = v3_dot(rh, S.sun);       /* 바로 아래 지표의 태양 고도 */
    double qs = 0.0;
    for (int i = 0; i < 6; i++) {
        const v3 n_eci = q_rot(S.q, FACES[i].n);
        const double cg = -v3_dot(n_eci, rh);        /* 면이 지구를 향하는 정도 */
        const double F = 0.835 * pow(0.5 * (1.0 + cg), 1.5);
        qs += EPS_IR * 237.0 * FACES[i].area * F;                         /* 지구 적외선 */
        if (cos_psi > 0.0) {
            qs += ALPHA_S * 0.30 * flux * FACES[i].area * F * cos_psi;    /* 알베도 */
        }
        if (!S.eclipse) {
            const double c = v3_dot(FACES[i].n, sun_b);
            if (c > 0.0) {
                qs += ALPHA_S * flux * FACES[i].area * c;                 /* 직달 태양 */
            }
        }
    }
    const double Ts = S.Ts + 273.15;
    qs -= S.p_used;                                    /* 전기로 빠져나간 몫 */
    qs += L->s;
    qs -= EPS_IR * SIGMA_SB * A_TOTAL * Ts * Ts * Ts * Ts;
    qs += G_SE * (S.Te - S.Ts);
    const double qe = L->e + G_SE * (S.Ts - S.Te) + G_EB * (S.Tb - S.Te) + G_EP * (S.Tp - S.Te);
    const double qb = L->b + S.i_batt * S.i_batt * S.r_pack + G_EB * (S.Te - S.Tb);
    const double qp = L->p + G_EP * (S.Te - S.Tp);
    S.Ts += qs * dt / C_S;
    S.Te += qe * dt / C_E;
    S.Tb += qb * dt / C_B;
    S.Tp += qp * dt / C_P;
}

/* ---------------- 초기화·스텝 ---------------- */

void phys_init(uint64_t seed, double u0_deg)
{
    memset(&S, 0, sizeof S);
    S.rng = seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL;

    /* 사출 시각 2026-10-10 00:00 UTC */
    S.jd0 = jd_utc(2026, 10, 10, 0, 0, 0.0);
    S.unix0 = (S.jd0 - 2440587.5) * 86400.0;

    /* 600 km 태양동기궤도, 승교점 지방시 22:30 */
    S.a = R_E + 600.0e3;
    const double n = sqrt(MU_E / (S.a * S.a * S.a));
    const double k = (R_E / S.a) * (R_E / S.a);
    S.inc = acos(-1.991063853e-7 / (1.5 * n * J2_E * k));
    v3 sun;
    double au;
    sun_eci(S.jd0, &sun, &au);
    S.raan0 = atan2(sun.y, sun.x) + (22.5 - 12.0) * 15.0 * D2R;
    S.raan_dot = -1.5 * n * J2_E * k * cos(S.inc);
    S.u_dot = n * (1.0 + 0.75 * J2_E * k * (8.0 * cos(S.inc) * cos(S.inc) - 2.0));
    S.u0 = u0_deg * D2R;

    /* 2U, 2.2 kg */
    S.I = v3_(0.0110, 0.0115, 0.0042);
    S.q = q_normalize((quat){0.82, 0.31, -0.42, 0.25});
    S.w = v3_(6.0 * D2R, -8.0 * D2R, 5.0 * D2R);            /* 사출 직후 텀블링 약 11°/s */
    S.m_res = v3_(2.0e-3, -1.5e-3, 3.0e-3);                 /* 잔류자기 [A·m²] */
    S.mtq_scale = v3_(1.03, 0.97, 1.02);                    /* 토커 보정 오차 */

    S.mag_bias = v3_(150e-9, -80e-9, 60e-9);
    S.gyro_bias = v3_(0.12 * D2R, -0.08 * D2R, 0.05 * D2R);
    S.ibat_bias = 0.004;

    S.soc = 0.78;
    S.cap_ah = 2.95;
    S.Ts = S.Te = S.Tb = S.Tp = 18.0;
    S.r_pack = 0.10;
    S.v_batt = 2.0 * ocv_cell(S.soc);
    S.charge_en = true;

    S.cpu_up = true;
    S.reset_cause = RESET_POWER_ON;
    S.first_tx_t = -1.0;
    S.first_burn_t = -1.0;
    ewod_init();
    environment();
    S.b_body = q_rot_inv(S.q, S.b_eci);
}

void phys_step(double dt)
{
    environment();

    if (S.rail[RAIL_ADCS]) {
        S.m_applied = v3_(S.m_cmd.x * S.mtq_scale.x, S.m_cmd.y * S.mtq_scale.y, S.m_cmd.z * S.mtq_scale.z);
    } else {
        S.m_applied = v3_(0, 0, 0);
    }
    att_rk4(dt / 2);
    att_rk4(dt / 2);
    S.b_body = q_rot_inv(S.q, S.b_eci);

    const v3 sun_b = q_rot_inv(S.q, S.sun);
    const loads_t L = compute_loads();
    power_step(dt, &L, sun_b);
    thermal_step(dt, &L, sun_b);
    ewod_step(dt);

    /* 안테나 고정줄: 열선 누적 7초면 끊어진다 (첫 5초 가열로는 부족) */
    if (S.burnwire) {
        S.burn_acc += dt;
        if (S.burn_acc >= 7.0) {
            S.ant_deployed = true;
        }
    }
}
