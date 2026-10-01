/*
 * 자세결정·제어: B-dot 디텀블링
 *
 * 사출 직후 위성은 초당 수~수십 도로 텀블링한다. 기체 좌표계에서 본 지구자기장 방향이
 * 변하는 속도(d(b̂)/dt)의 반대 방향으로 자기쌍극자를 만들면 토크 m×B가 회전 에너지를
 * 빼낸다:  m = -(K/|B|)·d(b̂)/dt   (Avanzini & Giulietti 2012 형태)
 *
 * 자기토커를 켠 상태로 자력계를 읽으면 토커가 만든 자기장이 섞이므로,
 * 1초 중 0.8초만 구동하고 0.2초는 끈 뒤 측정한다.
 */
#include <math.h>
#include <string.h>

#include "fsw_internal.h"

#define A g_fsw.adcs

static const float ZERO3[3] = {0.0f, 0.0f, 0.0f};

static float norm3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void adcs_init(void)
{
    memset(&A, 0, sizeof A);
    hal_mtq_set(ZERO3);
    hal_rail_set(RAIL_ADCS, false);
}

void adcs_set_mode(uint8_t m)
{
    A.mode = m;
    hal_rail_set(RAIL_ADCS, m != ADCS_OFF);
    memset(A.m_cmd, 0, sizeof A.m_cmd);
    hal_mtq_set(ZERO3);
    A.prev_valid = false;
    A.rate_valid = false;
    A.low_s = 0;
    A.high_s = 0;
}

static void bdot_law(float bn)
{
    const float k = BDOT_GAIN / bn;
    float peak = 0.0f;
    for (int i = 0; i < 3; i++) {
        A.m_cmd[i] = -k * A.bhat_dot[i];
        if (fabsf(A.m_cmd[i]) > peak) {
            peak = fabsf(A.m_cmd[i]);
        }
    }
    if (peak > MTQ_MAX_AM2) {                     /* 방향은 유지하고 크기만 포화 */
        const float s = MTQ_MAX_AM2 / peak;
        for (int i = 0; i < 3; i++) {
            A.m_cmd[i] *= s;
        }
    }
}

static void measure_and_control(void)
{
    float b[3];
    A.bdot_fresh = false;
    A.mag_ok = hal_mag_read(b);
    const float bn = A.mag_ok ? norm3(b) : 0.0f;
    if (A.mag_ok && (bn < MAG_MIN_T || bn > MAG_MAX_T)) {
        A.mag_ok = false;                         /* 물리적으로 불가능한 값 */
    }

    if (!A.mag_ok) {
        if (++A.mag_fault_n == 10u) {
            fsw_event(EVT_MAG_FAULT, 0, 0);
        }
        A.prev_valid = false;
        memset(A.m_cmd, 0, sizeof A.m_cmd);
    } else {
        A.mag_fault_n = 0;
        if (A.prev_valid) {
            const float pn = norm3(A.b_prev);
            for (int i = 0; i < 3; i++) {
                A.bhat_dot[i] = (b[i] / bn - A.b_prev[i] / pn) / ADCS_DT_S;
            }
            A.bdot_fresh = true;
            if (A.mode == ADCS_BDOT) {
                bdot_law(bn);
            }
        }
        memcpy(A.b_prev, b, sizeof b);
        memcpy(A.b, b, sizeof b);
        A.prev_valid = true;
    }

    /* 각속도 크기: 자이로 우선, 고장 시 |d(b̂)/dt| (자기장 축 회전은 못 보는 하한값) */
    float r = -1.0f;
    A.gyro_ok = hal_gyro_read(A.w);
    if (A.gyro_ok) {
        r = norm3(A.w);
    } else if (A.bdot_fresh) {
        r = norm3(A.bhat_dot);
    }
    if (r >= 0.0f) {
        A.rate = A.rate_valid ? A.rate + RATE_LPF * (r - A.rate) : r;
        A.rate_valid = true;
    }
    if (A.rate_valid) {
        A.low_s = (A.rate < RATE_LOW_RAD) ? A.low_s + 1u : 0u;
        A.high_s = (A.rate > RATE_HIGH_RAD) ? A.high_s + 1u : 0u;
    }
}

void adcs_tick(uint32_t sub)
{
    if (A.mode == ADCS_OFF) {
        return;
    }
    if (sub == 0u) {
        hal_mtq_set(A.mode == ADCS_BDOT ? A.m_cmd : ZERO3);
    } else if (sub == 8u) {
        hal_mtq_set(ZERO3);
    } else if (sub == 9u) {
        measure_and_control();
    }
}
