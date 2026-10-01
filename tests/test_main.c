/*
 * 비행 소프트웨어 단위·통합 테스트 (가짜 하드웨어 mock_hal 위에서)
 *   make test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "crc16.h"
#include "fsw.h"
#include "fsw_internal.h"
#include "keys.h"
#include "mock_hal.h"
#include "siphash.h"

static int g_checks, g_fails;

#define CHECK(c)                                                       \
    do {                                                               \
        g_checks++;                                                    \
        if (!(c)) {                                                    \
            g_fails++;                                                 \
            printf("      실패 %s:%d  %s\n", __FILE__, __LINE__, #c);  \
        }                                                              \
    } while (0)

#define RUN(fn, desc)                                                  \
    do {                                                               \
        const int f0 = g_fails;                                        \
        fn();                                                          \
        printf("  %s %s\n", g_fails == f0 ? "✓" : "✗", desc);          \
    } while (0)

/* ---------------- 도우미 ---------------- */

static void ticks(long n)
{
    for (long i = 0; i < n; i++) {
        fsw_tick();
    }
}

static void seconds(long s) { ticks(s * (long)TICK_HZ); }

static fsw_view_t view(void)
{
    fsw_view_t v;
    fsw_view(&v);
    return v;
}

static void boot_fresh(void)
{
    mock_power_cycle(true);
    M.ant_deploy_on_burn = true;
    fsw_init();
}

static void reboot(hal_reset_cause_t cause)
{
    mock_power_cycle(false);
    M.ant_deployed = true;                  /* 물리적으로 이미 펴져 있다 */
    M.reset_cause = cause;
    fsw_init();
}

/* 사출 → 45분 지나 송신 허용 + NOMINAL */
static void boot_to_nominal(void)
{
    boot_fresh();
    seconds(RF_SILENCE_S + 5);
}

static uint8_t g_frame[PROTO_MAX_FRAME];
static size_t g_flen;

static void cmd(uint32_t seq, const uint8_t *key, uint8_t op, const uint8_t *args, uint8_t n)
{
    frame_t f;
    memset(&f, 0, sizeof f);
    f.type = FT_CMD;
    f.scid = SPACECRAFT_ID;
    f.seq = seq;
    f.len = (uint8_t)(1u + n);
    f.data[0] = op;
    if (n) {
        memcpy(f.data + 1, args, n);
    }
    g_flen = proto_encode(&f, key, g_frame, sizeof g_frame);
    mock_push_rx(g_frame, g_flen);
    ticks(5);
}

/* seq에 대한 ACK 상태, 없으면 -1 */
static int ack_status(uint32_t seq)
{
    for (int i = M.tx_n - 1; i >= 0; i--) {
        frame_t f;
        if (proto_decode(M.tx[i], M.txlen[i], SPACECRAFT_ID, NULL, &f) == PROTO_OK && f.type == FT_ACK) {
            rbuf_t r;
            rb_init(&r, f.data, f.len);
            (void)rb_u8(&r);
            const uint8_t st = rb_u8(&r);
            if (rb_u32(&r) == seq) {
                return st;
            }
        }
    }
    return -1;
}

static int last_event_a(uint16_t id)
{
    for (int i = M.ev_n - 1; i >= 0; i--) {
        if (M.ev_id[i] == id) {
            return (int)M.ev_a[i];
        }
    }
    return -1;
}

/* ---------------- 테스트 ---------------- */

static void test_crc(void)
{
    const uint8_t s[] = "123456789";
    CHECK(crc16_ccitt(s, 9) == 0x29B1u);    /* CRC-16/CCITT-FALSE 표준 검사값 */
}

static void test_siphash(void)
{
    /* 참조 구현과 대조한 벡터: 키 00..0f, 메시지 00 01 02 ... (길이 n), 출력 바이트 순 */
    static const struct {
        size_t n;
        const char *hex;
    } V[] = {
        {0, "310e0edd47db6f72"}, {1, "fd67dc93c539f874"}, {2, "5a4fa9d909806c0d"}, {7, "37d1018bf50002ab"},
        {8, "6224939a79f5f593"}, {15, "e545be4961ca29a1"}, {63, "724506eb4c328a95"},
    };
    uint8_t key[16], msg[64];
    for (int i = 0; i < 64; i++) {
        msg[i] = (uint8_t)i;
        if (i < 16) {
            key[i] = (uint8_t)i;
        }
    }
    for (size_t k = 0; k < sizeof V / sizeof V[0]; k++) {
        const uint64_t h = siphash24(key, msg, V[k].n);
        char hex[17];
        for (int b = 0; b < 8; b++) {
            snprintf(hex + 2 * b, 3, "%02x", (unsigned)((h >> (8 * b)) & 0xFFu));
        }
        CHECK(strcmp(hex, V[k].hex) == 0);
    }
}

static void test_frames(void)
{
    uint8_t buf[PROTO_MAX_FRAME], t[PROTO_MAX_FRAME];
    frame_t f, g;
    memset(&f, 0, sizeof f);
    f.type = FT_CMD;
    f.scid = SPACECRAFT_ID;
    f.seq = 0x01020304u;
    f.len = 3;
    f.data[0] = OP_SET_BEACON;
    f.data[1] = 60;
    const size_t n = proto_encode(&f, CMD_KEY, buf, sizeof buf);
    CHECK(n == PROTO_HDR_LEN + 3u + PROTO_MAC_LEN + PROTO_CRC_LEN);
    CHECK(proto_decode(buf, n, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_OK);
    CHECK(g.seq == f.seq && g.len == 3 && g.data[1] == 60);

    memcpy(t, buf, n);
    t[11] ^= 0x10;                                       /* 전송 중 비트 하나 뒤집힘 */
    CHECK(proto_decode(t, n, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_ERR_CRC);

    memcpy(t, buf, n);
    t[11] = 10;                                          /* 공격자가 내용을 바꾸고 CRC도 다시 계산 */
    const uint16_t c = crc16_ccitt(t + 2, n - 4);
    t[n - 2] = (uint8_t)(c >> 8);
    t[n - 1] = (uint8_t)(c & 0xFF);
    CHECK(proto_decode(t, n, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_ERR_MAC);

    uint8_t k2[16];
    memcpy(k2, CMD_KEY, 16);
    k2[0] ^= 1;
    CHECK(proto_decode(buf, n, SPACECRAFT_ID, k2, &g) == PROTO_ERR_MAC);
    CHECK(proto_decode(buf, n - 1, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_ERR_LEN);
    CHECK(proto_decode(buf, n, 0x1234, CMD_KEY, &g) == PROTO_ERR_SCID);
    CHECK(proto_decode(buf, 5, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_ERR_SHORT);
    memcpy(t, buf, n);
    t[0] = 0;
    CHECK(proto_decode(t, n, SPACECRAFT_ID, CMD_KEY, &g) == PROTO_ERR_SYNC);

    /* 텔레메트리 직렬화 */
    tm_beacon_t b, b2;
    memset(&b, 0, sizeof b);
    b.met_s = 123456;
    b.batt_ma = -1234;
    b.batt_c = -12;
    b.pl_c = -40;
    b.rate_cdps = 0xFFFF;
    b.last_seq = 0xDEADBEEFu;
    uint8_t p[64];
    CHECK(tm_beacon_pack(&b, p, sizeof p) == TM_BEACON_SIZE);
    CHECK(tm_beacon_pack(&b, p, TM_BEACON_SIZE - 1) == 0);
    CHECK(tm_beacon_unpack(p, TM_BEACON_SIZE, &b2));
    CHECK(b2.met_s == 123456 && b2.batt_ma == -1234 && b2.batt_c == -12 && b2.pl_c == -40);
    CHECK(b2.rate_cdps == 0xFFFF && b2.last_seq == 0xDEADBEEFu);
    CHECK(!tm_beacon_unpack(p, TM_BEACON_SIZE - 1, &b2));

    tm_result_t r, r2;
    memset(&r, 0, sizeof r);
    r.exp_id = 7;
    r.chip_c = -5;
    r.step_ms_mean = 410;
    CHECK(tm_result_pack(&r, p, sizeof p) == TM_RESULT_SIZE);
    CHECK(tm_result_unpack(p, TM_RESULT_SIZE, &r2) && r2.exp_id == 7 && r2.chip_c == -5 && r2.step_ms_mean == 410);
}

static void test_deploy_timeline(void)
{
    boot_fresh();
    M.ant_deploy_on_burn = false;
    CHECK(view().mode == MODE_DEPLOY_WAIT);
    CHECK(!M.rail[RAIL_ADCS]);

    seconds(DEPLOY_WAIT_S);                              /* MET 29:59 */
    CHECK(M.burn_starts == 0);
    CHECK(M.tx_n == 0);
    ticks(1);                                            /* MET 30:00 */
    CHECK(M.burn_starts == 1 && M.burnwire);
    seconds(3);
    M.ant_deployed = true;                               /* 가열 중에 고정줄이 끊어짐 */
    seconds(3);
    CHECK(!M.burnwire);
    CHECK(mock_events(EVT_ANT_OK) == 1);
    seconds(2);
    CHECK(view().mode == MODE_DETUMBLE);
    CHECK(M.rail[RAIL_ADCS]);

    while (view().met_s < RF_SILENCE_S - 1u) {
        ticks(1);
    }
    CHECK(M.tx_n == 0);                                  /* 44:59까지 송신 없음 */
    seconds(3);
    CHECK(M.tx_n >= 1);
    frame_t f;
    tm_beacon_t b;
    CHECK(proto_decode(M.tx[0], M.txlen[0], SPACECRAFT_ID, NULL, &f) == PROTO_OK && f.type == FT_BEACON);
    CHECK(tm_beacon_unpack(f.data, f.len, &b) && b.met_s >= RF_SILENCE_S && b.boot_count == 1);
    CHECK(view().mode == MODE_NOMINAL);                  /* 각속도가 낮으니 그사이 NOMINAL */
}

static void test_antenna_fail(void)
{
    boot_fresh();
    M.ant_deploy_on_burn = false;
    seconds(DEPLOY_WAIT_S + BURN_MAX_TRIES * (BURN_RETRY_S + BURN_ON_S) + 10);
    CHECK(M.burn_starts == (int)BURN_MAX_TRIES);
    CHECK(mock_events(EVT_ANT_FAIL) == 1);
    CHECK(view().mode == MODE_DETUMBLE);
}

/* 열선 가열 중 저전압 리셋이 계속 나도 전체 가열은 5회를 넘지 않아야 한다 */
static void test_burn_cap_across_resets(void)
{
    boot_fresh();
    M.ant_deploy_on_burn = false;
    seconds(DEPLOY_WAIT_S + 2);
    int total = 0;
    for (int i = 0; i < 12; i++) {
        total += M.burn_starts;
        mock_power_cycle(false);                         /* 가열 도중 전원 강하 → 리셋 */
        M.reset_cause = RESET_BROWNOUT;
        fsw_init();
        seconds(BURN_RETRY_S + 3);
    }
    total += M.burn_starts;
    CHECK(total == (int)BURN_MAX_TRIES);
    CHECK(view().mode == MODE_SAFE);                     /* 1분마다 리셋 → 부팅 루프 보호도 작동 */
    CHECK(mock_events(EVT_BOOTLOOP) == 1);
}

static void test_reset_command(void)
{
    boot_to_nominal();
    uint8_t bad[4] = {1, 2, 3, 4};
    cmd(1, CMD_KEY, OP_RESET, bad, 4);
    CHECK(ack_status(1) == ACK_BAD_ARGS);
    CHECK(!M.reset_requested);
    const uint8_t magic[4] = {(uint8_t)(RESET_MAGIC & 0xFF), (uint8_t)((RESET_MAGIC >> 8) & 0xFF),
                              (uint8_t)((RESET_MAGIC >> 16) & 0xFF), (uint8_t)(RESET_MAGIC >> 24)};
    M.tx_n = 0;
    cmd(2, CMD_KEY, OP_RESET, magic, 4);
    CHECK(ack_status(2) == ACK_OK);                      /* ACK를 먼저 내보내고 */
    CHECK(M.reset_requested);                            /* 그다음 리셋 */
}

static void test_commands(void)
{
    boot_to_nominal();
    M.tx_n = 0;
    cmd(1, CMD_KEY, OP_PING, NULL, 0);
    CHECK(ack_status(1) == ACK_OK);
    CHECK(view().rx_ok == 1);

    M.tx_n = 0;
    mock_push_rx(g_frame, g_flen);                       /* 같은 프레임이 한 번 더 */
    ticks(5);
    CHECK(ack_status(1) == ACK_DUPLICATE);
    CHECK(view().rx_ok == 1);                            /* 두 번 실행하지 않음 */

    M.tx_n = 0;
    cmd(0, CMD_KEY, OP_PING, NULL, 0);                   /* 옛 번호 = 재전송 공격 */
    CHECK(ack_status(0) == -1);
    CHECK(view().replay == 1);

    uint8_t bad_key[16] = {0};
    uint8_t one = 1;
    M.tx_n = 0;
    cmd(50, bad_key, OP_SET_MODE, &one, 1);              /* 키 없이 만든 위조 명령 */
    CHECK(ack_status(50) == -1);
    CHECK(view().auth_fail == 1);
    seconds(2);
    CHECK(view().mode == MODE_NOMINAL);

    uint8_t a[2] = {5, 0};
    cmd(2, CMD_KEY, OP_SET_BEACON, a, 2);
    CHECK(ack_status(2) == ACK_BAD_ARGS);
    a[0] = 60;
    cmd(3, CMD_KEY, OP_SET_BEACON, a, 2);
    CHECK(ack_status(3) == ACK_OK);
    CHECK(view().beacon_period_s == 60);
    cmd(4, CMD_KEY, 0x77, NULL, 0);
    CHECK(ack_status(4) == ACK_UNKNOWN_OP);
    cmd(100, CMD_KEY, OP_PING, NULL, 0);                 /* 번호를 건너뛰어도 크면 수락 */
    CHECK(ack_status(100) == ACK_OK);
    CHECK(view().cmd_seq == 100);
}

static void test_reset_persistence(void)
{
    boot_to_nominal();
    for (uint32_t s = 1; s <= 5; s++) {
        cmd(s, CMD_KEY, OP_PING, NULL, 0);
    }
    const uint32_t met_before = view().met_s;
    reboot(RESET_WATCHDOG);
    fsw_view_t v = view();
    CHECK(v.boot_count == 2);
    CHECK(v.cmd_seq == 5);
    CHECK(v.mode == MODE_DETUMBLE);                      /* 리셋 뒤엔 각속도부터 다시 확인 */
    CHECK(v.met_s >= met_before);
    CHECK(last_event_a(EVT_BOOT) == 2);
    CHECK(M.ev_b[0] == RESET_WATCHDOG);

    M.tx_n = 0;
    seconds(1);
    cmd(5, CMD_KEY, OP_PING, NULL, 0);
    CHECK(ack_status(5) == ACK_DUPLICATE);
    cmd(3, CMD_KEY, OP_PING, NULL, 0);
    CHECK(ack_status(3) == -1);                          /* 리셋 뒤에도 옛 번호는 거부 */
    cmd(6, CMD_KEY, OP_PING, NULL, 0);
    CHECK(ack_status(6) == ACK_OK);

    seconds(RATE_LOW_HOLD_S + 5);
    CHECK(M.burn_starts == 0);                           /* 안테나 재전개 시도 없음 */
    CHECK(view().mode == MODE_NOMINAL);
}

static void test_low_power_safe(void)
{
    boot_to_nominal();
    M.eps.batt_v = 6.5f;                                 /* 셀당 3.25 V */
    seconds(2);
    CHECK(view().mode == MODE_SAFE);
    CHECK(mock_events(EVT_LOW_POWER) == 1);
    CHECK(view().beacon_period_s == BEACON_SAFE_S);
    CHECK(M.mtq[0] == 0.0f && M.mtq[1] == 0.0f && M.mtq[2] == 0.0f);

    M.eps.batt_v = 8.1f;
    seconds(SAFE_MIN_S - 10);
    CHECK(view().mode == MODE_SAFE);                     /* 최소 유지 시간 */
    seconds(20);
    CHECK(view().mode == MODE_DETUMBLE);
    seconds(RATE_LOW_HOLD_S + 5);
    CHECK(view().mode == MODE_NOMINAL);
    CHECK(view().beacon_period_s == BEACON_NOMINAL_S);

    uint8_t one = 1, zero = 0;
    cmd(1, CMD_KEY, OP_SET_MODE, &one, 1);               /* 지상에서 SAFE 고정 */
    seconds(2);
    CHECK(view().mode == MODE_SAFE);
    seconds(SAFE_MIN_S + 10);
    CHECK(view().mode == MODE_SAFE);
    cmd(2, CMD_KEY, OP_SET_MODE, &zero, 1);
    seconds(2);
    CHECK(view().mode == MODE_DETUMBLE);
}

static void test_bootloop(void)
{
    boot_to_nominal();
    for (int i = 0; i < 3; i++) {
        reboot(RESET_WATCHDOG);
        seconds(5);
    }
    CHECK(view().mode != MODE_SAFE);
    reboot(RESET_WATCHDOG);                              /* 10분 안에 네 번째 */
    CHECK(view().mode == MODE_SAFE);
    CHECK(mock_events(EVT_BOOTLOOP) == 1);
}

static void test_nvm_corruption(void)
{
    boot_to_nominal();
    cmd(7, CMD_KEY, OP_PING, NULL, 0);
    seconds(2);
    persist_t a, b;
    memcpy(&a, M.nvm + 0x0000, sizeof a);
    memcpy(&b, M.nvm + 0x0100, sizeof b);
    M.nvm[(a.write_count > b.write_count ? 0x0000 : 0x0100) + 8] ^= 0xFF;   /* 최신 슬롯 손상 */
    reboot(RESET_BROWNOUT);
    CHECK(mock_events(EVT_NVM_FALLBACK) == 1);
    CHECK(view().cmd_seq == 7);
    CHECK(view().mode == MODE_DETUMBLE);
    seconds(10);
    CHECK(M.burn_starts == 0);

    /* 두 슬롯 모두 깨지면 첫 부팅으로 간주 → 다시 45분 침묵 (안전한 쪽) */
    M.nvm[0x0000 + 8] ^= 0xFF;
    M.nvm[0x0100 + 8] ^= 0xFF;
    reboot(RESET_BROWNOUT);
    CHECK(view().mode == MODE_DEPLOY_WAIT);
    CHECK(view().boot_count == 1);
}

static void test_bdot(void)
{
    boot_to_nominal();
    const float w = 0.1f, bx = 2.0e-5f, bz = 1.0e-5f;    /* z축으로 5.7°/s 회전 */
    M.gyro[0] = 0.0f;
    M.gyro[1] = 0.0f;
    M.gyro[2] = w;
    float t = 0.0f, th = 0.0f;
    for (int k = 0; k < 50; k++) {
        th = w * t;
        M.mag[0] = bx * cosf(th);                        /* 기체에서 본 자기장은 반대로 돈다 */
        M.mag[1] = -bx * sinf(th);
        M.mag[2] = bz;
        fsw_tick();
        t += 0.1f;
    }
    const fsw_view_t v = view();
    const float B[3] = {bx * cosf(th), -bx * sinf(th), bz};
    const float tau_z = v.m_cmd[0] * B[1] - v.m_cmd[1] * B[0];
    CHECK(tau_z * w < 0.0f);                             /* 토크가 회전을 늦춘다 */
    for (int i = 0; i < 3; i++) {
        CHECK(fabsf(v.m_cmd[i]) <= MTQ_MAX_AM2 + 1e-6f);
    }
    for (int k = 0; k < 320; k++) {                      /* 3°/s 넘는 상태 30초 → DETUMBLE */
        th = w * t;
        M.mag[0] = bx * cosf(th);
        M.mag[1] = -bx * sinf(th);
        fsw_tick();
        t += 0.1f;
    }
    CHECK(view().mode == MODE_DETUMBLE);
    CHECK(mock_events(EVT_TUMBLING) == 1);
}

static void test_eps(void)
{
    boot_to_nominal();
    M.eps.batt_c = 1.0f;
    seconds(2);
    CHECK(M.rail[RAIL_BATT_HEATER]);
    CHECK(M.charge_en);
    M.eps.batt_c = -1.0f;
    seconds(2);
    CHECK(!M.charge_en);                                 /* 0°C 아래 충전 금지 */
    M.eps.batt_c = 7.0f;
    seconds(2);
    CHECK(!M.rail[RAIL_BATT_HEATER] && M.charge_en);

    /* 쿨롱 카운팅: 1.5 A로 20분 방전 = 0.5 Ah = 3 Ah의 16.7% */
    const float soc0 = view().soc_est;
    M.eps.batt_a = -1.5f;
    M.eps.batt_v = 7.72f;
    seconds(1200);
    const float d = soc0 - view().soc_est;
    CHECK(d > 0.12f && d < 0.18f);

    /* 만충 감지 */
    M.eps.batt_v = 8.28f;
    M.eps.batt_a = 0.05f;
    seconds(62);
    CHECK(view().soc_est > 0.999f);
}

static void run_until_state(uint8_t st, long max_ticks)
{
    for (long i = 0; i < max_ticks && view().pl_state != st; i++) {
        fsw_tick();
    }
}

static void test_payload(void)
{
    boot_to_nominal();
    M.gyro[0] = 0.03f;                                   /* 1.7°/s: 실험 시작 기준(1°/s) 초과 */
    seconds(5);
    uint8_t args[7] = {0, 0, 0, 0, 110, 2, 1};           /* 즉시, 110 V, 왕복 2회, AC */
    M.tx_n = 0;
    cmd(1, CMD_KEY, OP_EXP_SCHEDULE, args, 7);
    CHECK(ack_status(1) == ACK_OK);
    seconds(20);
    CHECK(view().pl_state == PL_ARMED);
    CHECK(!M.rail[RAIL_PAYLOAD]);

    M.gyro[0] = 0.001f;
    M.eps.panel_v = 0.2f;                                /* 일식 중이면 기다린다 */
    seconds(20);
    CHECK(view().pl_state == PL_ARMED);
    M.eps.panel_v = 5.0f;
    cmd(2, CMD_KEY, OP_EXP_SCHEDULE, args, 7);           /* 이미 예약됨 → 거부 */
    CHECK(ack_status(2) == ACK_REJECTED);
    run_until_state(PL_IDLE, 600);
    CHECK(view().pl_state == PL_IDLE);
    CHECK(last_event_a(EVT_PL_DONE) == PLR_OK);
    CHECK(!M.rail[RAIL_PAYLOAD] && !M.hv && M.electrodes == 0);

    M.tx_n = 0;
    uint8_t all = 0xFF;
    cmd(3, CMD_KEY, OP_EXP_DOWNLINK, &all, 1);
    ticks(20);
    bool got_sum = false, got_steps = false;
    for (int i = 0; i < M.tx_n; i++) {
        frame_t f;
        if (proto_decode(M.tx[i], M.txlen[i], SPACECRAFT_ID, NULL, &f) != PROTO_OK || f.type != FT_PAYLOAD) {
            continue;
        }
        if (f.data[0] == PLD_SUMMARY) {
            tm_result_t r;
            CHECK(tm_result_unpack(f.data + 1, f.len - 1u, &r));
            CHECK(r.result == PLR_OK && r.drive_ac == 1 && r.cycles_done == 2);
            CHECK(r.steps_ok == 25 && r.steps_fail == 0 && r.disp_ok == 1);   /* 12칸×2 + 저장조 복귀 1칸 */
            got_sum = true;
        } else if (f.data[0] == PLD_STEPS) {
            CHECK(f.data[4] == 25);
            got_steps = true;
        }
    }
    CHECK(got_sum && got_steps);
}

/* 지난 실험에서 트랙에 남은 액적이 있으면 먼저 저장조로 회수한 뒤 실험한다 */
static void test_payload_leftover(void)
{
    boot_to_nominal();
    M.drop_pos = 5;                                      /* E5에 멈춰 있던 액적 */
    uint8_t args[7] = {0, 0, 0, 0, 110, 2, 1};
    M.tx_n = 0;
    cmd(1, CMD_KEY, OP_EXP_SCHEDULE, args, 7);
    run_until_state(PL_IDLE, 900);
    CHECK(last_event_a(EVT_PL_DONE) == PLR_OK);
    CHECK(M.drop_pos == 0);                              /* 끝나면 트랙이 비어 있다 */
    uint8_t all = 0xFF;
    M.tx_n = 0;
    cmd(2, CMD_KEY, OP_EXP_DOWNLINK, &all, 1);
    ticks(20);
    bool seen = false;
    for (int i = 0; i < M.tx_n; i++) {
        frame_t f;
        tm_result_t r;
        if (proto_decode(M.tx[i], M.txlen[i], SPACECRAFT_ID, NULL, &f) == PROTO_OK && f.type == FT_PAYLOAD &&
            f.data[0] == PLD_SUMMARY && tm_result_unpack(f.data + 1, f.len - 1u, &r)) {
            CHECK(r.steps_ok == 25 && r.disp_ok == 1);       /* 회수 이동은 실험 데이터에서 빠진다 */
            seen = true;
        }
    }
    CHECK(seen);
}

static void test_payload_abort_and_reset(void)
{
    boot_to_nominal();
    uint8_t args[7] = {0, 0, 0, 0, 110, 10, 1};
    cmd(1, CMD_KEY, OP_EXP_SCHEDULE, args, 7);
    run_until_state(PL_TRANSPORT, 600);
    CHECK(view().pl_state == PL_TRANSPORT);
    M.gyro[0] = 0.05f;                                   /* 2.9°/s: 실험 중단 기준(2°/s) 초과 */
    run_until_state(PL_IDLE, 100);
    CHECK(last_event_a(EVT_PL_DONE) == PLR_ABORT_RATE);
    CHECK(!M.hv && !M.rail[RAIL_PAYLOAD]);

    M.gyro[0] = 0.001f;
    seconds(10);
    cmd(2, CMD_KEY, OP_EXP_SCHEDULE, args, 7);
    run_until_state(PL_TRANSPORT, 600);
    reboot(RESET_WATCHDOG);                              /* 실험 도중 리셋 */
    CHECK(view().pl_state == PL_IDLE);
    CHECK(last_event_a(EVT_PL_DONE) == PLR_INTERRUPTED);
    CHECK(!M.hv && !M.rail[RAIL_PAYLOAD]);

    args[0] = 0x10;                                      /* 0x0E10 = 3600초 뒤 */
    args[1] = 0x0E;
    seconds(RATE_LOW_HOLD_S + 5);
    cmd(3, CMD_KEY, OP_EXP_SCHEDULE, args, 7);
    CHECK(view().pl_state == PL_ARMED);
    reboot(RESET_WATCHDOG);
    CHECK(view().pl_state == PL_ARMED);                  /* 예약은 리셋을 견딘다 */
}

int main(void)
{
    printf("DROPSAT-1 비행 소프트웨어 테스트\n");
    RUN(test_crc, "CRC-16 표준 검사값");
    RUN(test_siphash, "SipHash-2-4 참조 벡터 7개");
    RUN(test_frames, "프레임 인코딩/디코딩, 비트 오류·변조·잘못된 키 거부");
    RUN(test_deploy_timeline, "사출 후 30분 전개 금지, 45분 송신 금지");
    RUN(test_antenna_fail, "안테나 전개 5회 실패 시 포기하고 임무 진행");
    RUN(test_burn_cap_across_resets, "가열 중 저전압 리셋이 반복돼도 열선 가열은 총 5회까지");
    RUN(test_reset_command, "RESET 명령: 확인 코드 검사, ACK 먼저 보낸 뒤 리셋");
    RUN(test_commands, "명령 인증, 중복 ACK, 재전송 공격·위조 거부, 인자 검사");
    RUN(test_reset_persistence, "리셋 뒤 상태 유지 (명령 번호, 전개 완료, 부팅 횟수)");
    RUN(test_low_power_safe, "저전력 SAFE 진입·복귀, 지상 명령 SAFE 고정");
    RUN(test_bootloop, "10분 안에 4번 리셋 → SAFE로 부팅");
    RUN(test_nvm_corruption, "FRAM 슬롯 손상 시 백업 슬롯으로 복구");
    RUN(test_bdot, "B-dot 토크 방향, 포화, 텀블링 감지");
    RUN(test_eps, "배터리 히터, 저온 충전 금지, SoC 추정");
    RUN(test_payload, "EWOD 실험 인터록·시퀀스·데이터 다운링크");
    RUN(test_payload_leftover, "트랙에 남은 액적을 먼저 회수하고 실험");
    RUN(test_payload_abort_and_reset, "실험 중단(각속도), 실행 중 리셋, 예약 유지");
    printf("%d개 검사 중 %d개 실패\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
