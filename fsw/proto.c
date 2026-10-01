#include <string.h>

#include "crc16.h"
#include "proto.h"
#include "siphash.h"

/* ---------------- 바이트 버퍼 ---------------- */

void wb_init(wbuf_t *w, uint8_t *p, size_t cap)
{
    w->p = p;
    w->cap = cap;
    w->n = 0;
    w->ovf = false;
}

void wb_u8(wbuf_t *w, uint8_t v)
{
    if (w->n < w->cap) {
        w->p[w->n++] = v;
    } else {
        w->ovf = true;
    }
}

void wb_u16(wbuf_t *w, uint16_t v)
{
    wb_u8(w, (uint8_t)(v & 0xFFu));
    wb_u8(w, (uint8_t)(v >> 8));
}

void wb_u32(wbuf_t *w, uint32_t v)
{
    wb_u16(w, (uint16_t)(v & 0xFFFFu));
    wb_u16(w, (uint16_t)(v >> 16));
}

void rb_init(rbuf_t *r, const uint8_t *p, size_t n)
{
    r->p = p;
    r->n = n;
    r->pos = 0;
    r->err = false;
}

uint8_t rb_u8(rbuf_t *r)
{
    if (r->pos < r->n) {
        return r->p[r->pos++];
    }
    r->err = true;
    return 0;
}

uint16_t rb_u16(rbuf_t *r)
{
    const uint16_t lo = rb_u8(r);
    const uint16_t hi = rb_u8(r);
    return (uint16_t)(lo | (uint16_t)(hi << 8));
}

uint32_t rb_u32(rbuf_t *r)
{
    const uint32_t lo = rb_u16(r);
    const uint32_t hi = rb_u16(r);
    return lo | (hi << 16);
}

int16_t u16_to_s16(uint16_t v)
{
    return (v & 0x8000u) ? (int16_t)((int32_t)v - 65536) : (int16_t)v;
}

int8_t u8_to_s8(uint8_t v)
{
    return (v & 0x80u) ? (int8_t)((int32_t)v - 256) : (int8_t)v;
}

/* ---------------- 프레임 ---------------- */

static bool has_mac(uint8_t type)
{
    return type == FT_CMD;
}

size_t proto_encode(const frame_t *f, const uint8_t *key, uint8_t *out, size_t cap)
{
    const bool mac = has_mac(f->type);
    const size_t total = PROTO_HDR_LEN + f->len + (mac ? PROTO_MAC_LEN : 0u) + PROTO_CRC_LEN;
    if (f->len > PROTO_MAX_PAYLOAD || total > cap || (mac && key == NULL)) {
        return 0;
    }
    out[0] = PROTO_SYNC0;
    out[1] = PROTO_SYNC1;
    out[2] = (uint8_t)((PROTO_VERSION << 4) | (f->type & 0x0Fu));
    out[3] = (uint8_t)(f->scid & 0xFFu);
    out[4] = (uint8_t)(f->scid >> 8);
    for (unsigned i = 0; i < 4u; i++) {
        out[5u + i] = (uint8_t)(f->seq >> (8u * i));
    }
    out[9] = f->len;
    memcpy(out + PROTO_HDR_LEN, f->data, f->len);

    size_t p = PROTO_HDR_LEN + f->len;
    if (mac) {
        const uint64_t tag = siphash24(key, out + 2, p - 2u);
        for (unsigned i = 0; i < PROTO_MAC_LEN; i++) {
            out[p + i] = (uint8_t)(tag >> (8u * i));
        }
        p += PROTO_MAC_LEN;
    }
    const uint16_t crc = crc16_ccitt(out + 2, p - 2u);
    out[p] = (uint8_t)(crc >> 8);
    out[p + 1u] = (uint8_t)(crc & 0xFFu);
    return p + PROTO_CRC_LEN;
}

proto_err_t proto_decode(const uint8_t *buf, size_t n, uint16_t scid, const uint8_t *key, frame_t *f)
{
    if (n < PROTO_HDR_LEN + PROTO_CRC_LEN) {
        return PROTO_ERR_SHORT;
    }
    if (buf[0] != PROTO_SYNC0 || buf[1] != PROTO_SYNC1) {
        return PROTO_ERR_SYNC;
    }
    const uint8_t type = (uint8_t)(buf[2] & 0x0Fu);
    const uint8_t len = buf[9];
    const bool mac = has_mac(type);
    const size_t expect = PROTO_HDR_LEN + len + (mac ? PROTO_MAC_LEN : 0u) + PROTO_CRC_LEN;
    if (len > PROTO_MAX_PAYLOAD || expect != n) {
        return PROTO_ERR_LEN;
    }
    const uint16_t crc_rx = (uint16_t)(((uint16_t)buf[n - 2u] << 8) | buf[n - 1u]);
    if (crc16_ccitt(buf + 2, n - 4u) != crc_rx) {
        return PROTO_ERR_CRC;
    }

    /* 헤더는 MAC 검증 전에 채워 둔다 (거부 이벤트에 seq를 남기기 위해) */
    f->type = type;
    f->scid = (uint16_t)(buf[3] | (uint16_t)(buf[4] << 8));
    f->seq = 0;
    for (unsigned i = 0; i < 4u; i++) {
        f->seq |= (uint32_t)buf[5u + i] << (8u * i);
    }
    f->len = len;

    if ((buf[2] >> 4) != PROTO_VERSION) {
        return PROTO_ERR_VERSION;
    }
    if (f->scid != scid) {
        return PROTO_ERR_SCID;
    }
    if (mac) {
        if (key == NULL) {
            return PROTO_ERR_MAC;
        }
        const size_t body = PROTO_HDR_LEN + len;
        const uint64_t tag = siphash24(key, buf + 2, body - 2u);
        uint8_t diff = 0;   /* 상수 시간 비교 */
        for (unsigned i = 0; i < PROTO_MAC_LEN; i++) {
            diff |= (uint8_t)(buf[body + i] ^ (uint8_t)(tag >> (8u * i)));
        }
        if (diff != 0u) {
            return PROTO_ERR_MAC;
        }
    }
    memcpy(f->data, buf + PROTO_HDR_LEN, len);
    return PROTO_OK;
}

/* ---------------- 텔레메트리 직렬화 ---------------- */

size_t tm_beacon_pack(const tm_beacon_t *b, uint8_t *out, size_t cap)
{
    wbuf_t w;
    wb_init(&w, out, cap);
    wb_u32(&w, b->met_s);
    wb_u32(&w, b->utc_s);
    wb_u16(&w, b->boot_count);
    wb_u8(&w, b->mode);
    wb_u8(&w, b->flags);
    wb_u16(&w, b->batt_mv);
    wb_u16(&w, (uint16_t)b->batt_ma);
    wb_u8(&w, b->soc_pct);
    wb_u8(&w, (uint8_t)b->batt_c);
    wb_u8(&w, (uint8_t)b->pl_c);
    wb_u16(&w, b->solar_mw);
    wb_u16(&w, b->rate_cdps);
    wb_u16(&w, b->rx_ok);
    wb_u16(&w, b->rx_bad);
    wb_u16(&w, b->auth_fail);
    wb_u16(&w, b->replay);
    wb_u32(&w, b->last_seq);
    wb_u16(&w, b->exp_count);
    wb_u8(&w, b->pl_state);
    wb_u8(&w, b->reset_cause);
    wb_u16(&w, b->last_event);
    return w.ovf ? 0u : w.n;
}

bool tm_beacon_unpack(const uint8_t *p, size_t n, tm_beacon_t *b)
{
    rbuf_t r;
    rb_init(&r, p, n);
    b->met_s = rb_u32(&r);
    b->utc_s = rb_u32(&r);
    b->boot_count = rb_u16(&r);
    b->mode = rb_u8(&r);
    b->flags = rb_u8(&r);
    b->batt_mv = rb_u16(&r);
    b->batt_ma = u16_to_s16(rb_u16(&r));
    b->soc_pct = rb_u8(&r);
    b->batt_c = u8_to_s8(rb_u8(&r));
    b->pl_c = u8_to_s8(rb_u8(&r));
    b->solar_mw = rb_u16(&r);
    b->rate_cdps = rb_u16(&r);
    b->rx_ok = rb_u16(&r);
    b->rx_bad = rb_u16(&r);
    b->auth_fail = rb_u16(&r);
    b->replay = rb_u16(&r);
    b->last_seq = rb_u32(&r);
    b->exp_count = rb_u16(&r);
    b->pl_state = rb_u8(&r);
    b->reset_cause = rb_u8(&r);
    b->last_event = rb_u16(&r);
    return !r.err;
}

size_t tm_result_pack(const tm_result_t *x, uint8_t *out, size_t cap)
{
    wbuf_t w;
    wb_init(&w, out, cap);
    wb_u16(&w, x->exp_id);
    wb_u32(&w, x->start_met);
    wb_u8(&w, x->drive_ac);
    wb_u8(&w, x->volts);
    wb_u8(&w, x->cycles_plan);
    wb_u8(&w, x->cycles_done);
    wb_u16(&w, x->steps_ok);
    wb_u16(&w, x->steps_retry);
    wb_u16(&w, x->steps_fail);
    wb_u16(&w, x->step_ms_mean);
    wb_u16(&w, x->step_ms_max);
    wb_u16(&w, x->leak_na_start);
    wb_u16(&w, x->leak_na_end);
    wb_u8(&w, (uint8_t)x->chip_c);
    wb_u8(&w, x->result);
    wb_u8(&w, x->disp_ok);
    wb_u8(&w, x->disp_fail);
    wb_u16(&w, x->duration_s);
    return w.ovf ? 0u : w.n;
}

bool tm_result_unpack(const uint8_t *p, size_t n, tm_result_t *x)
{
    rbuf_t r;
    rb_init(&r, p, n);
    x->exp_id = rb_u16(&r);
    x->start_met = rb_u32(&r);
    x->drive_ac = rb_u8(&r);
    x->volts = rb_u8(&r);
    x->cycles_plan = rb_u8(&r);
    x->cycles_done = rb_u8(&r);
    x->steps_ok = rb_u16(&r);
    x->steps_retry = rb_u16(&r);
    x->steps_fail = rb_u16(&r);
    x->step_ms_mean = rb_u16(&r);
    x->step_ms_max = rb_u16(&r);
    x->leak_na_start = rb_u16(&r);
    x->leak_na_end = rb_u16(&r);
    x->chip_c = u8_to_s8(rb_u8(&r));
    x->result = rb_u8(&r);
    x->disp_ok = rb_u8(&r);
    x->disp_fail = rb_u8(&r);
    x->duration_s = rb_u16(&r);
    return !r.err;
}

size_t tm_event_pack(const tm_event_t *e, uint8_t *out, size_t cap)
{
    wbuf_t w;
    wb_init(&w, out, cap);
    wb_u32(&w, e->met_s);
    wb_u16(&w, e->id);
    wb_u16(&w, (uint16_t)e->a);
    wb_u16(&w, (uint16_t)e->b);
    return w.ovf ? 0u : w.n;
}

bool tm_event_unpack(const uint8_t *p, size_t n, tm_event_t *e)
{
    rbuf_t r;
    rb_init(&r, p, n);
    e->met_s = rb_u32(&r);
    e->id = rb_u16(&r);
    e->a = u16_to_s16(rb_u16(&r));
    e->b = u16_to_s16(rb_u16(&r));
    return !r.err;
}

/* ---------------- 이름 ---------------- */

const char *proto_mode_name(uint8_t m)
{
    static const char *const names[MODE_COUNT] = {"DEPLOY_WAIT", "DETUMBLE", "NOMINAL", "SAFE"};
    return m < MODE_COUNT ? names[m] : "BOOT";
}

const char *proto_op_name(uint8_t op)
{
    switch (op) {
    case OP_PING: return "PING";
    case OP_SET_TIME: return "SET_TIME";
    case OP_SET_MODE: return "SET_MODE";
    case OP_SET_BEACON: return "SET_BEACON";
    case OP_EXP_SCHEDULE: return "EXP_SCHEDULE";
    case OP_EXP_ABORT: return "EXP_ABORT";
    case OP_EXP_DOWNLINK: return "EXP_DOWNLINK";
    case OP_REQ_EVENTS: return "REQ_EVENTS";
    case OP_RESET: return "RESET";
    default: return "UNKNOWN";
    }
}

const char *proto_ack_name(uint8_t s)
{
    static const char *const names[] = {"OK", "DUPLICATE", "BAD_ARGS", "REJECTED", "UNKNOWN_OP"};
    return s < 5u ? names[s] : "?";
}

const char *proto_plr_name(uint8_t r)
{
    static const char *const names[PLR_COUNT] = {
        "OK", "ABORT_POWER", "ABORT_RATE", "ABORT_TEMP", "ABORT_LEAK", "ABORT_CMD",
        "ABORT_MODE", "PREHEAT_TIMEOUT", "DISPENSE_FAIL", "STUCK", "TIMEOUT", "EXPIRED",
        "INTERRUPTED", "TRACK_BLOCKED"};
    return r < PLR_COUNT ? names[r] : "?";
}

const char *proto_pl_state_name(uint8_t s)
{
    static const char *const names[] = {"IDLE", "ARMED", "PREHEAT", "HVCHECK", "DISPENSE", "TRANSPORT", "RETURN"};
    return s < 7u ? names[s] : "?";
}
