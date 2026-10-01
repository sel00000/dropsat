/*
 * 시뮬레이터 공용 상태
 * 실제 위성이 겪을 물리(궤도·자기장·자세·햇빛·배터리·열·무선 링크·EWOD 칩)를 흉내 내고,
 * 비행 소프트웨어에는 hal.h 함수로만 보인다.
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hal.h"
#include "proto.h"
#include "vecmath.h"

#define PI          3.14159265358979323846
#define D2R         (PI / 180.0)
#define R2D         (180.0 / PI)
#define MU_E        3.986004418e14     /* 지구 중력상수 [m³/s²] */
#define R_E         6378137.0          /* 지구 반지름 [m] */
#define J2_E        1.08263e-3
#define W_E         7.2921159e-5       /* 지구 자전 [rad/s] */
#define S0          1361.0             /* 태양상수 [W/m²] */
#define SIGMA_SB    5.670374e-8
#define SIM_DT      0.1                /* 시뮬레이션 간격 = FSW 틱 */
#define NVM_SIZE    8192
#define RXQ_N       8

typedef struct {
    bool drop;          /* 트랙 위에 액적이 있나 */
    double x;           /* 액적 중심 [전극 피치 단위] */
    double finger;      /* 디스펜스 중 저장조에서 뻗어 나온 액체 길이 */
    double v_trap;      /* 유전막 포획전하에 의한 전압 손실 [V] */
    double jitter;      /* 이번 이동의 속도 편차 */
    int last_target;
} ewod_t;

typedef struct {
    /* 시간 */
    long k;
    double t;                       /* 사출 후 경과 [s] */
    double jd0, unix0;
    /* 궤도 */
    double a, inc, raan0, raan_dot, u0, u_dot;
    v3 r, v;
    double gmst;
    /* 자세 */
    quat q;                         /* 기체→ECI */
    v3 w, I, m_res, m_applied, mtq_scale;
    /* 환경 */
    v3 sun;
    double sun_au;
    bool eclipse;
    v3 b_eci, b_body;
    /* 전력 */
    double soc, v_batt, i_batt, r_pack, p_avail, p_used, p_load, panel_v, cap_ah;
    /* 열 (구조체, 내부 전자장비, 배터리, 탑재체 칩) */
    double Ts, Te, Tb, Tp;
    /* HAL이 바꾸는 하드웨어 상태 */
    bool rail[RAIL_COUNT];
    bool charge_en, burnwire, ant_deployed;
    double burn_acc;
    v3 m_cmd;
    bool pl_heater, hv_cmd, hv_ac;
    double hv_v;
    uint16_t electrodes;
    double tx_air;                  /* 이번 스텝 송신 시간 [s] */
    /* 센서 오차 */
    v3 mag_bias, gyro_bias;
    double ibat_bias;
    /* OBC */
    bool cpu_up, cpu_hung, reset_req;
    double cpu_down_until, wdt_last;
    hal_reset_cause_t reset_cause;
    int resets;
    uint8_t nvm[NVM_SIZE];
    /* 수신 큐 (지상→위성) */
    uint8_t rxq[RXQ_N][PROTO_MAX_FRAME];
    size_t rxlen[RXQ_N];
    int rx_head, rx_n;
    /* 통계 */
    double e_solar_wh, e_load_wh;
    long tx_frames, tx_early;
    double first_tx_t, first_burn_t;
    ewod_t ew;
    uint64_t rng;
} sim_t;

extern sim_t S;

/* physics.c */
double rng_u(void);
double rng_n(void);
void phys_init(uint64_t seed, double u0_deg);
void phys_step(double dt);
v3 eci_to_ecef(v3 r, double gmst);
v3 ecef_to_eci(v3 r, double gmst);
bool hv_effective(void);

/* ewod_sim.c */
void ewod_init(void);
void ewod_step(double dt);
double ewod_cap(int e);
double ewod_leak_ua(void);

/* sim_hal.c */
void simhal_power_on_defaults(void);
void simhal_rx_push(const uint8_t *buf, size_t n);

/* ground.c */
void gs_init(const char *outdir);
void gs_tick(void);
void gs_downlink(const uint8_t *buf, size_t n);
void gs_finish(void);
double gs_elevation(void);

/* simlog.c */
void simlog_open(const char *path);
void simlog_close(void);
void simlog(const char *who, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void simlog_event(const char *who, uint32_t met, uint16_t id, int32_t a, int32_t b, bool dump);
const char *reset_cause_kr(int c);

#endif
