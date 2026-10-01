#include <string.h>

#include "mock_hal.h"

mock_t M;

void mock_power_cycle(bool wipe_nvm)
{
    uint8_t keep[sizeof M.nvm];
    memcpy(keep, M.nvm, sizeof keep);
    memset(&M, 0, sizeof M);
    if (!wipe_nvm) {
        memcpy(M.nvm, keep, sizeof keep);
    }
    M.reset_cause = RESET_POWER_ON;
    mock_set_nominal();
}

void mock_set_nominal(void)
{
    M.mag[0] = 2.0e-5f;
    M.mag[1] = 1.0e-5f;
    M.mag[2] = -3.0e-5f;
    M.mag_ok = true;
    M.gyro[0] = 0.001f;
    M.gyro[1] = -0.001f;
    M.gyro[2] = 0.001f;
    M.gyro_ok = true;
    M.eps.batt_v = 7.90f;          /* 약 3.95 V/셀 → SoC 80% 근처 */
    M.eps.batt_a = 0.05f;
    M.eps.batt_c = 15.0f;
    M.eps.solar_w = 3.0f;
    M.eps.panel_v = 5.0f;
    M.pl_temp = 22.0f;
    M.leak_ua = 0.002f;
}

void mock_push_rx(const uint8_t *buf, size_t n)
{
    const int idx = (M.rx_head + M.rx_n) % 8;
    memcpy(M.rx[idx], buf, n);
    M.rxlen[idx] = n;
    M.rx_n++;
}

int mock_events(uint16_t id)
{
    int c = 0;
    for (int i = 0; i < M.ev_n; i++) {
        c += M.ev_id[i] == id;
    }
    return c;
}

/* ---------------- hal.h 구현 ---------------- */

hal_reset_cause_t hal_reset_cause(void) { return M.reset_cause; }
void hal_wdt_kick(void) {}
void hal_reset(void) { M.reset_requested = true; }

bool hal_mag_read(float b[3])
{
    if (!M.rail[RAIL_ADCS] || !M.mag_ok) {
        return false;
    }
    memcpy(b, M.mag, sizeof M.mag);
    return true;
}

bool hal_gyro_read(float w[3])
{
    if (!M.rail[RAIL_ADCS] || !M.gyro_ok) {
        return false;
    }
    memcpy(w, M.gyro, sizeof M.gyro);
    return true;
}

void hal_mtq_set(const float m[3]) { memcpy(M.mtq, m, sizeof M.mtq); }

bool hal_eps_read(hal_eps_t *o)
{
    *o = M.eps;
    return true;
}

void hal_rail_set(hal_rail_t r, bool on) { M.rail[r] = on; }
void hal_charge_enable(bool on) { M.charge_en = on; }

void hal_burnwire(bool on)
{
    if (on && !M.burnwire) {
        M.burn_starts++;
        if (M.ant_deploy_on_burn) {
            M.ant_deployed = true;
        }
    }
    M.burnwire = on;
}

bool hal_antenna_deployed(void) { return M.ant_deployed; }

size_t hal_radio_rx(uint8_t *buf, size_t cap)
{
    if (M.rx_n == 0) {
        return 0;
    }
    size_t n = M.rxlen[M.rx_head];
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, M.rx[M.rx_head], n);
    M.rx_head = (M.rx_head + 1) % 8;
    M.rx_n--;
    return n;
}

bool hal_radio_tx(const uint8_t *buf, size_t len)
{
    if (M.tx_n < MOCK_TXMAX) {
        memcpy(M.tx[M.tx_n], buf, len);
        M.txlen[M.tx_n] = len;
        M.tx_n++;
    }
    return true;
}

bool hal_nvm_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > sizeof M.nvm) {
        return false;
    }
    memcpy(buf, M.nvm + addr, len);
    return true;
}

bool hal_nvm_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr + len > sizeof M.nvm) {
        return false;
    }
    memcpy(M.nvm + addr, buf, len);
    return true;
}

void hal_ewod_hv(bool on, float vrms, bool ac)
{
    M.hv = on;
    M.hv_v = vrms;
    M.hv_ac = ac;
}

/* 이상적인 칩: 액적은 켜진 "이웃" 전극으로 즉시 옮겨 간다 */
void hal_ewod_electrodes(uint16_t mask)
{
    M.electrodes = mask;
    if (!M.hv) {
        return;
    }
    if (M.drop_pos == 0) {
        if ((mask & 0x7u) == 0x7u) {
            M.finger = true;                                /* 저장조에서 E1·E2로 액체가 뻗음 */
        } else if (M.finger && (mask & 0x7u) == 0x5u) {
            M.drop_pos = 2;                                 /* E1을 끊으면 E2에 액적 하나 */
            M.finger = false;
        } else {
            M.finger = false;
        }
        return;
    }
    const int p = M.drop_pos;
    const bool here = (mask >> p) & 1u;
    if (p == 1 && (mask & 1u) && !here) {
        M.drop_pos = 0;                                     /* 저장조와 합쳐짐 */
    } else if (!here && p < 9 && ((mask >> (p + 1)) & 1u)) {
        M.drop_pos = p + 1;
    } else if (!here && p > 1 && ((mask >> (p - 1)) & 1u)) {
        M.drop_pos = p - 1;
    }
}

float hal_ewod_cap_pf(uint8_t e)
{
    if (!M.rail[RAIL_PAYLOAD]) {
        return 6.0f;
    }
    if (e == 0u) {
        return 96.0f;
    }
    if (M.finger) {
        return (e == 1u || e == 2u) ? 48.0f : 6.0f;
    }
    return (int)e == M.drop_pos ? 48.0f : 6.0f;
}

float hal_ewod_leak_ua(void) { return M.hv ? M.leak_ua : 0.0f; }
float hal_payload_temp_c(void) { return M.pl_temp; }
void hal_payload_heater(bool on) { M.pl_heater = on; }

void hal_event_hook(uint32_t met_s, uint16_t id, int32_t a, int32_t b)
{
    (void)met_s;
    if (M.ev_n < 1024) {
        M.ev_id[M.ev_n] = id;
        M.ev_a[M.ev_n] = a;
        M.ev_b[M.ev_n] = b;
        M.ev_n++;
    }
}
