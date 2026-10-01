/*
 * 통신: 비콘 송신, 명령 수신·인증·실행, 응답(ACK)
 *
 * 명령 보안
 *   1) CRC   : 전송 중 비트 오류 걸러냄
 *   2) MAC   : SipHash-2-4. 키가 없으면 유효한 명령을 만들 수 없다
 *   3) SEQ   : 마지막으로 수락한 번호보다 커야 실행. 녹음해 둔 명령을 다시 보내는 공격 차단.
 *              번호는 실행 "전에" FRAM에 저장하므로 리셋 뒤에도 재사용이 안 된다.
 *   같은 번호가 다시 오면(지상국이 ACK를 못 받아 재전송한 경우) 실행하지 않고 ACK만 다시 보낸다.
 *   인증 실패 프레임에는 응답하지 않는다 (공격자에게 정보를 주지 않기 위해). 카운터만 올린다.
 */
#include <string.h>

#include "fsw_internal.h"
#include "keys.h"

#define C g_fsw.comms
#define G g_fsw

void comms_init(void)
{
    memset(&C, 0, sizeof C);
    C.beacon_period = G.nv.beacon_period_s ? G.nv.beacon_period_s : (uint16_t)BEACON_NOMINAL_S;
}

static bool tx_allowed(void)
{
    return G.met_s >= RF_SILENCE_S && G.nv.deploy_done;
}

void comms_set_beacon_period(uint16_t s)
{
    C.beacon_period = s;
    if (C.next_beacon > G.met_s + s) {
        C.next_beacon = G.met_s + s;
    }
}

bool comms_send(uint8_t type, const uint8_t *data, uint8_t len)
{
    if (C.count >= TXQ_DEPTH) {
        return false;
    }
    frame_t f;
    f.type = type;
    f.scid = SPACECRAFT_ID;
    f.seq = C.tm_seq;
    f.len = len;
    memcpy(f.data, data, len);
    const uint8_t idx = (uint8_t)((C.head + C.count) % TXQ_DEPTH);
    const size_t n = proto_encode(&f, NULL, C.q[idx], PROTO_MAX_FRAME);
    if (n == 0u) {
        return false;
    }
    C.qlen[idx] = (uint16_t)n;
    C.count++;
    C.tm_seq++;
    return true;
}

/* ---------------- 비콘 ---------------- */

static uint16_t sat_u16(float x)
{
    return x <= 0.0f ? 0u : (x >= 65535.0f ? 65535u : (uint16_t)(x + 0.5f));
}

static int16_t sat_i16(float x)
{
    if (x >= 32767.0f) {
        return 32767;
    }
    if (x <= -32768.0f) {
        return -32768;
    }
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

static int8_t sat_i8(float x)
{
    if (x >= 127.0f) {
        return 127;
    }
    if (x <= -128.0f) {
        return -128;
    }
    return (int8_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

static void send_beacon(void)
{
    tm_beacon_t b;
    memset(&b, 0, sizeof b);
    const hal_eps_t *e = &G.eps.raw;
    b.met_s = G.met_s;
    b.utc_s = fsw_utc();
    b.boot_count = G.nv.boot_count;
    b.mode = G.mode;
    uint8_t fl = 0;
    if (G.nv.ant_ok) { fl |= BF_ANT_OK; }
    if (eps_sunlit()) { fl |= BF_SUNLIT; }
    if (G.eps.heater) { fl |= BF_BATT_HEATER; }
    if (G.eps.charge_inhibit) { fl |= BF_CHG_INHIBIT; }
    if (G.nv.safe_latched) { fl |= BF_SAFE_LATCH; }
    if (G.adcs.gyro_ok) { fl |= BF_GYRO_OK; }
    if (G.nv.utc_offset != 0u) { fl |= BF_TIME_SYNC; }
    if (G.adcs.mag_ok) { fl |= BF_MAG_OK; }
    b.flags = fl;
    b.batt_mv = sat_u16(e->batt_v * 1000.0f);
    b.batt_ma = sat_i16(e->batt_a * 1000.0f);
    b.soc_pct = (uint8_t)sat_u16(G.eps.soc * 100.0f);
    b.batt_c = sat_i8(e->batt_c);
    b.pl_c = sat_i8(hal_payload_temp_c());
    b.solar_mw = sat_u16(e->solar_w * 1000.0f);
    b.rate_cdps = G.adcs.rate_valid ? sat_u16(G.adcs.rate / DEG2RAD * 100.0f) : 0xFFFFu;
    b.rx_ok = C.rx_ok;
    b.rx_bad = C.rx_bad;
    b.auth_fail = C.auth_fail;
    b.replay = C.replay;
    b.last_seq = G.nv.cmd_seq;
    b.exp_count = G.nv.exp_count;
    b.pl_state = G.pl.state;
    b.reset_cause = G.reset_cause;
    b.last_event = G.last_event;

    uint8_t buf[TM_BEACON_SIZE];
    const size_t n = tm_beacon_pack(&b, buf, sizeof buf);
    if (n == TM_BEACON_SIZE) {
        (void)comms_send(FT_BEACON, buf, (uint8_t)n);
    }
}

/* ---------------- 명령 ---------------- */

static void send_ack(uint32_t seq, uint8_t op, uint8_t status)
{
    uint8_t d[6];
    wbuf_t w;
    wb_init(&w, d, sizeof d);
    wb_u8(&w, op);
    wb_u8(&w, status);
    wb_u32(&w, seq);
    (void)comms_send(FT_ACK, d, (uint8_t)w.n);
}

static void send_events(uint8_t n)
{
    tm_event_t ev[16];
    uint8_t buf[16u * TM_EVENT_SIZE];
    if (n == 0u || n > 16u) {
        n = 16u;
    }
    const uint8_t got = fsw_events_latest(ev, n);
    size_t len = 0;
    for (uint8_t i = 0; i < got; i++) {
        len += tm_event_pack(&ev[i], buf + len, sizeof buf - len);
    }
    (void)comms_send(FT_EVENTS, buf, (uint8_t)len);
}

static uint8_t exec_cmd(const frame_t *f)
{
    rbuf_t r;
    rb_init(&r, f->data, f->len);
    const uint8_t op = rb_u8(&r);
    switch (op) {
    case OP_PING:
        return ACK_OK;

    case OP_SET_TIME: {
        const uint32_t utc = rb_u32(&r);
        if (r.err || utc <= G.met_s) {
            return ACK_BAD_ARGS;
        }
        G.nv.utc_offset = utc - G.met_s;
        fsw_persist();
        fsw_event(EVT_TIME_SET, 0, 0);
        return ACK_OK;
    }

    case OP_SET_MODE: {
        const uint8_t latch = rb_u8(&r);
        if (r.err || latch > 1u) {
            return ACK_BAD_ARGS;
        }
        G.nv.safe_latched = latch;       /* 실제 전환은 모드 관리자가 다음 1초 주기에 */
        fsw_persist();
        return ACK_OK;
    }

    case OP_SET_BEACON: {
        const uint16_t p = rb_u16(&r);
        if (r.err || p < BEACON_MIN_S || p > BEACON_MAX_S) {
            return ACK_BAD_ARGS;
        }
        G.nv.beacon_period_s = p;
        if (G.mode != MODE_SAFE) {
            comms_set_beacon_period(p);
        }
        fsw_persist();
        return ACK_OK;
    }

    case OP_EXP_SCHEDULE: {
        const uint32_t delay = rb_u32(&r);
        const uint8_t volts = rb_u8(&r);
        const uint8_t cycles = rb_u8(&r);
        const uint8_t ac = rb_u8(&r);
        if (r.err) {
            return ACK_BAD_ARGS;
        }
        return payload_schedule(delay, volts, cycles, ac);
    }

    case OP_EXP_ABORT:
        return payload_abort(PLR_ABORT_CMD);

    case OP_EXP_DOWNLINK: {
        const uint8_t id = rb_u8(&r);
        if (r.err) {
            return ACK_BAD_ARGS;
        }
        payload_downlink(id);
        return ACK_OK;
    }

    case OP_REQ_EVENTS: {
        const uint8_t n = rb_u8(&r);
        send_events(r.err ? 16u : n);
        return ACK_OK;
    }

    case OP_RESET: {
        const uint32_t magic = rb_u32(&r);
        if (r.err || magic != RESET_MAGIC) {
            return ACK_BAD_ARGS;
        }
        C.reset_pending = true;          /* ACK를 내보낸 뒤 리셋 */
        return ACK_OK;
    }

    default:
        return ACK_UNKNOWN_OP;
    }
}

static void handle_rx(const uint8_t *buf, size_t n)
{
    frame_t f;
    const proto_err_t e = proto_decode(buf, n, SPACECRAFT_ID, CMD_KEY, &f);
    if (e == PROTO_ERR_MAC) {
        C.auth_fail++;
        fsw_event(EVT_AUTH_FAIL, (int32_t)(f.seq & 0x7FFFu), 0);
        return;
    }
    if (e != PROTO_OK || f.type != FT_CMD || f.len == 0u) {
        C.rx_bad++;
        return;
    }
    const uint8_t op = f.data[0];
    if (f.seq <= G.nv.cmd_seq) {
        if (f.seq == G.nv.cmd_seq) {
            send_ack(f.seq, op, ACK_DUPLICATE);   /* 이미 실행함: ACK만 다시 */
        } else {
            C.replay++;
            fsw_event(EVT_REPLAY, (int32_t)(f.seq & 0x7FFFu), (int32_t)(G.nv.cmd_seq & 0x7FFFu));
        }
        return;
    }
    G.nv.cmd_seq = f.seq;
    fsw_persist();                                /* 실행 전에 저장 */
    C.rx_ok++;
    const uint8_t st = exec_cmd(&f);
    if (st == ACK_OK) {
        fsw_event(EVT_CMD, op, (int32_t)(f.seq & 0x7FFFu));
    } else {
        fsw_event(EVT_CMD_REJECT, op, st);
    }
    send_ack(f.seq, op, st);
}

/* ---------------- 주기 처리 (10 Hz) ---------------- */

void comms_tick(uint32_t sub)
{
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = hal_radio_rx(buf, sizeof buf);
    if (n > 0u) {
        handle_rx(buf, n);
    }
    if (!tx_allowed()) {
        return;
    }
    if (!C.rf_on) {
        C.rf_on = true;
        C.next_beacon = G.met_s;
        fsw_event(EVT_RF_ON, 0, 0);
    }
    if (sub == 0u && G.met_s >= C.next_beacon) {
        send_beacon();
        C.next_beacon = G.met_s + C.beacon_period;
    }
    if (C.count > 0u) {
        (void)hal_radio_tx(C.q[C.head], C.qlen[C.head]);   /* 틱당 한 프레임 (9600 bps) */
        C.head = (uint8_t)((C.head + 1u) % TXQ_DEPTH);
        C.count--;
    } else if (C.reset_pending) {
        hal_reset();
    }
}
