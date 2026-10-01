/* 단위 테스트용 가짜 하드웨어: 테스트가 센서 값을 마음대로 정하고 출력을 검사한다 */
#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include "hal.h"
#include "proto.h"

#define MOCK_TXMAX 512

typedef struct {
    hal_reset_cause_t reset_cause;
    bool reset_requested;
    /* 자세 */
    float mag[3];
    bool mag_ok;
    float gyro[3];
    bool gyro_ok;
    float mtq[3];
    /* 전력 */
    hal_eps_t eps;
    bool rail[RAIL_COUNT];
    bool charge_en;
    /* 전개 */
    bool burnwire;
    int burn_starts;
    bool ant_deployed;
    bool ant_deploy_on_burn;      /* 열선을 켜면 바로 펴지게 */
    /* 무선 */
    uint8_t rx[8][PROTO_MAX_FRAME];
    size_t rxlen[8];
    int rx_n, rx_head;
    uint8_t tx[MOCK_TXMAX][PROTO_MAX_FRAME];
    size_t txlen[MOCK_TXMAX];
    int tx_n;
    /* FRAM */
    uint8_t nvm[8192];
    /* EWOD */
    bool hv;
    float hv_v;
    bool hv_ac;
    uint16_t electrodes;
    float pl_temp;
    bool pl_heater;
    float leak_ua;
    int drop_pos;                 /* 트랙 위 액적 전극 번호 (0 = 없음) */
    bool finger;                  /* 디스펜스 중 액체가 E1·E2를 덮고 있음 */
    /* 이벤트 */
    uint16_t ev_id[1024];
    int32_t ev_a[1024], ev_b[1024];
    int ev_n;
} mock_t;

extern mock_t M;

void mock_power_cycle(bool wipe_nvm);   /* 전원 재인가 (NVM은 선택) */
void mock_set_nominal(void);            /* 정상 궤도 상태의 센서 값 */
void mock_push_rx(const uint8_t *buf, size_t n);
int mock_events(uint16_t id);

#endif
