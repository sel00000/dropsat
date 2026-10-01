/* 임무 로그: 화면과 mission_log.txt에 같이 쓴다 */
#include <stdarg.h>
#include <stdio.h>

#include "sim.h"

static FILE *g_log;

void simlog_open(const char *path) { g_log = fopen(path, "w"); }

void simlog_close(void)
{
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void simlog(const char *who, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    const long s = (long)S.t;
    printf("[T+%02ld:%02ld:%02ld] %s  %s\n", s / 3600, (s / 60) % 60, s % 60, who, msg);
    if (g_log) {
        fprintf(g_log, "[T+%02ld:%02ld:%02ld] %s  %s\n", s / 3600, (s / 60) % 60, s % 60, who, msg);
    }
}

const char *reset_cause_kr(int c)
{
    switch (c) {
    case RESET_POWER_ON: return "전원 인가";
    case RESET_WATCHDOG: return "워치독";
    case RESET_SOFTWARE: return "소프트웨어";
    case RESET_BROWNOUT: return "저전압";
    default: return "?";
    }
}

static void fmt_event(char *o, size_t n, uint16_t id, int32_t a, int32_t b)
{
    switch (id) {
    case EVT_BOOT: snprintf(o, n, "부팅 #%d (원인: %s)", (int)a, reset_cause_kr((int)b)); break;
    case EVT_MODE:
        snprintf(o, n, "모드 %s → %s", a == 255 ? "부팅" : proto_mode_name((uint8_t)a), proto_mode_name((uint8_t)b));
        break;
    case EVT_BURN: snprintf(o, n, "안테나 고정줄 열선 가열 (%d회째)", (int)a); break;
    case EVT_ANT_OK: snprintf(o, n, "안테나 전개 확인 (가열 %d회)", (int)a); break;
    case EVT_ANT_FAIL: snprintf(o, n, "안테나 전개 실패 (가열 %d회), 임무 계속", (int)a); break;
    case EVT_RF_ON: snprintf(o, n, "송신 허용 (사출 후 45분 경과) → 비콘 시작"); break;
    case EVT_DETUMBLED: snprintf(o, n, "디텀블링 완료 (각속도 %.2f°/s)", a / 100.0); break;
    case EVT_TUMBLING: snprintf(o, n, "각속도 증가 감지 (%.2f°/s)", a / 100.0); break;
    case EVT_LOW_POWER: snprintf(o, n, "저전력 (SoC %d%%, %.2f V)", (int)a, b / 1000.0); break;
    case EVT_POWER_OK: snprintf(o, n, "전력 회복 (SoC %d%%)", (int)a); break;
    case EVT_BATT_HEATER: snprintf(o, n, "배터리 히터 %s (%.1f°C)", a ? "켬" : "끔", b / 10.0); break;
    case EVT_CHARGE_INHIBIT: snprintf(o, n, "저온 충전 %s (%.1f°C)", a ? "금지" : "재개", b / 10.0); break;
    case EVT_CMD: snprintf(o, n, "명령 실행 %s (seq %d)", proto_op_name((uint8_t)a), (int)b); break;
    case EVT_CMD_REJECT:
        snprintf(o, n, "명령 거부 %s (%s)", proto_op_name((uint8_t)a), proto_ack_name((uint8_t)b));
        break;
    case EVT_AUTH_FAIL: snprintf(o, n, "인증 실패 프레임 폐기 (seq %d)", (int)a); break;
    case EVT_REPLAY: snprintf(o, n, "재전송 공격 차단 (받은 seq %d ≤ 마지막 seq %d)", (int)a, (int)b); break;
    case EVT_TIME_SET: snprintf(o, n, "UTC 시각 동기화"); break;
    case EVT_PL_ARMED: snprintf(o, n, "EWOD 실험 #%d 예약 (%d분 뒤)", (int)a, (int)b); break;
    case EVT_PL_START: snprintf(o, n, "EWOD 실험 #%d 시작 (SoC %d%%)", (int)a, (int)b); break;
    case EVT_PL_DONE: snprintf(o, n, "EWOD 실험 #%d 종료: %s", (int)b, proto_plr_name((uint8_t)a)); break;
    case EVT_NVM_FALLBACK: snprintf(o, n, "FRAM 슬롯 손상 → 백업 슬롯으로 복구 (유효 %d)", (int)a); break;
    case EVT_BOOTLOOP: snprintf(o, n, "연속 리셋 %d회 → SAFE로 부팅", (int)a); break;
    case EVT_MAG_FAULT: snprintf(o, n, "자력계 이상"); break;
    default: snprintf(o, n, "이벤트 %u (%d, %d)", id, (int)a, (int)b); break;
    }
}

void simlog_event(const char *who, uint32_t met, uint16_t id, int32_t a, int32_t b, bool dump)
{
    char msg[256];
    fmt_event(msg, sizeof msg, id, a, b);
    if (dump) {
        /* 지상에서 받은 이벤트 덤프: 위성 시계(MET)로 표시 */
        simlog(who, "    MET %02u:%02u:%02u  %s", (unsigned)(met / 3600u), (unsigned)(met / 60u % 60u),
               (unsigned)(met % 60u), msg);
    } else {
        simlog(who, "%s", msg);
    }
}
