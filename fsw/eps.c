/*
 * 전력 관리
 *  - 배터리 잔량(SoC) 추정: 쿨롱 카운팅 + 개방전압(OCV) 기반 느린 보정 + 만충 시 100% 재설정
 *  - 배터리 히터 (온도 히스테리시스)
 *  - 저온 충전 금지: 0°C 이하에서 리튬이온을 충전하면 음극에 리튬이 석출된다
 *  - 저전력 판정 → 모드 관리자가 SAFE로 전환
 */
#include <string.h>

#include "fsw_internal.h"

#define E g_fsw.eps

/* 셀 개방전압 [V] vs SoC 0,10,...,100 % (데이터시트 값) */
static const float OCV_CELL[11] = {3.00f, 3.45f, 3.55f, 3.62f, 3.68f, 3.73f,
                                   3.79f, 3.87f, 3.95f, 4.05f, 4.18f};

static float soc_from_ocv(float v_cell)
{
    if (v_cell <= OCV_CELL[0]) {
        return 0.0f;
    }
    for (int i = 0; i < 10; i++) {
        if (v_cell < OCV_CELL[i + 1]) {
            const float f = (v_cell - OCV_CELL[i]) / (OCV_CELL[i + 1] - OCV_CELL[i]);
            return ((float)i + f) / 10.0f;
        }
    }
    return 1.0f;
}

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static int32_t deci(float x)
{
    return (int32_t)(x * 10.0f);
}

void eps_init(void)
{
    memset(&E, 0, sizeof E);
    hal_rail_set(RAIL_BATT_HEATER, false);
    hal_charge_enable(true);
}

bool eps_sunlit(void)
{
    return E.raw_ok && E.raw.panel_v > SUN_PANEL_V;
}

void eps_task_1hz(void)
{
    E.raw_ok = hal_eps_read(&E.raw);
    if (!E.raw_ok) {
        return;
    }
    const hal_eps_t *r = &E.raw;

    /* SoC 추정 */
    const float ocv_cell = 0.5f * (r->batt_v - r->batt_a * BATT_R_PACK);
    const float soc_v = soc_from_ocv(ocv_cell);
    if (!E.soc_init) {
        E.soc = soc_v;
        E.soc_init = true;
    } else {
        E.soc += r->batt_a / (3600.0f * BATT_CAPACITY_AH);    /* 1초 쿨롱 카운팅 */
        E.soc += SOC_OCV_GAIN * (soc_v - E.soc);
    }
    if (r->batt_v >= EOC_V && r->batt_a > 0.0f && r->batt_a < EOC_A) {
        if (++E.eoc_s >= 60u) {
            E.soc = 1.0f;                                    /* 정전압 충전 끝 = 만충 */
        }
    } else {
        E.eoc_s = 0;
    }
    E.soc = clamp01(E.soc);

    /* 배터리 히터 */
    if (!E.heater && r->batt_c < BATT_HEAT_ON_C) {
        E.heater = true;
        hal_rail_set(RAIL_BATT_HEATER, true);
        fsw_event(EVT_BATT_HEATER, 1, deci(r->batt_c));
    } else if (E.heater && r->batt_c > BATT_HEAT_OFF_C) {
        E.heater = false;
        hal_rail_set(RAIL_BATT_HEATER, false);
        fsw_event(EVT_BATT_HEATER, 0, deci(r->batt_c));
    }

    /* 저온 충전 금지 */
    if (!E.charge_inhibit && r->batt_c < BATT_CHARGE_MIN_C) {
        E.charge_inhibit = true;
        hal_charge_enable(false);
        fsw_event(EVT_CHARGE_INHIBIT, 1, deci(r->batt_c));
    } else if (E.charge_inhibit && r->batt_c > BATT_CHARGE_RESUME_C) {
        E.charge_inhibit = false;
        hal_charge_enable(true);
        fsw_event(EVT_CHARGE_INHIBIT, 0, deci(r->batt_c));
    }

    /* 저전력 판정 (히스테리시스) */
    const bool critical = r->batt_v < VBATT_CRITICAL;
    if (!E.low_power && (E.soc < SOC_SAFE_ENTER || critical)) {
        E.low_power = true;
        fsw_event(EVT_LOW_POWER, (int32_t)(E.soc * 100.0f), (int32_t)(r->batt_v * 1000.0f));
    } else if (E.low_power && E.soc > SOC_SAFE_EXIT && !critical) {
        E.low_power = false;
        fsw_event(EVT_POWER_OK, (int32_t)(E.soc * 100.0f), 0);
    }
}
