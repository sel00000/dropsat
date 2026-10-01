/*
 * 시뮬레이터용 HAL 구현
 * 비행 소프트웨어가 hal_*()을 부르면 시뮬레이터 상태(S)를 읽거나 바꾼다.
 * 센서에는 바이어스와 잡음을 넣는다.
 */
#include <string.h>

#include "sim.h"

hal_reset_cause_t hal_reset_cause(void) { return S.reset_cause; }
void hal_wdt_kick(void) { S.wdt_last = S.t; }
void hal_reset(void) { S.reset_req = true; }

bool hal_mag_read(float b[3])
{
    if (!S.rail[RAIL_ADCS]) {
        return false;
    }
    v3 m = v3_add(S.b_body, S.mag_bias);
    m = v3_add(m, v3_scale(S.m_applied, 4.0e-6));   /* 토커가 켜져 있으면 측정에 섞인다 */
    b[0] = (float)(m.x + 60e-9 * rng_n());
    b[1] = (float)(m.y + 60e-9 * rng_n());
    b[2] = (float)(m.z + 60e-9 * rng_n());
    return true;
}

bool hal_gyro_read(float w[3])
{
    if (!S.rail[RAIL_ADCS]) {
        return false;
    }
    const double sd = 0.03 * D2R;
    w[0] = (float)(S.w.x + S.gyro_bias.x + sd * rng_n());
    w[1] = (float)(S.w.y + S.gyro_bias.y + sd * rng_n());
    w[2] = (float)(S.w.z + S.gyro_bias.z + sd * rng_n());
    return true;
}

void hal_mtq_set(const float m[3]) { S.m_cmd = v3_(m[0], m[1], m[2]); }

bool hal_eps_read(hal_eps_t *o)
{
    o->batt_v = (float)(S.v_batt + 0.003 * rng_n());
    o->batt_a = (float)(S.i_batt + S.ibat_bias + 0.005 * rng_n());
    o->batt_c = (float)(S.Tb + 0.1 * rng_n());
    o->solar_w = (float)S.p_used;
    o->panel_v = (float)S.panel_v;
    return true;
}

void hal_rail_set(hal_rail_t r, bool on)
{
    if ((int)r >= 0 && r < RAIL_COUNT) {
        S.rail[r] = on;
    }
}

void hal_charge_enable(bool on) { S.charge_en = on; }

void hal_burnwire(bool on)
{
    if (on && S.first_burn_t < 0.0) {
        S.first_burn_t = S.t;
    }
    S.burnwire = on;
}

bool hal_antenna_deployed(void) { return S.ant_deployed; }

size_t hal_radio_rx(uint8_t *buf, size_t cap)
{
    if (S.rx_n == 0) {
        return 0;
    }
    size_t n = S.rxlen[S.rx_head];
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, S.rxq[S.rx_head], n);
    S.rx_head = (S.rx_head + 1) % RXQ_N;
    S.rx_n--;
    return n;
}

void simhal_rx_push(const uint8_t *buf, size_t n)
{
    if (!S.cpu_up || S.cpu_hung || S.rx_n >= RXQ_N || n > PROTO_MAX_FRAME) {
        return;
    }
    const int idx = (S.rx_head + S.rx_n) % RXQ_N;
    memcpy(S.rxq[idx], buf, n);
    S.rxlen[idx] = n;
    S.rx_n++;
}

bool hal_radio_tx(const uint8_t *buf, size_t len)
{
    S.tx_air += (double)(len + 24u) * 8.0 / 9600.0;   /* 프리앰블·플래그 포함 방송 시간 */
    S.tx_frames++;
    if (S.first_tx_t < 0.0) {
        S.first_tx_t = S.t;
    }
    if (S.t < 45.0 * 60.0) {
        S.tx_early++;                                  /* CubeSat 규격 위반 감시 */
    }
    gs_downlink(buf, len);
    return true;
}

bool hal_nvm_read(uint32_t addr, void *buf, size_t len)
{
    if ((size_t)addr + len > NVM_SIZE) {
        return false;
    }
    memcpy(buf, S.nvm + addr, len);
    return true;
}

bool hal_nvm_write(uint32_t addr, const void *buf, size_t len)
{
    if ((size_t)addr + len > NVM_SIZE) {
        return false;
    }
    memcpy(S.nvm + addr, buf, len);
    return true;
}

void hal_ewod_hv(bool on, float vrms, bool ac)
{
    S.hv_cmd = on;
    S.hv_v = vrms;
    S.hv_ac = ac;
}

void hal_ewod_electrodes(uint16_t mask) { S.electrodes = mask; }

float hal_ewod_cap_pf(uint8_t e)
{
    if (!S.rail[RAIL_PAYLOAD] || e > 9u) {
        return 0.0f;
    }
    return (float)(ewod_cap(e) + 0.5 * rng_n());
}

float hal_ewod_leak_ua(void)
{
    if (!S.rail[RAIL_PAYLOAD]) {
        return 0.0f;
    }
    return (float)(ewod_leak_ua() * (1.0 + 0.03 * rng_n()));
}

float hal_payload_temp_c(void) { return (float)(S.Tp + 0.1 * rng_n()); }
void hal_payload_heater(bool on) { S.pl_heater = on; }

void hal_event_hook(uint32_t met_s, uint16_t id, int32_t a, int32_t b)
{
    simlog_event("위성", met_s, id, a, b, false);
}

/* OBC가 리셋되면 OBC가 잡고 있던 출력은 모두 기본값으로 */
void simhal_power_on_defaults(void)
{
    memset(S.rail, 0, sizeof S.rail);
    S.charge_en = true;
    S.burnwire = false;
    S.m_cmd = v3_(0, 0, 0);
    S.pl_heater = false;
    S.hv_cmd = false;
    S.hv_v = 0.0;
    S.electrodes = 0;
    S.rx_n = 0;
}
