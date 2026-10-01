/*
 * 지상-위성 인터페이스 정의 (ICD)
 * 프레임 형식, 명령 코드, 텔레메트리 형식. 비행 소프트웨어와 지상국이 같은 코드를 쓴다.
 *
 * 프레임 구조 (리틀엔디언, CRC만 빅엔디언)
 *   오프셋 크기
 *    0     2   SYNC   EB 90
 *    2     1   VER(상위 4비트) | TYPE(하위 4비트)
 *    3     2   SCID   위성 식별자
 *    5     4   SEQ    명령: 재전송 방지 카운터 / 텔레메트리: 프레임 번호
 *    9     1   LEN    페이로드 길이 (0..200)
 *   10     N   PAYLOAD
 *   10+N   8   MAC    명령 프레임만. SipHash-2-4(바이트 2 .. 10+N)
 *   끝-2   2   CRC    CRC-16/CCITT(바이트 2 .. 끝-2)
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_SYNC0          0xEBu
#define PROTO_SYNC1          0x90u
#define PROTO_VERSION        1u
#define PROTO_HDR_LEN        10u
#define PROTO_MAC_LEN        8u
#define PROTO_CRC_LEN        2u
#define PROTO_MAX_PAYLOAD    200u
#define PROTO_MAX_FRAME      (PROTO_HDR_LEN + PROTO_MAX_PAYLOAD + PROTO_MAC_LEN + PROTO_CRC_LEN)

/* 프레임 종류 */
enum {
    FT_CMD = 0x1,        /* 지상→위성 명령 (MAC 필수) */
    FT_BEACON = 0x8,     /* 주기 비콘 */
    FT_ACK = 0x9,        /* 명령 응답 */
    FT_PAYLOAD = 0xA,    /* 실험 결과 */
    FT_EVENTS = 0xB      /* 이벤트 로그 */
};

typedef struct {
    uint8_t type;
    uint16_t scid;
    uint32_t seq;
    uint8_t len;
    uint8_t data[PROTO_MAX_PAYLOAD];
} frame_t;

typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_SHORT,
    PROTO_ERR_SYNC,
    PROTO_ERR_LEN,
    PROTO_ERR_CRC,
    PROTO_ERR_VERSION,
    PROTO_ERR_SCID,
    PROTO_ERR_MAC
} proto_err_t;

/* key == NULL 이면 MAC 없는 프레임만 다룰 수 있다 */
size_t proto_encode(const frame_t *f, const uint8_t *key, uint8_t *out, size_t cap);
proto_err_t proto_decode(const uint8_t *buf, size_t n, uint16_t scid, const uint8_t *key, frame_t *f);

/* ---------------- 명령 ---------------- */
enum {
    OP_PING = 0x01,
    OP_SET_TIME = 0x02,      /* u32 UTC [s] */
    OP_SET_MODE = 0x03,      /* u8 0=SAFE 해제, 1=SAFE 고정 */
    OP_SET_BEACON = 0x04,    /* u16 비콘 주기 [s] */
    OP_EXP_SCHEDULE = 0x10,  /* u32 지연[s], u8 전압[Vrms], u8 왕복 횟수, u8 구동(0=DC, 1=AC) */
    OP_EXP_ABORT = 0x11,
    OP_EXP_DOWNLINK = 0x12,  /* u8 실험 번호 (0xFF = 저장된 전부) */
    OP_REQ_EVENTS = 0x20,    /* u8 개수 (1..16) */
    OP_RESET = 0x30          /* u32 확인 코드 RESET_MAGIC */
};
#define RESET_MAGIC 0x52535421u   /* "RST!" */

enum { ACK_OK = 0, ACK_DUPLICATE, ACK_BAD_ARGS, ACK_REJECTED, ACK_UNKNOWN_OP };

/* ---------------- 모드·상태 코드 ---------------- */
enum { MODE_DEPLOY_WAIT = 0, MODE_DETUMBLE, MODE_NOMINAL, MODE_SAFE, MODE_COUNT };

enum { PL_IDLE = 0, PL_ARMED, PL_PREHEAT, PL_HVCHECK, PL_DISPENSE, PL_TRANSPORT, PL_RETURN };

enum {
    PLR_OK = 0, PLR_ABORT_POWER, PLR_ABORT_RATE, PLR_ABORT_TEMP, PLR_ABORT_LEAK,
    PLR_ABORT_CMD, PLR_ABORT_MODE, PLR_PREHEAT_TIMEOUT, PLR_DISPENSE_FAIL,
    PLR_STUCK, PLR_TIMEOUT, PLR_EXPIRED, PLR_INTERRUPTED, PLR_TRACK_BLOCKED, PLR_COUNT
};

enum {
    EVT_BOOT = 1,        /* a=부팅 횟수, b=리셋 원인 */
    EVT_MODE,            /* a=이전(255=부팅), b=새 모드 */
    EVT_BURN,            /* a=시도 횟수 */
    EVT_ANT_OK,          /* a=시도 횟수 */
    EVT_ANT_FAIL,
    EVT_RF_ON,
    EVT_DETUMBLED,       /* a=각속도 [0.01°/s] */
    EVT_TUMBLING,        /* a=각속도 [0.01°/s] */
    EVT_LOW_POWER,       /* a=SoC%, b=전압 mV */
    EVT_POWER_OK,        /* a=SoC% */
    EVT_BATT_HEATER,     /* a=1/0, b=온도 [0.1°C] */
    EVT_CHARGE_INHIBIT,  /* a=1/0, b=온도 [0.1°C] */
    EVT_CMD,             /* a=명령 코드, b=seq */
    EVT_CMD_REJECT,      /* a=명령 코드, b=ACK 상태 */
    EVT_AUTH_FAIL,       /* a=seq */
    EVT_REPLAY,          /* a=받은 seq, b=마지막 seq */
    EVT_TIME_SET,
    EVT_PL_ARMED,        /* a=실험 번호, b=지연 [분] */
    EVT_PL_START,        /* a=실험 번호, b=SoC% */
    EVT_PL_DONE,         /* a=결과 코드, b=실험 번호 */
    EVT_NVM_FALLBACK,    /* a=유효 슬롯 비트 */
    EVT_BOOTLOOP,        /* a=연속 리셋 횟수 */
    EVT_MAG_FAULT,
    EVT_COUNT
};

/* ---------------- 텔레메트리 ---------------- */
enum {
    BF_ANT_OK = 1u << 0, BF_SUNLIT = 1u << 1, BF_BATT_HEATER = 1u << 2, BF_CHG_INHIBIT = 1u << 3,
    BF_SAFE_LATCH = 1u << 4, BF_GYRO_OK = 1u << 5, BF_TIME_SYNC = 1u << 6, BF_MAG_OK = 1u << 7
};

typedef struct {
    uint32_t met_s, utc_s;
    uint16_t boot_count;
    uint8_t mode, flags;
    uint16_t batt_mv;
    int16_t batt_ma;
    uint8_t soc_pct;
    int8_t batt_c, pl_c;
    uint16_t solar_mw;
    uint16_t rate_cdps;      /* 0.01 °/s, 0xFFFF = 알 수 없음 */
    uint16_t rx_ok, rx_bad, auth_fail, replay;
    uint32_t last_seq;
    uint16_t exp_count;
    uint8_t pl_state, reset_cause;
    uint16_t last_event;
} tm_beacon_t;

typedef struct {
    uint16_t exp_id;
    uint32_t start_met;
    uint8_t drive_ac, volts, cycles_plan, cycles_done;
    uint16_t steps_ok, steps_retry, steps_fail;
    uint16_t step_ms_mean, step_ms_max;
    uint16_t leak_na_start, leak_na_end;
    int8_t chip_c;
    uint8_t result;
    uint8_t disp_ok, disp_fail;
    uint16_t duration_s;
} tm_result_t;

typedef struct {
    uint32_t met_s;
    uint16_t id;
    int16_t a, b;
} tm_event_t;

/* 실험 데이터 프레임(FT_PAYLOAD) 첫 바이트 */
enum { PLD_SUMMARY = 1, PLD_STEPS = 2 };

#define TM_BEACON_SIZE  41u
#define TM_RESULT_SIZE  30u
#define TM_EVENT_SIZE   10u

size_t tm_beacon_pack(const tm_beacon_t *b, uint8_t *out, size_t cap);
bool tm_beacon_unpack(const uint8_t *p, size_t n, tm_beacon_t *b);
size_t tm_result_pack(const tm_result_t *r, uint8_t *out, size_t cap);
bool tm_result_unpack(const uint8_t *p, size_t n, tm_result_t *r);
size_t tm_event_pack(const tm_event_t *e, uint8_t *out, size_t cap);
bool tm_event_unpack(const uint8_t *p, size_t n, tm_event_t *e);

/* ---------------- 바이트 버퍼 도우미 ---------------- */
typedef struct {
    uint8_t *p;
    size_t cap, n;
    bool ovf;
} wbuf_t;

typedef struct {
    const uint8_t *p;
    size_t n, pos;
    bool err;
} rbuf_t;

void wb_init(wbuf_t *w, uint8_t *p, size_t cap);
void wb_u8(wbuf_t *w, uint8_t v);
void wb_u16(wbuf_t *w, uint16_t v);
void wb_u32(wbuf_t *w, uint32_t v);
void rb_init(rbuf_t *r, const uint8_t *p, size_t n);
uint8_t rb_u8(rbuf_t *r);
uint16_t rb_u16(rbuf_t *r);
uint32_t rb_u32(rbuf_t *r);
int16_t u16_to_s16(uint16_t v);
int8_t u8_to_s8(uint8_t v);

/* 사람이 읽는 이름 */
const char *proto_mode_name(uint8_t m);
const char *proto_op_name(uint8_t op);
const char *proto_ack_name(uint8_t s);
const char *proto_plr_name(uint8_t r);
const char *proto_pl_state_name(uint8_t s);

#endif
