/*
 * 비행 소프트웨어 최상위: 부팅, 스케줄러, 모드 관리, 안테나 전개, 이벤트 로그
 *
 * 스케줄 (100 ms 틱, 1초 = 10틱)
 *   틱 0   : 1 Hz 작업 (전력, 모드), MET 증가, 상태 저장
 *   틱 0-7 : 자기토커 구동
 *   틱 8   : 자기토커 끔 (자력계 측정 전 잔류자기 안정화)
 *   틱 9   : 자력계·자이로 측정 → B-dot 계산
 *   매 틱  : 탑재체 시퀀서, 무선 송수신, 워치독
 */
#include <string.h>

#include "fsw.h"
#include "fsw_internal.h"

fsw_t g_fsw;
#define G g_fsw

/* ---------------- 이벤트 로그 ---------------- */

static int16_t clamp16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

void fsw_event(uint16_t id, int32_t a, int32_t b)
{
    tm_event_t *e = &G.evlog[G.ev_head];
    e->met_s = G.met_s;
    e->id = id;
    e->a = clamp16(a);
    e->b = clamp16(b);
    G.ev_head = (uint16_t)((G.ev_head + 1u) % EVLOG_N);
    if (G.ev_count < EVLOG_N) {
        G.ev_count++;
    }
    G.last_event = id;
    hal_event_hook(G.met_s, id, a, b);
}

/* 최근 이벤트를 오래된 순서로 최대 max개 복사 */
uint8_t fsw_events_latest(tm_event_t *out, uint8_t max)
{
    const uint16_t n = (G.ev_count < max) ? G.ev_count : max;
    for (uint16_t i = 0; i < n; i++) {
        const uint16_t idx = (uint16_t)((G.ev_head + EVLOG_N - n + i) % EVLOG_N);
        out[i] = G.evlog[idx];
    }
    return (uint8_t)n;
}

/* ---------------- 공용 ---------------- */

uint32_t fsw_utc(void)
{
    return (G.nv.utc_offset != 0u) ? G.met_s + G.nv.utc_offset : 0u;
}

void fsw_persist(void)
{
    G.nv.met_s = G.met_s;
    persist_save(&G.nv);
}

void fsw_set_mode(uint8_t m)
{
    const uint8_t old = G.mode;
    G.mode = m;
    G.mode_since = G.met_s;
    switch (m) {
    case MODE_DEPLOY_WAIT:
        adcs_set_mode(ADCS_OFF);
        break;
    case MODE_DETUMBLE:
    case MODE_NOMINAL:
        adcs_set_mode(ADCS_BDOT);
        break;
    case MODE_SAFE:
        adcs_set_mode(ADCS_SENSE);   /* 센서만 켜 두고 토커는 끈다 */
        (void)payload_abort(PLR_ABORT_MODE);
        break;
    default:
        break;
    }
    comms_set_beacon_period(m == MODE_SAFE ? (uint16_t)BEACON_SAFE_S : G.nv.beacon_period_s);
    G.nv.last_mode = m;
    fsw_persist();
    fsw_event(EVT_MODE, old, m);
}

/* ---------------- 안테나 전개 (사출 후 30분) ---------------- */

static void deploy_step(void)
{
    if (G.met_s < DEPLOY_WAIT_S) {
        return;                                   /* 침묵 대기 */
    }
    if (G.nv.deploy_done) {
        fsw_set_mode(MODE_DETUMBLE);
        return;
    }
    if (G.burning) {
        if (G.met_s - G.burn_start < BURN_ON_S) {
            return;
        }
        hal_burnwire(false);
        G.burning = false;
        G.burn_end = G.met_s;
        if (hal_antenna_deployed()) {
            G.nv.deploy_done = 1u;
            G.nv.ant_ok = 1u;
            fsw_persist();
            fsw_event(EVT_ANT_OK, G.nv.burn_tries, 0);
        } else if (G.nv.burn_tries >= BURN_MAX_TRIES) {
            G.nv.deploy_done = 1u;               /* 포기하고 임무 진행 (접힌 안테나로도 약하게 송신됨) */
            G.nv.ant_ok = 0u;
            fsw_persist();
            fsw_event(EVT_ANT_FAIL, G.nv.burn_tries, 0);
        }
        return;
    }
    if (hal_antenna_deployed()) {                 /* 이전 부팅에서 이미 펴졌는데 저장 전에 리셋된 경우 */
        G.nv.deploy_done = 1u;
        G.nv.ant_ok = 1u;
        fsw_persist();
        fsw_event(EVT_ANT_OK, G.nv.burn_tries, 0);
        return;
    }
    if (G.nv.burn_tries >= BURN_MAX_TRIES) {
        /* 열선이 큰 전류를 끌어 저전압 리셋이 나도, 재부팅마다 다시 가열하는 무한 반복을 막는다 */
        G.nv.deploy_done = 1u;
        G.nv.ant_ok = 0u;
        fsw_persist();
        fsw_event(EVT_ANT_FAIL, G.nv.burn_tries, 0);
        return;
    }
    if (G.nv.burn_tries == 0u || G.met_s - G.burn_end >= BURN_RETRY_S) {
        G.nv.burn_tries++;
        fsw_persist();                            /* 가열 전에 저장: 가열 중 리셋돼도 횟수가 남는다 */
        hal_burnwire(true);
        G.burning = true;
        G.burn_start = G.met_s;
        fsw_event(EVT_BURN, G.nv.burn_tries, 0);
    }
}

/* ---------------- 모드 관리 (1 Hz) ---------------- */

static void mode_task(void)
{
    const bool want_safe = G.eps.low_power || G.nv.safe_latched;
    switch (G.mode) {
    case MODE_DEPLOY_WAIT:
        deploy_step();
        break;
    case MODE_DETUMBLE:
        if (want_safe) {
            fsw_set_mode(MODE_SAFE);
        } else if (G.adcs.low_s >= RATE_LOW_HOLD_S) {
            fsw_event(EVT_DETUMBLED, (int32_t)(G.adcs.rate / DEG2RAD * 100.0f), 0);
            fsw_set_mode(MODE_NOMINAL);
        }
        break;
    case MODE_NOMINAL:
        if (want_safe) {
            fsw_set_mode(MODE_SAFE);
        } else if (G.adcs.high_s >= RATE_HIGH_HOLD_S) {
            fsw_event(EVT_TUMBLING, (int32_t)(G.adcs.rate / DEG2RAD * 100.0f), 0);
            fsw_set_mode(MODE_DETUMBLE);
        }
        break;
    case MODE_SAFE:
        if (!want_safe && G.met_s - G.mode_since >= SAFE_MIN_S) {
            fsw_set_mode(MODE_DETUMBLE);          /* 각속도부터 다시 확인 */
        }
        break;
    default:
        fsw_set_mode(MODE_SAFE);
        break;
    }
}

/* ---------------- 부팅 ---------------- */

void fsw_init(void)
{
    memset(&G, 0, sizeof G);
    G.mode = 0xFFu;
    G.reset_cause = (uint8_t)hal_reset_cause();
    hal_burnwire(false);

    const uint8_t slots = persist_load(&G.nv);
    if (slots == 0u) {
        /* 유효한 저장 상태 없음 = 사출 직후 첫 부팅 */
        memset(&G.nv, 0, sizeof G.nv);
        G.nv.magic = PERSIST_MAGIC;
        G.nv.version = PERSIST_VERSION;
        G.nv.beacon_period_s = BEACON_NOMINAL_S;
        G.first_boot = true;
    }
    G.nv.boot_count++;
    if (!G.first_boot && G.nv.met_s - G.nv.last_boot_met < BOOTLOOP_WINDOW_S) {
        G.nv.quick_reboots++;
    } else {
        G.nv.quick_reboots = 0u;
    }
    G.nv.last_boot_met = G.nv.met_s;
    G.met_s = G.nv.met_s;
    G.burn_end = G.met_s;                         /* 전개 중 리셋됐다면 배터리가 회복되게 60초 쉬고 재가열 */
    if (G.first_boot) {
        fsw_persist();                            /* 두 슬롯 모두 초기화 */
    }
    fsw_persist();

    eps_init();
    adcs_init();
    comms_init();
    payload_init();

    fsw_event(EVT_BOOT, G.nv.boot_count, G.reset_cause);
    if (slots == 1u || slots == 2u) {
        fsw_event(EVT_NVM_FALLBACK, slots, 0);
    }

    uint8_t m;
    if (!G.nv.deploy_done) {
        m = MODE_DEPLOY_WAIT;
    } else if (G.nv.quick_reboots >= BOOTLOOP_LIMIT) {
        fsw_event(EVT_BOOTLOOP, G.nv.quick_reboots, 0);
        m = MODE_SAFE;
    } else if (G.nv.safe_latched || G.nv.last_mode == MODE_SAFE) {
        m = MODE_SAFE;
    } else {
        m = MODE_DETUMBLE;                        /* 리셋 뒤에는 각속도부터 다시 확인 */
    }
    fsw_set_mode(m);
}

/* ---------------- 스케줄러 ---------------- */

void fsw_tick(void)
{
    const uint32_t sub = G.tick % TICK_HZ;
    if (sub == 0u) {
        if (G.tick != 0u) {
            G.uptime_s++;
            G.met_s++;
        }
        eps_task_1hz();
        mode_task();
        fsw_persist();                            /* FRAM이라 매초 써도 수명 문제 없음 */
    }
    adcs_tick(sub);
    payload_tick(sub);
    comms_tick(sub);
    G.tick++;
    hal_wdt_kick();
}

void fsw_view(fsw_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->met_s = G.met_s;
    v->mode = G.mode;
    v->pl_state = G.pl.state;
    v->soc_est = G.eps.soc;
    v->soc_valid = G.eps.soc_init;
    v->rate_est_rad = G.adcs.rate;
    v->rate_valid = G.adcs.rate_valid;
    memcpy(v->m_cmd, G.adcs.m_cmd, sizeof v->m_cmd);
    v->boot_count = G.nv.boot_count;
    v->cmd_seq = G.nv.cmd_seq;
    v->rx_ok = G.comms.rx_ok;
    v->rx_bad = G.comms.rx_bad;
    v->auth_fail = G.comms.auth_fail;
    v->replay = G.comms.replay;
    v->batt_heater = G.eps.heater;
    v->charge_inhibit = G.eps.charge_inhibit;
    v->low_power = G.eps.low_power;
    v->exp_count = G.nv.exp_count;
    v->beacon_period_s = G.comms.beacon_period;
}
