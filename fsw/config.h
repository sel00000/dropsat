/*
 * DROPSAT-1 비행 소프트웨어 설정값
 * 모든 임계값과 주기는 이 파일 한 곳에서 관리한다.
 */
#ifndef FSW_CONFIG_H
#define FSW_CONFIG_H

#define FSW_VERSION           0x0100u      /* v1.0 */
#define SPACECRAFT_ID         0x0D01u

/* ---------------- 스케줄러 ---------------- */
#define TICK_HZ               10u          /* fsw_tick() 호출 주기 100 ms */

/* ---------------- 사출 직후 (CubeSat Design Specification Rev 14.1) ---------------- */
#define DEPLOY_WAIT_S         (30u * 60u)  /* 2.4.4 전개물은 사출 30분 뒤부터 */
#define RF_SILENCE_S          (45u * 60u)  /* 2.4.5 신호 송신은 사출 45분 뒤부터 */
#define BURN_ON_S             5u           /* 열선 1회 가열 시간 */
#define BURN_RETRY_S          60u          /* 재시도 간격 */
#define BURN_MAX_TRIES        5u

/* ---------------- 부팅·모드 ---------------- */
#define BOOTLOOP_WINDOW_S     600u         /* 이 시간 안에 연속 리셋되면 */
#define BOOTLOOP_LIMIT        3u           /* 이 횟수에서 SAFE로 부팅 */
#define SAFE_MIN_S            300u         /* SAFE 최소 유지 시간 */
#define EVLOG_N               32u          /* 이벤트 로그 링버퍼 크기 */

/* ---------------- 자세제어 (B-dot 디텀블링) ---------------- */
#define DEG2RAD               0.017453292f
#define ADCS_DT_S             1.0f         /* 자력계 샘플 간격 */
#define MTQ_MAX_AM2           0.20f        /* 축당 최대 자기쌍극자 [A·m²] */
#define BDOT_GAIN             4.0e-5f      /* m = -(K/|B|)·d(b̂)/dt  [N·m·s] */
#define MAG_MIN_T             5.0e-6f      /* 600 km 고도에서 |B| 타당 범위 */
#define MAG_MAX_T             1.0e-4f
#define RATE_LPF              0.3f
#define RATE_LOW_RAD          (1.0f * DEG2RAD)   /* 디텀블 완료 판정 */
#define RATE_HIGH_RAD         (3.0f * DEG2RAD)   /* 다시 텀블링 판정 */
#define RATE_LOW_HOLD_S       300u
#define RATE_HIGH_HOLD_S      30u

/* ---------------- 전력 (2S1P 18650 리튬이온) ---------------- */
#define BATT_CAPACITY_AH      3.0f
#define BATT_R_PACK           0.12f        /* 내부저항 추정값 [Ω] */
#define SOC_OCV_GAIN          0.0005f      /* 전압 기반 보정 세기 (1초당) */
#define SOC_SAFE_ENTER        0.20f
#define SOC_SAFE_EXIT         0.45f
#define VBATT_CRITICAL        6.6f         /* 3.3 V/cell 아래면 즉시 SAFE */
#define EOC_V                 8.25f        /* 만충 판정 전압 */
#define EOC_A                 0.10f        /* 만충 판정 전류 */
#define BATT_HEAT_ON_C        2.0f
#define BATT_HEAT_OFF_C       6.0f
#define BATT_CHARGE_MIN_C     0.0f         /* 0°C 이하 충전 금지 (리튬 석출) */
#define BATT_CHARGE_RESUME_C  2.0f
#define SUN_PANEL_V           3.0f         /* 패널 전압이 이보다 높으면 일조 */

/* ---------------- 통신 (UHF 437 MHz, 9600 bps) ---------------- */
#define BEACON_NOMINAL_S      30u
#define BEACON_SAFE_S         120u
#define BEACON_MIN_S          10u
#define BEACON_MAX_S          600u
#define TXQ_DEPTH             32u

/* ---------------- EWOD 탑재체 ---------------- */
/* 전극 배치: E0 = 저장조, E1..E8 = 수송 트랙 */
#define PL_RESERVOIR          0u
#define PL_TRACK_FIRST        1u
#define PL_TRACK_LAST         8u
#define PL_VOLTS_MIN          40u
#define PL_VOLTS_MAX          150u
#define PL_CYCLES_MAX         10u
#define PL_DELAY_MAX_S        (7u * 86400u)
#define PL_PREHEAT_MIN_C      20.0f        /* 이 온도가 되어야 고전압 인가 */
#define PL_HEAT_ON_C          22.0f        /* 실험 중 칩 온도 유지 범위 */
#define PL_HEAT_OFF_C         26.0f
#define PL_PREHEAT_TIMEOUT_S  1800u
#define PL_TEMP_MAX_C         45.0f
#define PL_LEAK_MAX_UA        2.0f         /* 유전막 누설전류 한계 (절연파괴 감지) */
#define PL_CAP_DETECT_PF      40.0f        /* 전극 위 액적 감지 임계 정전용량 */
#define PL_HV_SETTLE_T        20u          /* 고전압 안정화 (틱) */
#define PL_STEP_TIMEOUT_T     10u          /* 전극 한 칸 이동 제한 1.0 s */
#define PL_SETTLE_T           2u           /* 도착 감지 후 안착 대기 */
#define PL_STEP_RETRIES       2u
#define PL_MERGE_T            5u           /* E1이 빈 뒤에도 0.5 s 더 저장조로 끌어당긴다 */
#define PL_MERGE_TIMEOUT_T    30u
#define PL_DISP_EXTEND_T      20u          /* 디스펜스: 액체가 E2에 닿을 때까지 최대 2 s */
#define PL_DISP_CUT_T         6u           /* 디스펜스: 중간 전극을 끊고 기다리는 시간 */
#define PL_DISPENSE_TRIES     3u
#define PL_SOC_START          0.55f
#define PL_SOC_ABORT          0.35f
#define PL_RATE_START_RAD     (1.0f * DEG2RAD)   /* 원심가속도 < 1e-6 g 보장 */
#define PL_RATE_ABORT_RAD     (2.0f * DEG2RAD)
#define PL_ARM_EXPIRE_S       (3u * 3600u)
#define PL_RUN_TIMEOUT_S      (40u * 60u)
#define PL_RESULT_SLOTS       8u
#define PL_RAW_MAX            124u         /* 실험당 저장하는 이동시간 샘플 수 */

#endif
