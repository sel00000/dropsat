/*
 * 하드웨어 추상화 계층 (HAL)
 *
 * 비행 소프트웨어는 오직 이 함수들로만 하드웨어에 접근한다.
 *   - 시뮬레이터 구현:   sim/sim_hal.c
 *   - 단위 테스트 구현: tests/mock_hal.c
 *   - 실제 보드 구현:   이 파일의 함수들을 MCU 드라이버로 채우면 된다
 */
#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RAIL_ADCS = 0,       /* 자력계·자이로·자기토커 */
    RAIL_PAYLOAD,        /* EWOD 탑재체 (MCU·고전압 드라이버·칩 히터) */
    RAIL_BATT_HEATER,    /* 배터리 히터 */
    RAIL_COUNT
} hal_rail_t;

typedef enum {
    RESET_POWER_ON = 0,
    RESET_WATCHDOG,
    RESET_SOFTWARE,
    RESET_BROWNOUT
} hal_reset_cause_t;

typedef struct {
    float batt_v;        /* 배터리 전압 [V] */
    float batt_a;        /* 배터리 전류 [A], +충전 / -방전 */
    float batt_c;        /* 배터리 온도 [°C] */
    float solar_w;       /* 태양전지에서 실제로 끌어온 전력 [W] */
    float panel_v;       /* 패널 전압 중 최댓값 [V] (일조 판정용) */
} hal_eps_t;

/* 리셋·워치독 */
hal_reset_cause_t hal_reset_cause(void);
void hal_wdt_kick(void);
void hal_reset(void);

/* 자세 센서·구동기 (모두 기체 좌표계) */
bool hal_mag_read(float b_tesla[3]);
bool hal_gyro_read(float w_rad_s[3]);
void hal_mtq_set(const float m_am2[3]);

/* 전력 */
bool hal_eps_read(hal_eps_t *out);
void hal_rail_set(hal_rail_t rail, bool on);
void hal_charge_enable(bool on);

/* 안테나 전개 */
void hal_burnwire(bool on);
bool hal_antenna_deployed(void);

/* UHF 무선 */
size_t hal_radio_rx(uint8_t *buf, size_t cap);           /* 수신 프레임 길이, 없으면 0 */
bool hal_radio_tx(const uint8_t *buf, size_t len);

/* 비휘발성 메모리 (FRAM) */
bool hal_nvm_read(uint32_t addr, void *buf, size_t len);
bool hal_nvm_write(uint32_t addr, const void *buf, size_t len);

/* EWOD 탑재체 */
void hal_ewod_hv(bool on, float vrms, bool ac);           /* 고전압 드라이버 */
void hal_ewod_electrodes(uint16_t mask);                  /* 전극 on/off 비트마스크 */
float hal_ewod_cap_pf(uint8_t electrode);                 /* 전극 정전용량 → 액적 위치 감지 */
float hal_ewod_leak_ua(void);                             /* 유전막 누설전류 */
float hal_payload_temp_c(void);
void hal_payload_heater(bool on);

/* 디버그 훅: 이벤트가 기록될 때마다 호출 (비행 빌드에서는 빈 함수) */
void hal_event_hook(uint32_t met_s, uint16_t id, int32_t a, int32_t b);

#endif
