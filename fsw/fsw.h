/*
 * 비행 소프트웨어 공개 인터페이스
 *
 * 실제 보드에서는:
 *     fsw_init();
 *     매 100 ms 타이머 인터럽트(또는 RTOS 태스크)에서 fsw_tick();
 */
#ifndef FSW_H
#define FSW_H

#include <stdbool.h>
#include <stdint.h>

void fsw_init(void);
void fsw_tick(void);

/* 시뮬레이터·테스트에서 내부 상태를 들여다보기 위한 읽기 전용 스냅샷 */
typedef struct {
    uint32_t met_s;
    uint8_t mode;
    uint8_t pl_state;
    float soc_est;
    bool soc_valid;
    float rate_est_rad;
    bool rate_valid;
    float m_cmd[3];
    uint16_t boot_count;
    uint32_t cmd_seq;
    uint16_t rx_ok, rx_bad, auth_fail, replay;
    bool batt_heater;
    bool charge_inhibit;
    bool low_power;
    uint16_t exp_count;
    uint16_t beacon_period_s;
} fsw_view_t;

void fsw_view(fsw_view_t *v);

#endif
