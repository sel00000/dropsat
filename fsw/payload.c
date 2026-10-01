/*
 * EWOD(전기습윤) 액적 실험 시퀀서
 *
 * 전극 배치:  [E0 저장조] [E1] [E2] [E3] ... [E8]
 *
 *   1. ARMED      예약 시각까지 대기. 시작 조건(정상모드·일조·SoC·각속도)이 맞을 때 시작
 *   2. PREHEAT    칩 히터로 20°C 이상까지 예열
 *   3. HVCHECK    고전압 인가 후 유전막 누설전류 확인 (절연파괴 감지)
 *   4. DISPENSE   저장조→E1→E2로 액체를 뻗고, E2에 닿은 걸 확인한 뒤 E1을 꺼서 액적 하나를 떼어낸다
 *                 (그 전에 트랙에 지난 실험의 액적이 남아 있으면 먼저 저장조로 회수)
 *   5. TRANSPORT  E2→E8→E2 왕복을 지정 횟수만큼. 매 칸 이동시간을 정전용량으로 잰다
 *   6. RETURN     E1으로 옮긴 뒤 저장조와 합쳐 액체를 회수 (우주에선 액체를 버릴 곳이 없다)
 *
 * 각 칸 이동시간(ms)이 과학 데이터다. AC/DC 구동에 따라 유전막 전하 포획으로 속도가
 * 어떻게 변하는지 궤도에서 측정한다.
 */
#include <string.h>

#include "fsw_internal.h"

#define P g_fsw.pl
#define G g_fsw

#define PLR_NONE  0xFFu
#define E_BIT(i)  ((uint16_t)(1u << (i)))
#define RES_BIT   E_BIT(PL_RESERVOIR)

/* ---------------- 공용 ---------------- */

static uint16_t to_u16(float x)
{
    return x <= 0.0f ? 0u : (x >= 65535.0f ? 65535u : (uint16_t)(x + 0.5f));
}

static void drive(uint16_t mask)
{
    P.mask = mask;
    hal_ewod_electrodes(mask);
}

static bool drop_at(uint8_t e)
{
    return hal_ewod_cap_pf(e) > PL_CAP_DETECT_PF;
}

static void outputs_off(void)
{
    hal_ewod_hv(false, 0.0f, false);
    hal_ewod_electrodes(0u);
    hal_payload_heater(false);
    hal_rail_set(RAIL_PAYLOAD, false);
    P.hv_on = false;
    P.heater = false;
    P.mask = 0u;
}

static void save_state(uint8_t s)
{
    G.nv.pl_saved = s;
    G.nv.pl_exp_id = P.exp_id;
    G.nv.pl_start_met = P.start_met;
    G.nv.pl_volts = P.volts;
    G.nv.pl_cycles = P.cycles;
    G.nv.pl_ac = P.ac;
    fsw_persist();
}

static void finish(uint8_t code)
{
    if (P.hv_on) {
        P.leak_end = hal_ewod_leak_ua();
    }
    tm_result_t r;
    memset(&r, 0, sizeof r);
    r.exp_id = P.exp_id;
    r.start_met = P.run_start ? P.run_start : P.start_met;
    r.drive_ac = P.ac;
    r.volts = P.volts;
    r.cycles_plan = P.cycles;
    r.cycles_done = P.cycle;
    r.steps_ok = P.steps_ok;
    r.steps_retry = P.steps_retry;
    r.steps_fail = P.steps_fail;
    r.step_ms_mean = P.steps_ok ? (uint16_t)(P.step_ms_sum / P.steps_ok) : 0u;
    r.step_ms_max = P.step_ms_max;
    r.leak_na_start = to_u16(P.leak_start * 1000.0f);
    r.leak_na_end = to_u16(P.leak_end * 1000.0f);
    r.chip_c = P.temp_n ? (int8_t)(P.temp_sum / (float)P.temp_n + 0.5f) : 0;
    r.result = code;
    r.disp_ok = P.disp_ok;
    r.disp_fail = P.disp_fail;
    r.duration_s = P.run_start ? (uint16_t)(G.met_s - P.run_start) : 0u;
    persist_store_result(&r, P.raw, P.n_raw);

    outputs_off();
    P.state = PL_IDLE;
    save_state(0u);
    fsw_event(EVT_PL_DONE, code, P.exp_id);
}

/* ---------------- 외부 인터페이스 ---------------- */

void payload_init(void)
{
    memset(&P, 0, sizeof P);
    outputs_off();
    if (G.nv.pl_saved == 0u) {
        return;
    }
    P.exp_id = G.nv.pl_exp_id;
    P.start_met = G.nv.pl_start_met;
    P.volts = G.nv.pl_volts;
    P.cycles = G.nv.pl_cycles;
    P.ac = G.nv.pl_ac;
    if (G.nv.pl_saved == 1u) {
        P.state = PL_ARMED;                       /* 예약은 리셋 뒤에도 유지 */
    } else {
        finish(PLR_INTERRUPTED);                  /* 실행 중 리셋: 중단으로 기록 */
    }
}

uint8_t payload_schedule(uint32_t delay_s, uint8_t volts, uint8_t cycles, uint8_t ac)
{
    if (volts < PL_VOLTS_MIN || volts > PL_VOLTS_MAX || cycles == 0u || cycles > PL_CYCLES_MAX ||
        ac > 1u || delay_s > PL_DELAY_MAX_S) {
        return ACK_BAD_ARGS;
    }
    if (P.state != PL_IDLE || G.mode == MODE_SAFE) {
        return ACK_REJECTED;
    }
    memset(&P, 0, sizeof P);
    G.nv.exp_count++;
    P.exp_id = G.nv.exp_count;
    P.start_met = G.met_s + delay_s;
    P.volts = volts;
    P.cycles = cycles;
    P.ac = ac;
    P.state = PL_ARMED;
    save_state(1u);
    fsw_event(EVT_PL_ARMED, P.exp_id, (int32_t)(delay_s / 60u));
    return ACK_OK;
}

uint8_t payload_abort(uint8_t code)
{
    if (P.state == PL_IDLE) {
        return ACK_REJECTED;
    }
    finish(code);
    return ACK_OK;
}

void payload_downlink(uint8_t exp_id)
{
    for (uint8_t s = 0; s < PL_RESULT_SLOTS; s++) {
        tm_result_t r;
        if (!persist_load_result(s, &r) || (exp_id != 0xFFu && r.exp_id != exp_id)) {
            continue;
        }
        uint8_t buf[PROTO_MAX_PAYLOAD];
        buf[0] = PLD_SUMMARY;
        const size_t n = tm_result_pack(&r, buf + 1, sizeof buf - 1u);
        (void)comms_send(FT_PAYLOAD, buf, (uint8_t)(n + 1u));

        uint16_t raw[PL_RAW_MAX];
        uint8_t n_raw = 0;
        if (!persist_load_raw(s, r.exp_id, raw, &n_raw)) {
            continue;
        }
        for (uint8_t off = 0; off < n_raw; off = (uint8_t)(off + 90u)) {
            const uint8_t left = (uint8_t)(n_raw - off);
            const uint8_t cnt = (uint8_t)(left > 90u ? 90u : left);
            wbuf_t w;
            wb_init(&w, buf, sizeof buf);
            wb_u8(&w, PLD_STEPS);
            wb_u16(&w, r.exp_id);
            wb_u8(&w, off);
            wb_u8(&w, cnt);
            for (uint8_t i = 0; i < cnt; i++) {
                wb_u16(&w, raw[off + i]);
            }
            (void)comms_send(FT_PAYLOAD, buf, (uint8_t)w.n);
        }
    }
}

/* ---------------- 시퀀서 ---------------- */

static bool start_ok(void)
{
    return G.mode == MODE_NOMINAL && !G.eps.low_power && G.eps.soc >= PL_SOC_START &&
           G.adcs.rate_valid && G.adcs.rate < PL_RATE_START_RAD && eps_sunlit();
}

static uint8_t abort_check(float temp, uint32_t sub)
{
    if (G.mode != MODE_NOMINAL) {
        return PLR_ABORT_MODE;
    }
    if (G.eps.low_power || G.eps.soc < PL_SOC_ABORT) {
        return PLR_ABORT_POWER;
    }
    if (G.adcs.rate_valid && G.adcs.rate > PL_RATE_ABORT_RAD) {
        return PLR_ABORT_RATE;                    /* 원심가속도가 액적을 흔든다 */
    }
    if (temp > PL_TEMP_MAX_C) {
        return PLR_ABORT_TEMP;
    }
    if (G.met_s - P.run_start > PL_RUN_TIMEOUT_S) {
        return PLR_TIMEOUT;
    }
    if (P.hv_on && sub == 5u && hal_ewod_leak_ua() > PL_LEAK_MAX_UA) {
        return PLR_ABORT_LEAK;
    }
    return PLR_NONE;
}

static void thermostat(float t)
{
    if (!P.heater && t < PL_HEAT_ON_C) {
        P.heater = true;
        hal_payload_heater(true);
    } else if (P.heater && t > PL_HEAT_OFF_C) {
        P.heater = false;
        hal_payload_heater(false);
    }
}

static void record_step(uint16_t ms)
{
    if (P.n_raw < PL_RAW_MAX) {
        P.raw[P.n_raw++] = ms;
    }
}

static void step_begin(uint8_t target)
{
    P.target = target;
    P.step_t = 0;
    P.step_total_t = 0;
    P.retries = 0;
    P.settle = 0;
    P.repulse = false;
    drive((uint16_t)(RES_BIT | E_BIT(target)));  /* 저장조 전극은 계속 켜서 남은 액체를 붙잡아 둔다 */
}

static void begin_merge(void)
{
    P.dsub = 1u;                                  /* E1을 끄면 저장조가 액적을 끌어당겨 합쳐진다 */
    P.step_t = 0;
    P.settle = 0;
    drive(RES_BIT);
}

/* 다음 목적지: E2→E8→E2 왕복이 1사이클. 다 돌면 한 칸씩 E1까지 옮긴 뒤 저장조와 합친다 */
static void next_step(void)
{
    if (P.state == PL_RETURN) {
        if (P.pos > PL_TRACK_FIRST) {
            step_begin((uint8_t)(P.pos - 1u));
        } else {
            begin_merge();
        }
        return;
    }
    if (P.dir > 0 && P.pos >= PL_TRACK_LAST) {
        P.dir = -1;
    } else if (P.dir < 0 && P.pos <= PL_TRACK_FIRST + 1u) {
        P.cycle++;
        if (P.cycle >= P.cycles) {
            P.state = PL_RETURN;
            P.dsub = 0u;
            step_begin(PL_TRACK_FIRST);
            return;
        }
        P.dir = 1;
    }
    step_begin((uint8_t)((int)P.pos + P.dir));
}

static void transport_tick(void)
{
    if (P.repulse) {                              /* 재시도: 한 틱 껐던 전극을 다시 켠다 */
        P.repulse = false;
        drive((uint16_t)(RES_BIT | E_BIT(P.target)));
        return;
    }
    if (P.settle > 0u) {                          /* 도착 감지 후 중앙에 자리잡을 때까지 */
        if (--P.settle == 0u) {
            P.pos = P.target;
            next_step();
        }
        return;
    }
    P.step_t++;
    P.step_total_t++;
    if (drop_at(P.target)) {
        if (!P.clearing) {                        /* 회수 중 이동은 실험 데이터에 넣지 않는다 */
            const uint16_t ms = (uint16_t)(P.step_total_t * 100u);
            P.steps_ok++;
            P.step_ms_sum += ms;
            if (ms > P.step_ms_max) {
                P.step_ms_max = ms;
            }
            record_step(ms);
        }
        P.settle = PL_SETTLE_T;
        return;
    }
    if (P.step_t >= PL_STEP_TIMEOUT_T) {
        if (P.retries < PL_STEP_RETRIES) {
            P.retries++;
            if (!P.clearing) {
                P.steps_retry++;
            }
            P.step_t = 0;
            drive(RES_BIT);
            P.repulse = true;
        } else if (P.clearing) {
            finish(PLR_TRACK_BLOCKED);            /* 남은 액적을 못 치움 → 실험 못 함 */
        } else {
            P.steps_fail++;
            record_step(0xFFFFu);
            finish(PLR_STUCK);
        }
    }
}

/* 트랙(E1..E8)에 액적이 남아 있으면 그 전극 번호, 없으면 0 */
static uint8_t find_leftover(void)
{
    for (uint8_t k = PL_TRACK_FIRST; k <= PL_TRACK_LAST; k++) {
        if (drop_at(k)) {
            return k;
        }
    }
    return 0u;
}

/* E1에서 저장조로 합치기: E1이 빈 뒤에도 PL_MERGE_T 동안 더 끌어당겨 확실히 합친다 */
static void merge_tick(void)
{
    P.step_t++;
    if (!drop_at(PL_TRACK_FIRST)) {
        P.settle++;
    }
    if (P.settle >= PL_MERGE_T) {
        if (P.clearing) {                         /* 회수 끝 → 이제 진짜 실험 시작 */
            P.clearing = false;
            P.state = PL_DISPENSE;
            P.dsub = 0u;
            P.step_t = 0;
            P.settle = 0;
        } else {
            finish(PLR_OK);
        }
    } else if (P.step_t >= PL_MERGE_TIMEOUT_T) {
        finish(P.clearing ? PLR_TRACK_BLOCKED : PLR_OK);
    }
}

static void dispense_failed(void)
{
    P.disp_fail++;
    if (++P.disp_tries >= PL_DISPENSE_TRIES) {
        finish(PLR_DISPENSE_FAIL);
    } else {
        P.dsub = 0u;
        P.step_t = 0;
        drive(RES_BIT);                                          /* 액체를 저장조로 되돌린 뒤 다시 */
    }
}

/* 시간으로 끊지 않고 정전용량으로 확인하며 진행한다 (폐루프) */
static void dispense_tick(void)
{
    P.step_t++;
    if (P.dsub == 0u) {                                          /* 1) 액체 손가락을 E2까지 뻗는다 */
        if (P.step_t == 1u) {
            drive((uint16_t)(RES_BIT | E_BIT(1) | E_BIT(2)));
        }
        if (drop_at(2)) {
            P.dsub = 1u;
            P.step_t = 0;
        } else if (P.step_t >= PL_DISP_EXTEND_T) {
            dispense_failed();                                   /* 제한 시간 안에 E2까지 못 감 */
        }
        return;
    }
    if (P.dsub == 1u) {                                          /* 2) E2 위에 자리잡을 때까지 잠깐 */
        if (P.step_t >= PL_SETTLE_T) {
            P.dsub = 2u;
            P.step_t = 0;
            drive((uint16_t)(RES_BIT | E_BIT(2)));               /* 3) E1을 꺼서 목을 끊는다 */
        }
        return;
    }
    if (P.step_t < PL_DISP_CUT_T) {
        return;
    }
    if (drop_at(2) && !drop_at(1)) {                             /* 4) E2에만 액적이 남았나 */
        P.disp_ok++;
        P.pos = 2u;
        P.dir = 1;
        P.cycle = 0u;
        P.state = PL_TRANSPORT;
        step_begin(3u);
    } else {
        dispense_failed();
    }
}

void payload_tick(uint32_t sub)
{
    if (P.state == PL_IDLE) {
        return;
    }
    if (P.state == PL_ARMED) {
        if (sub != 0u || G.met_s < P.start_met) {
            return;
        }
        if (G.met_s - P.start_met > PL_ARM_EXPIRE_S) {
            finish(PLR_EXPIRED);
            return;
        }
        if (!start_ok()) {
            return;                               /* 조건이 맞을 때까지 대기 */
        }
        P.run_start = G.met_s;
        P.state_since = G.met_s;
        P.state = PL_PREHEAT;
        hal_rail_set(RAIL_PAYLOAD, true);
        save_state(2u);
        fsw_event(EVT_PL_START, P.exp_id, (int32_t)(G.eps.soc * 100.0f));
        return;
    }

    const float temp = hal_payload_temp_c();
    thermostat(temp);
    if (P.hv_on && sub == 0u) {
        P.temp_sum += temp;
        P.temp_n++;
    }
    const uint8_t ab = abort_check(temp, sub);
    if (ab != PLR_NONE) {
        finish(ab);
        return;
    }

    switch (P.state) {
    case PL_PREHEAT:
        if (sub != 0u) {
            break;
        }
        if (temp >= PL_PREHEAT_MIN_C) {
            hal_ewod_hv(true, (float)P.volts, P.ac != 0u);
            P.hv_on = true;
            P.state = PL_HVCHECK;
            P.step_t = 0;
        } else if (G.met_s - P.state_since >= PL_PREHEAT_TIMEOUT_S) {
            finish(PLR_PREHEAT_TIMEOUT);
        }
        break;

    case PL_HVCHECK:
        if (++P.step_t >= PL_HV_SETTLE_T) {
            P.leak_start = hal_ewod_leak_ua();
            const uint8_t left = find_leftover();
            if (P.leak_start > PL_LEAK_MAX_UA) {
                finish(PLR_ABORT_LEAK);
            } else if (left != 0u) {
                /* 지난 실험(예: STUCK)에서 남은 액적이 트랙에 있다 → 먼저 저장조로 회수 */
                P.clearing = true;
                P.state = PL_RETURN;
                P.pos = left;
                if (left > PL_TRACK_FIRST) {
                    P.dsub = 0u;
                    step_begin((uint8_t)(left - 1u));
                } else {
                    begin_merge();
                }
            } else {
                P.state = PL_DISPENSE;
                P.dsub = 0u;
                P.step_t = 0;
            }
        }
        break;

    case PL_DISPENSE:
        dispense_tick();
        break;

    case PL_TRANSPORT:
        transport_tick();
        break;

    case PL_RETURN:
        if (P.dsub == 0u) {
            transport_tick();
        } else {
            merge_tick();
        }
        break;

    default:
        finish(PLR_ABORT_CMD);
        break;
    }
}
