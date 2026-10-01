/*
 * 비휘발성 저장 (FRAM)
 *
 * 0x0000  상태 슬롯 A ─┐ 번갈아 저장: 쓰는 도중 전원이 나가도
 * 0x0100  상태 슬롯 B ─┘ 다른 슬롯 하나는 항상 온전하다
 * 0x0400  실험 결과 요약 8개 × 64 B
 * 0x0800  실험 원시 데이터 8개 × 256 B
 */
#include <stddef.h>
#include <string.h>

#include "crc16.h"
#include "fsw_internal.h"

#define NVM_SLOT_A        0x0000u
#define NVM_SLOT_B        0x0100u
#define NVM_RESULTS       0x0400u
#define NVM_RESULT_STRIDE 0x40u
#define NVM_RAW           0x0800u
#define NVM_RAW_STRIDE    0x100u

typedef char persist_fits_slot[(sizeof(persist_t) <= 0x100u) ? 1 : -1];

static uint16_t state_crc(const persist_t *p)
{
    return crc16_ccitt((const uint8_t *)p, offsetof(persist_t, crc));
}

static bool slot_read(uint32_t addr, persist_t *p)
{
    if (!hal_nvm_read(addr, p, sizeof *p)) {
        return false;
    }
    if (p->magic != PERSIST_MAGIC || p->version != PERSIST_VERSION) {
        return false;
    }
    return state_crc(p) == p->crc;
}

uint8_t persist_load(persist_t *out)
{
    persist_t a, b;
    const bool va = slot_read(NVM_SLOT_A, &a);
    const bool vb = slot_read(NVM_SLOT_B, &b);
    if (va && vb) {
        *out = (a.write_count >= b.write_count) ? a : b;
    } else if (va) {
        *out = a;
    } else if (vb) {
        *out = b;
    }
    return (uint8_t)((va ? 1u : 0u) | (vb ? 2u : 0u));
}

void persist_save(persist_t *p)
{
    p->write_count++;
    p->crc = state_crc(p);
    (void)hal_nvm_write((p->write_count & 1u) ? NVM_SLOT_A : NVM_SLOT_B, p, sizeof *p);
}

void persist_store_result(const tm_result_t *r, const uint16_t *raw, uint8_t n_raw)
{
    const uint32_t slot = (uint32_t)(r->exp_id % PL_RESULT_SLOTS);
    uint8_t buf[NVM_RAW_STRIDE];

    /* 요약: [길이][데이터][CRC] */
    const size_t n = tm_result_pack(r, buf + 1, NVM_RESULT_STRIDE - 3u);
    buf[0] = (uint8_t)n;
    uint16_t c = crc16_ccitt(buf, n + 1u);
    buf[n + 1u] = (uint8_t)(c >> 8);
    buf[n + 2u] = (uint8_t)(c & 0xFFu);
    (void)hal_nvm_write(NVM_RESULTS + slot * NVM_RESULT_STRIDE, buf, n + 3u);

    /* 원시 데이터: [실험번호 2][개수 1][이동시간 ms × n][CRC] */
    if (n_raw > PL_RAW_MAX) {
        n_raw = PL_RAW_MAX;
    }
    wbuf_t w;
    wb_init(&w, buf, sizeof buf);
    wb_u16(&w, r->exp_id);
    wb_u8(&w, n_raw);
    for (uint8_t i = 0; i < n_raw; i++) {
        wb_u16(&w, raw[i]);
    }
    c = crc16_ccitt(buf, w.n);
    wb_u8(&w, (uint8_t)(c >> 8));
    wb_u8(&w, (uint8_t)(c & 0xFFu));
    if (!w.ovf) {
        (void)hal_nvm_write(NVM_RAW + slot * NVM_RAW_STRIDE, buf, w.n);
    }
}

bool persist_load_result(uint8_t slot, tm_result_t *r)
{
    uint8_t buf[NVM_RESULT_STRIDE];
    if (slot >= PL_RESULT_SLOTS ||
        !hal_nvm_read(NVM_RESULTS + (uint32_t)slot * NVM_RESULT_STRIDE, buf, sizeof buf)) {
        return false;
    }
    const size_t n = buf[0];
    if (n == 0u || n > NVM_RESULT_STRIDE - 3u) {
        return false;
    }
    const uint16_t c = (uint16_t)(((uint16_t)buf[n + 1u] << 8) | buf[n + 2u]);
    if (crc16_ccitt(buf, n + 1u) != c) {
        return false;
    }
    return tm_result_unpack(buf + 1, n, r);
}

bool persist_load_raw(uint8_t slot, uint16_t exp_id, uint16_t *raw, uint8_t *n_raw)
{
    uint8_t buf[NVM_RAW_STRIDE];
    if (slot >= PL_RESULT_SLOTS ||
        !hal_nvm_read(NVM_RAW + (uint32_t)slot * NVM_RAW_STRIDE, buf, sizeof buf)) {
        return false;
    }
    rbuf_t r;
    rb_init(&r, buf, sizeof buf);
    const uint16_t id = rb_u16(&r);
    const uint8_t n = rb_u8(&r);
    if (id != exp_id || n > PL_RAW_MAX) {
        return false;
    }
    const size_t body = 3u + 2u * (size_t)n;
    const uint16_t c = (uint16_t)(((uint16_t)buf[body] << 8) | buf[body + 1u]);
    if (crc16_ccitt(buf, body) != c) {
        return false;
    }
    for (uint8_t i = 0; i < n; i++) {
        raw[i] = rb_u16(&r);
    }
    *n_raw = n;
    return true;
}
