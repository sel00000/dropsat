/*
 * EWOD 칩 물리 모델 (단순화)
 *
 *  - 전기습윤 힘 F ∝ ε0·εr·V²/(2d). 접촉각 히스테리시스 때문에 문턱전압 V_th 이상에서만 움직인다.
 *      속도 v = K·(V_eff² − V_th²),  V_eff = V − V_trap
 *  - V_trap: 유전막에 포획된 전하가 걸어 주는 전압을 깎아 먹는 효과.
 *      DC 구동에서는 빠르게 쌓이고, AC 구동에서는 극성이 계속 바뀌어 거의 쌓이지 않는다.
 *      (누적 속도 값은 실험으로 정해야 하는 "가정"이다)
 *  - 액적 길이는 전극 피치의 1.4배라서 중앙에 있으면 이웃 전극에 살짝 걸친다 → 이웃 전극을 켜면 끌려간다.
 *  - 전극 정전용량은 액적이 덮은 면적에 비례 → 위치 감지.
 */
#include <math.h>
#include <string.h>

#include "sim.h"

#define EW_L          1.4       /* 액적 길이 [피치] */
#define EW_REACH      (0.5 * EW_L + 0.5)
#define EW_VTH25      55.0      /* 25°C 문턱전압 [Vrms] */
#define EW_VREF       110.0
#define EW_SPEED_REF  3.0       /* 110 V, 25°C 이동속도 [피치/s] (피치 2 mm → 6 mm/s) */
#define EW_TRAP_DC    0.8       /* DC 구동 중 V_trap 누적 [V/s] */
#define EW_TRAP_AC    0.01      /* AC 구동 중 V_trap 누적 [V/s] */
#define EW_TRAP_TAU   3600.0    /* 전압을 끄면 포획전하가 빠지는 시정수 [s] */
#define EW_CAP_BASE   6.0       /* 빈 전극 [pF] */
#define EW_CAP_DROP   42.0      /* 액적이 다 덮었을 때 추가 [pF] */
#define EW_CAP_RES    90.0      /* 저장조 */

/* 전극 5번 표면 결함: 문턱전압 +4 V */
static const double DEFECT_V[10] = {0, 0, 0, 0, 0, 4.0, 0, 0, 0, 0};

void ewod_init(void)
{
    memset(&S.ew, 0, sizeof S.ew);
    S.ew.jitter = 1.0;
    S.ew.last_target = -1;
}

static bool on(int k)
{
    return ((S.electrodes >> k) & 1u) != 0u;
}

static double speed_to(int k)
{
    const double ve = S.hv_v - S.ew.v_trap;
    const double vth = EW_VTH25 * (1.0 + 0.006 * (25.0 - S.Tp)) + DEFECT_V[k];
    if (ve <= vth) {
        return 0.0;
    }
    return EW_SPEED_REF * (ve * ve - vth * vth) / (EW_VREF * EW_VREF - EW_VTH25 * EW_VTH25) * S.ew.jitter;
}

void ewod_step(double dt)
{
    ewod_t *e = &S.ew;
    const bool hv = hv_effective();
    if (hv && S.electrodes != 0u) {
        e->v_trap += (S.hv_ac ? EW_TRAP_AC : EW_TRAP_DC) * (S.hv_v / EW_VREF) * dt;
    } else {
        e->v_trap *= exp(-dt / EW_TRAP_TAU);
    }
    if (!hv) {
        e->finger = 0.0;
        return;
    }

    if (!e->drop) {
        /* 디스펜스: 저장조(E0)에서 액체 손가락이 E1, E2 위로 뻗는다 */
        if (on(0) && on(1) && on(2)) {
            if (e->finger <= 0.0) {
                e->jitter = fmax(0.7, 1.0 + 0.08 * rng_n());   /* 시도마다 속도 편차 */
            }
            e->finger = fmin(2.0, e->finger + speed_to(2) * dt);
        } else if (on(0) && !on(1) && on(2)) {
            if (e->finger >= 1.8) {
                /* 목(neck)이 끊어지며 액적 분리. 힘이 약하면 저장조로 도로 빨려 들어간다 */
                double p = (S.hv_v - e->v_trap - 70.0) / 35.0;
                p = fmin(0.97, fmax(0.05, p));
                if (rng_u() < p) {
                    e->drop = true;
                    e->x = 2.0;
                }
            }
            e->finger = 0.0;
        } else {
            e->finger = fmax(0.0, e->finger - 3.0 * dt);
        }
        return;
    }

    /* 수송: 액적이 걸쳐 있는 켜진 전극 중 가장 가까운 쪽으로 끌려간다 */
    int best = -1;
    double bd = 1e9;
    for (int k = 0; k < 10; k++) {
        if (!on(k)) {
            continue;
        }
        const double d = fabs(k - e->x);
        if (d < EW_REACH && d < bd) {
            bd = d;
            best = k;
        }
    }
    if (best < 0) {
        return;
    }
    if (best != e->last_target) {
        e->last_target = best;
        e->jitter = fmax(0.7, 1.0 + 0.08 * rng_n());
    }
    const double dx = best - e->x;
    const double step = speed_to(best) * dt;
    if (fabs(dx) <= step) {
        e->x = best;
    } else {
        e->x += dx > 0 ? step : -step;
    }
    if (best == 0 && fabs(e->x) < 0.3) {
        e->drop = false;                 /* 저장조와 합쳐짐 */
        e->x = 0.0;
    }
}

double ewod_cap(int k)
{
    if (k == 0) {
        return EW_CAP_BASE + EW_CAP_RES;
    }
    double cov = 0.0;
    if (S.ew.drop) {
        const double lo = fmax(S.ew.x - 0.5 * EW_L, k - 0.5);
        const double hi = fmin(S.ew.x + 0.5 * EW_L, k + 0.5);
        cov = fmax(0.0, hi - lo);
    } else if (k <= 2) {
        cov = fmin(1.0, fmax(0.0, S.ew.finger - (k - 1)));
    }
    return EW_CAP_BASE + EW_CAP_DROP * cov;
}

double ewod_leak_ua(void)
{
    if (!hv_effective()) {
        return 0.0;
    }
    const double r_diel = 50.0e9 * exp(-(S.Tp - 25.0) / 15.0);   /* 유전막 절연저항 [Ω] */
    return S.hv_v / r_diel * 1e6 + 5.0e-5 * S.ew.v_trap;
}
