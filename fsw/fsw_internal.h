/*
 * 비행 소프트웨어 내부 상태와 모듈 간 함수
 * 동적 메모리 없음: 모든 상태는 정적 구조체 g_fsw 하나에 들어 있다.
 * fsw_init()이 이 구조체를 0으로 지우는 것이 곧 "RAM이 날아간 리부트"다.
 */
#ifndef FSW_INTERNAL_H
#define FSW_INTERNAL_H

#include "config.h"
#include "hal.h"
#include "proto.h"

#define PERSIST_MAGIC     0x44534154u   /* 'DSAT' */
#define PERSIST_VERSION   2u

/* 비휘발성 상태 (FRAM, A/B 두 슬롯에 번갈아 저장) */
typedef struct {
    uint32_t magic;
    uint32_t write_count;
    uint32_t met_s;            /* 임무 경과 시간 (사출 후) */
    uint32_t last_boot_met;
    uint32_t cmd_seq;          /* 마지막으로 수락한 명령 번호 (재전송 방지) */
    uint32_t utc_offset;       /* UTC = MET + offset, 0이면 미동기 */
    uint32_t pl_start_met;     /* 예약된 실험 시작 시각 */
    uint16_t version;
    uint16_t boot_count;
    uint16_t exp_count;
    uint16_t beacon_period_s;
    uint16_t pl_exp_id;
    uint8_t quick_reboots;
    uint8_t deploy_done;
    uint8_t ant_ok;
    uint8_t burn_tries;
    uint8_t safe_latched;
    uint8_t last_mode;
    uint8_t pl_saved;          /* 0=없음, 1=예약됨, 2=실행 중 */
    uint8_t pl_volts;
    uint8_t pl_cycles;
    uint8_t pl_ac;
    uint16_t crc;
} persist_t;

enum { ADCS_OFF = 0, ADCS_SENSE, ADCS_BDOT };

typedef struct {
    uint8_t mode;
    float b[3], b_prev[3];
    bool prev_valid, mag_ok;
    float bhat_dot[3];
    bool bdot_fresh;
    float m_cmd[3];
    float w[3];
    bool gyro_ok;
    float rate;                /* 필터된 각속도 크기 [rad/s] */
    bool rate_valid;
    uint32_t low_s, high_s;    /* 저/고 각속도 연속 시간 */
    uint16_t mag_fault_n;
} adcs_t;

typedef struct {
    hal_eps_t raw;
    bool raw_ok;
    float soc;
    bool soc_init;
    bool heater;
    bool charge_inhibit;
    bool low_power;
    uint32_t eoc_s;
} eps_t;

typedef struct {
    uint8_t q[TXQ_DEPTH][PROTO_MAX_FRAME];
    uint16_t qlen[TXQ_DEPTH];
    uint8_t head, count;
    uint32_t tm_seq;
    uint32_t next_beacon;
    uint16_t beacon_period;
    uint16_t rx_ok, rx_bad, auth_fail, replay;
    bool rf_on;
    bool reset_pending;
} comms_t;

typedef struct {
    uint8_t state;
    uint16_t exp_id;
    uint32_t start_met;        /* 예약 시작 시각 */
    uint32_t run_start;        /* 실제 시작 시각 */
    uint32_t state_since;
    uint8_t volts, cycles, ac;
    bool hv_on, heater;
    uint16_t mask;
    /* 시퀀스 진행 */
    uint8_t pos, target, cycle, retries, disp_tries, dsub, settle;
    int8_t dir;
    bool repulse;
    bool clearing;             /* 지난 실험에서 남은 액적을 회수하는 중 */
    uint16_t step_t;           /* 이번 재시도 이후 틱 */
    uint16_t step_total_t;     /* 이 칸 이동에 든 전체 틱 */
    /* 측정값 */
    uint32_t step_ms_sum;
    uint16_t step_ms_max, steps_ok, steps_retry, steps_fail;
    uint8_t disp_ok, disp_fail;
    float leak_start, leak_end;
    float temp_sum;
    uint16_t temp_n;
    uint16_t raw[PL_RAW_MAX];
    uint8_t n_raw;
} payload_t;

typedef struct {
    uint32_t tick, uptime_s, met_s;
    uint8_t mode;
    uint32_t mode_since;
    uint8_t reset_cause;
    bool first_boot;
    bool burning;
    uint32_t burn_start, burn_end;
    persist_t nv;
    adcs_t adcs;
    eps_t eps;
    comms_t comms;
    payload_t pl;
    tm_event_t evlog[EVLOG_N];
    uint16_t ev_head, ev_count;
    uint16_t last_event;
} fsw_t;

extern fsw_t g_fsw;

/* fsw.c */
void fsw_event(uint16_t id, int32_t a, int32_t b);
void fsw_set_mode(uint8_t m);
void fsw_persist(void);
uint32_t fsw_utc(void);
uint8_t fsw_events_latest(tm_event_t *out, uint8_t max);

/* persist.c */
uint8_t persist_load(persist_t *p);              /* 반환: 유효 슬롯 비트 (bit0=A, bit1=B) */
void persist_save(persist_t *p);
void persist_store_result(const tm_result_t *r, const uint16_t *raw, uint8_t n_raw);
bool persist_load_result(uint8_t slot, tm_result_t *r);
bool persist_load_raw(uint8_t slot, uint16_t exp_id, uint16_t *raw, uint8_t *n_raw);

/* adcs.c */
void adcs_init(void);
void adcs_set_mode(uint8_t m);
void adcs_tick(uint32_t sub);

/* eps.c */
void eps_init(void);
void eps_task_1hz(void);
bool eps_sunlit(void);

/* comms.c */
void comms_init(void);
void comms_tick(uint32_t sub);
bool comms_send(uint8_t type, const uint8_t *data, uint8_t len);
void comms_set_beacon_period(uint16_t s);

/* payload.c */
void payload_init(void);
void payload_tick(uint32_t sub);
uint8_t payload_schedule(uint32_t delay_s, uint8_t volts, uint8_t cycles, uint8_t ac);
uint8_t payload_abort(uint8_t code);
void payload_downlink(uint8_t exp_id);

#endif
