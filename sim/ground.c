/*
 * 지상국 (대전, UHF 437 MHz, 원편파 크로스야기 14 dBi)
 *
 *  - 위성이 고도 5° 위로 올라오면 교신 시작 (AOS). 명령은 고도 10° 이상에서만 보낸다
 *  - 링크 버짓으로 Eb/N0를 계산하고 비트 오류를 실제로 프레임에 넣는다 → CRC가 걸러내야 한다
 *  - 위성 안테나는 -Z면 턴스타일: 거의 전방향이지만 방향에 따라 ±3 dB, 뒤쪽은 기체에 가린다
 *  - 첫 비콘을 들은 뒤 운용 계획(PLAN)을 차례로 실행. ACK가 없으면 "같은 프레임"으로 재전송
 *    (번호가 같으므로 위성이 이미 실행했어도 두 번 실행되지 않는다)
 *  - 실험 데이터가 빠지면 그 실험만 다시 요청 (갭 필링)
 *  - 보안 시험은 비콘의 차단 카운터가 올라가는 것을 확인할 때까지 반복
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "keys.h"
#include "sim.h"

#define GS_LAT       36.372
#define GS_LON       127.360
#define GS_ALT       100.0
#define GS_MIN_EL    5.0
#define GS_CMD_EL    10.0
#define ACK_TIMEOUT  4.0
#define MAX_TRIES    4
#define MAX_EXP      16

typedef enum { GC_CMD = 0, GC_REPLAY, GC_FORGED } gc_kind_t;

typedef struct {
    int contact;            /* 이 교신부터 보낸다 */
    gc_kind_t kind;
    uint8_t op;
    uint8_t args[8];
    uint8_t nargs;
    const char *desc;
} gs_cmd_t;

/* 운용 계획 */
static const gs_cmd_t PLAN[] = {
    {1, GC_CMD, OP_PING, {0}, 0, "생존 확인"},
    {1, GC_CMD, OP_SET_TIME, {0}, 4, "UTC 시각 동기화"},
    {1, GC_CMD, OP_REQ_EVENTS, {16}, 1, "이벤트 로그 요청"},
    {1, GC_CMD, OP_EXP_SCHEDULE, {0x08, 0x07, 0, 0, 110, 5, 1}, 7, "EWOD 실험 #1 예약: AC 110 V, 왕복 5회, 30분 뒤"},
    {2, GC_CMD, OP_EXP_DOWNLINK, {0xFF}, 1, "실험 데이터 다운링크"},
    {2, GC_CMD, OP_EXP_SCHEDULE, {0xB0, 0x04, 0, 0, 110, 5, 0}, 7, "EWOD 실험 #2 예약: DC 110 V, 왕복 5회, 20분 뒤"},
    {3, GC_CMD, OP_REQ_EVENTS, {16}, 1, "이벤트 로그 요청"},
    {3, GC_CMD, OP_EXP_DOWNLINK, {0xFF}, 1, "실험 데이터 다운링크"},
    {3, GC_REPLAY, 0, {0}, 0, "[보안 시험] 1번 교신 때 녹음해 둔 명령 프레임을 그대로 재전송"},
    {3, GC_FORGED, OP_SET_MODE, {1}, 1, "[보안 시험] 엉터리 키로 만든 SAFE 전환 명령"},
    {4, GC_CMD, OP_PING, {0}, 0, "생존 확인"},
};
#define PLAN_N (sizeof PLAN / sizeof PLAN[0])

/* 실험별 수신 현황 */
typedef struct {
    bool summary;
    tm_result_t res;
    uint16_t expect, ngot;
    bool got[PL_RAW_MAX];
    uint16_t ms[PL_RAW_MAX];
} expdata_t;

static struct {
    v3 pos, up;
    bool in_view, heard;
    int contact;
    double aos_t, max_el;
    int pass_ok, pass_bad, pass_cmds;
    size_t plan_pos;
    uint32_t next_seq;
    /* 응답 대기 중인 명령 */
    bool waiting, resend, cur_from_plan;
    uint8_t cur_op;
    double deadline, next_t;
    int tries;
    uint32_t wait_seq;
    uint8_t frame[PROTO_MAX_FRAME];
    size_t flen;
    /* 보안 시험 */
    uint8_t captured[PROTO_MAX_FRAME];
    size_t captured_len;
    uint8_t attack[PROTO_MAX_FRAME];
    size_t attack_len;
    int verify;                 /* 0=없음, 1=재전송 차단 확인, 2=인증 실패 확인 */
    uint16_t verify_base;
    double verify_deadline;
    int verify_tries;
    /* 갭 필링 */
    uint16_t gap_id;
    int gap_tries;
    expdata_t x[MAX_EXP + 1];
    /* 링크 */
    double el, range, gain_db;
    FILE *tm, *passes, *results;
    char steps_path[600];
    long rx_ok, rx_bad;
    tm_beacon_t last;
    bool have_last;
} G;

/* ---------------- 기하·링크 ---------------- */

static v3 geodetic(double lat, double lon, double h)
{
    const double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
    const double sl = sin(lat), cl = cos(lat);
    const double N = a / sqrt(1.0 - e2 * sl * sl);
    return v3_((N + h) * cl * cos(lon), (N + h) * cl * sin(lon), (N * (1.0 - e2) + h) * sl);
}

/* 턴스타일 안테나 (-Z면에 다이폴 4개, 원편파). c = 안테나 축(-Z)과 지상국 방향의 코사인
 * 정면 2.15 dBi, 옆 -0.9 dBi, 뒤쪽은 기체에 가려 최대 4 dB 더 손실 */
static double antenna_gain_db(double c)
{
    double db = 10.0 * log10(0.82 * (1.0 + c * c));
    if (c < 0.0) {
        db -= 4.0 * (-c);
    }
    return db;
}

static double link_ebn0(bool up)
{
    const double f = 437.0e6, c = 299792458.0, rb = 9600.0;
    const double fspl = 20.0 * log10(4.0 * PI * G.range * f / c);
    double p, gt, gr, tsys, loss;
    if (up) {   /* 지상 50 W → 위성. 위성은 넓은 지역의 지상 간섭을 듣는다: 간섭 여유 18 dB */
        p = 47.0; gt = 14.0; gr = G.gain_db; tsys = 1500.0; loss = 1.5 + 3.0 + 18.0;
    } else {    /* 위성 1 W → 지상. 케이블 1.5 dB, 지향·구현 4 dB, 지상 잡음 3.5 dB */
        p = 30.0; gt = G.gain_db; gr = 14.0; tsys = 600.0; loss = 1.5 + 4.0 + 3.5;
    }
    const double c_dbm = p + gt + gr - fspl - loss;
    const double n0_dbm = -198.6 + 10.0 * log10(tsys);
    return c_dbm - n0_dbm - 10.0 * log10(rb) + 2.0 * rng_n();   /* 프레임마다 페이딩 */
}

static double ber_fsk(double ebn0_db)
{
    const double x = pow(10.0, ebn0_db / 10.0);
    return 0.5 * exp(-0.5 * x);                                 /* 비동기 FSK */
}

static void corrupt(uint8_t *b, size_t n, double p)
{
    if (p < 1e-10) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        for (int k = 0; k < 8; k++) {
            if (rng_u() < p) {
                b[i] ^= (uint8_t)(1u << k);
            }
        }
    }
}

static void uplink(const uint8_t *buf, size_t n)
{
    uint8_t tmp[PROTO_MAX_FRAME];
    memcpy(tmp, buf, n);
    corrupt(tmp, n, ber_fsk(link_ebn0(true)));
    simhal_rx_push(tmp, n);
}

static size_t build_cmd(uint8_t op, const uint8_t *args, uint8_t nargs, uint32_t seq, const uint8_t *key,
                        uint8_t *out)
{
    frame_t f;
    f.type = FT_CMD;
    f.scid = SPACECRAFT_ID;
    f.seq = seq;
    f.len = (uint8_t)(1u + nargs);
    f.data[0] = op;
    memcpy(f.data + 1, args, nargs);
    return proto_encode(&f, key, out, PROTO_MAX_FRAME);
}

/* ---------------- 실험 데이터 ---------------- */

static uint16_t find_gap(void)
{
    if (!G.have_last) {
        return 0;
    }
    int n = G.last.exp_count;
    if (G.last.pl_state != PL_IDLE && n > 0) {
        n--;                                      /* 진행 중인 실험은 아직 데이터 없음 */
    }
    for (int id = 1; id <= n && id <= MAX_EXP; id++) {
        const expdata_t *e = &G.x[id];
        if (!e->summary || e->ngot < e->expect) {
            return (uint16_t)id;
        }
    }
    return 0;
}

static void on_payload(const frame_t *f)
{
    if (f->len < 1u) {
        return;
    }
    if (f->data[0] == PLD_SUMMARY) {
        tm_result_t x;
        if (!tm_result_unpack(f->data + 1, (size_t)f->len - 1u, &x) || x.exp_id == 0 || x.exp_id > MAX_EXP) {
            return;
        }
        expdata_t *e = &G.x[x.exp_id];
        if (e->summary) {
            return;                               /* 이미 받은 결과 */
        }
        e->summary = true;
        e->res = x;
        const unsigned n = (unsigned)x.steps_ok + x.steps_fail;
        e->expect = (uint16_t)(n > PL_RAW_MAX ? PL_RAW_MAX : n);
        simlog("지상", "  ← 실험 #%u 결과 [%s]: %s %u V, 디스펜스 %u/%u, 이동 %u칸 성공·재시도 %u·실패 %u,",
               x.exp_id, proto_plr_name(x.result), x.drive_ac ? "AC" : "DC", x.volts, x.disp_ok,
               x.disp_ok + x.disp_fail, x.steps_ok, x.steps_retry, x.steps_fail);
        simlog("지상", "        칸당 평균 %u ms (최대 %u ms), 왕복 %u/%u회, 누설전류 %u→%u nA, 칩 %d°C, %u초",
               x.step_ms_mean, x.step_ms_max, x.cycles_done, x.cycles_plan, x.leak_na_start, x.leak_na_end,
               x.chip_c, x.duration_s);
        fprintf(G.results, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u\n", x.exp_id, x.start_met, x.drive_ac,
                x.volts, x.cycles_plan, x.cycles_done, x.steps_ok, x.steps_retry, x.steps_fail, x.step_ms_mean,
                x.step_ms_max, x.leak_na_start, x.leak_na_end, x.chip_c, x.result, x.disp_ok, x.duration_s);
    } else if (f->data[0] == PLD_STEPS) {
        rbuf_t r;
        rb_init(&r, f->data + 1, (size_t)f->len - 1u);
        const uint16_t id = rb_u16(&r);
        const uint8_t off = rb_u8(&r);
        const uint8_t cnt = rb_u8(&r);
        if (r.err || id == 0 || id > MAX_EXP) {
            return;
        }
        expdata_t *e = &G.x[id];
        for (unsigned i = 0; i < cnt; i++) {
            const uint16_t ms = rb_u16(&r);
            const unsigned idx = off + i;
            if (r.err || idx >= PL_RAW_MAX) {
                break;
            }
            if (!e->got[idx]) {
                e->got[idx] = true;
                e->ms[idx] = ms;
                e->ngot++;
            }
        }
    }
}

/* ---------------- 수신 처리 ---------------- */

static void finish_command(void)
{
    G.waiting = false;
    G.resend = false;
    G.pass_cmds++;
    G.next_t = S.t + 1.0;
    if (G.cur_from_plan) {
        G.plan_pos++;
    }
    if (G.cur_op == OP_EXP_DOWNLINK) {
        const uint16_t gap = find_gap();
        if (gap != 0 && gap == G.gap_id) {
            G.gap_tries++;
        } else {
            G.gap_tries = 0;
        }
        G.gap_id = gap;
        if (gap != 0 && G.gap_tries < 4) {
            simlog("지상", "  실험 #%u 데이터 일부 손실 → 그 실험만 다시 요청", gap);
        }
    }
}

static void on_beacon(const tm_beacon_t *b)
{
    fprintf(G.tm, "%.1f,%u,%u,%u,%u,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.1f\n", S.t, b->met_s, b->mode,
            b->soc_pct, b->batt_mv, b->batt_ma, b->batt_c, b->pl_c, b->solar_mw, b->rate_cdps, b->boot_count,
            b->rx_ok, b->auth_fail, b->replay, b->last_seq, b->exp_count, b->pl_state, b->reset_cause, b->flags,
            G.el);
    if (!G.heard) {
        G.heard = true;
        simlog("지상", "  ← 비콘: %s, 배터리 %.2f V (%u%%), %d°C, 각속도 %.2f°/s, 부팅 %u회",
               proto_mode_name(b->mode), b->batt_mv / 1000.0, b->soc_pct, b->batt_c,
               b->rate_cdps == 0xFFFFu ? -1.0 : b->rate_cdps / 100.0, b->boot_count);
    }
    if (G.have_last && b->boot_count > G.last.boot_count) {
        simlog("지상", "  ⚠ 지난 교신 뒤 위성이 재부팅됨: %u → %u회 (원인: %s)", G.last.boot_count, b->boot_count,
               reset_cause_kr(b->reset_cause));
    }
    if (G.verify == 1 && b->replay > G.verify_base) {
        simlog("지상", "  ✓ 비콘 확인: 위성이 재전송 공격을 막았다 (차단 카운터 %u → %u)", G.verify_base, b->replay);
        G.verify = 0;
        G.plan_pos++;
        G.next_t = S.t + 1.0;
    } else if (G.verify == 2 && b->auth_fail > G.verify_base) {
        simlog("지상", "  ✓ 비콘 확인: 위성이 위조 명령을 버렸다 (인증 실패 카운터 %u → %u)", G.verify_base,
               b->auth_fail);
        G.verify = 0;
        G.plan_pos++;
        G.next_t = S.t + 1.0;
    }
    G.last = *b;
    G.have_last = true;
}

void gs_downlink(const uint8_t *buf, size_t n)
{
    if (!G.in_view || n > PROTO_MAX_FRAME) {
        return;
    }
    uint8_t tmp[PROTO_MAX_FRAME];
    memcpy(tmp, buf, n);
    corrupt(tmp, n, ber_fsk(link_ebn0(false)));
    frame_t f;
    if (proto_decode(tmp, n, SPACECRAFT_ID, NULL, &f) != PROTO_OK) {
        G.pass_bad++;
        G.rx_bad++;
        return;
    }
    G.pass_ok++;
    G.rx_ok++;
    switch (f.type) {
    case FT_BEACON: {
        tm_beacon_t b;
        if (tm_beacon_unpack(f.data, f.len, &b)) {
            on_beacon(&b);
        }
        break;
    }
    case FT_ACK: {
        rbuf_t r;
        rb_init(&r, f.data, f.len);
        const uint8_t op = rb_u8(&r);
        const uint8_t st = rb_u8(&r);
        const uint32_t seq = rb_u32(&r);
        simlog("지상", "  ← ACK %s: %s (seq %u)", proto_op_name(op), proto_ack_name(st), seq);
        if (G.waiting && seq == G.wait_seq) {
            finish_command();
        }
        break;
    }
    case FT_PAYLOAD:
        on_payload(&f);
        break;
    case FT_EVENTS: {
        simlog("지상", "  ← 이벤트 로그 %u건:", f.len / TM_EVENT_SIZE);
        for (size_t i = 0; i + TM_EVENT_SIZE <= f.len; i += TM_EVENT_SIZE) {
            tm_event_t e;
            if (tm_event_unpack(f.data + i, TM_EVENT_SIZE, &e)) {
                simlog_event("지상", e.met_s, e.id, e.a, e.b, true);
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------- 교신 운용 ---------------- */

void gs_init(const char *outdir)
{
    memset(&G, 0, sizeof G);
    G.pos = geodetic(GS_LAT * D2R, GS_LON * D2R, GS_ALT);
    G.up = v3_(cos(GS_LAT * D2R) * cos(GS_LON * D2R), cos(GS_LAT * D2R) * sin(GS_LON * D2R), sin(GS_LAT * D2R));
    G.next_seq = 1;
    char p[600];
    snprintf(p, sizeof p, "%s/gs_beacons.csv", outdir);
    G.tm = fopen(p, "w");
    snprintf(p, sizeof p, "%s/gs_passes.csv", outdir);
    G.passes = fopen(p, "w");
    snprintf(p, sizeof p, "%s/gs_results.csv", outdir);
    G.results = fopen(p, "w");
    snprintf(G.steps_path, sizeof G.steps_path, "%s/gs_steps.csv", outdir);
    if (!G.tm || !G.passes || !G.results) {
        fprintf(stderr, "출력 파일을 열 수 없습니다: %s\n", outdir);
        return;
    }
    fprintf(G.tm, "t,met,mode,soc,batt_mv,batt_ma,batt_c,pl_c,solar_mw,rate_cdps,boot,rx_ok,auth_fail,replay,"
                  "last_seq,exp_count,pl_state,reset_cause,flags,el\n");
    fprintf(G.passes, "contact,aos_t,los_t,max_el,rx_ok,rx_bad,cmds\n");
    fprintf(G.results, "exp_id,start_met,ac,volts,cycles_plan,cycles_done,steps_ok,steps_retry,steps_fail,"
                       "step_ms_mean,step_ms_max,leak_na_start,leak_na_end,chip_c,result,disp_ok,duration_s\n");
}

double gs_elevation(void) { return G.el; }

static void send_cmd(uint8_t op, const uint8_t *args, uint8_t nargs, const char *desc, bool from_plan)
{
    G.wait_seq = G.next_seq++;
    G.flen = build_cmd(op, args, nargs, G.wait_seq, CMD_KEY, G.frame);
    G.cur_op = op;
    G.cur_from_plan = from_plan;
    if (op == OP_EXP_SCHEDULE && G.captured_len == 0) {
        memcpy(G.captured, G.frame, G.flen);      /* 공격 시험용으로 "녹음" */
        G.captured_len = G.flen;
    }
    simlog("지상", "→ %s (seq %u): %s", proto_op_name(op), G.wait_seq, desc);
    G.waiting = true;
    G.tries = 1;
    G.deadline = S.t + ACK_TIMEOUT;
    uplink(G.frame, G.flen);
}

static void send_next(void)
{
    const gs_cmd_t *c = &PLAN[G.plan_pos];
    switch (c->kind) {
    case GC_CMD: {
        uint8_t args[8];
        memcpy(args, c->args, sizeof args);
        if (c->op == OP_SET_TIME) {
            const uint32_t utc = (uint32_t)(S.unix0 + S.t);
            for (int i = 0; i < 4; i++) {
                args[i] = (uint8_t)(utc >> (8 * i));
            }
        }
        send_cmd(c->op, args, c->nargs, c->desc, true);
        break;
    }
    case GC_REPLAY:
        simlog("지상", "→ %s", c->desc);
        memcpy(G.attack, G.captured, G.captured_len);
        G.attack_len = G.captured_len;
        G.verify = 1;
        G.verify_base = G.last.replay;
        break;
    case GC_FORGED: {
        static const uint8_t bad_key[16] = {0x13, 0x37, 0xC0, 0xFF, 0xEE, 0x00, 0xBA, 0xD0,
                                            0x5E, 0xC0, 0x4E, 0x77, 0x42, 0x99, 0x01, 0x02};
        simlog("지상", "→ %s", c->desc);
        G.attack_len = build_cmd(c->op, c->args, c->nargs, G.next_seq + 1000u, bad_key, G.attack);
        G.verify = 2;
        G.verify_base = G.last.auth_fail;
        break;
    }
    }
    if (G.verify) {
        G.verify_tries = 1;
        G.verify_deadline = S.t + 45.0;
        uplink(G.attack, G.attack_len);
    }
}

void gs_tick(void)
{
    const v3 sat = eci_to_ecef(S.r, S.gmst);
    const v3 rho = v3_sub(sat, G.pos);
    G.range = v3_norm(rho);
    G.el = asin(v3_dot(rho, G.up) / G.range) * R2D;
    /* 위성 안테나(-Z면)에서 본 지상국 방향 */
    const v3 los_b = q_rot_inv(S.q, ecef_to_eci(v3_scale(rho, -1.0 / G.range), S.gmst));
    G.gain_db = antenna_gain_db(-los_b.z);

    const bool vis = G.el >= GS_MIN_EL;
    if (vis && !G.in_view) {
        G.in_view = true;
        G.heard = false;
        G.contact++;
        G.aos_t = S.t;
        G.max_el = G.el;
        G.pass_ok = G.pass_bad = G.pass_cmds = 0;
        simlog("지상", "━━━━ 교신 #%d 시작 (대전 지상국 AOS) ━━━━", G.contact);
    } else if (!vis && G.in_view) {
        G.in_view = false;
        if (G.waiting) {
            G.waiting = false;
            G.resend = true;                      /* 다음 교신에서 같은 프레임으로 */
        }
        simlog("지상", "━━━━ 교신 #%d 종료 (LOS): %.1f분, 최고고도 %.0f°, 프레임 %d개 수신·%d개 CRC 오류, 명령 %d건 ━━━━",
               G.contact, (S.t - G.aos_t) / 60.0, G.max_el, G.pass_ok, G.pass_bad, G.pass_cmds);
        if (G.passes) {
            fprintf(G.passes, "%d,%.1f,%.1f,%.1f,%d,%d,%d\n", G.contact, G.aos_t, S.t, G.max_el, G.pass_ok,
                    G.pass_bad, G.pass_cmds);
        }
    }
    if (!vis) {
        return;
    }
    if (G.el > G.max_el) {
        G.max_el = G.el;
    }
    if (!G.heard || G.el < GS_CMD_EL || S.t < G.next_t) {
        return;
    }

    /* 보안 시험 결과 확인 중 */
    if (G.verify) {
        if (S.t >= G.verify_deadline) {
            if (G.verify_tries < 3) {
                G.verify_tries++;
                G.verify_deadline = S.t + 45.0;
                simlog("지상", "  비콘에서 차단 확인 안 됨 → 공격 프레임 다시 송신 (%d회째)", G.verify_tries);
                uplink(G.attack, G.attack_len);
            } else {
                simlog("지상", "  확인 실패, 다음 항목으로");
                G.verify = 0;
                G.plan_pos++;
            }
        }
        return;
    }

    /* 응답 대기 중 */
    if (G.waiting) {
        if (S.t >= G.deadline) {
            if (G.tries < MAX_TRIES) {
                G.tries++;
                G.deadline = S.t + ACK_TIMEOUT;
                simlog("지상", "  ACK 없음 → 같은 프레임 재전송 (%d회째)", G.tries);
                uplink(G.frame, G.flen);
            } else {
                simlog("지상", "  응답 없음 → 30초 쉬고 같은 프레임으로 다시");
                G.waiting = false;
                G.resend = true;
                G.next_t = S.t + 30.0;
            }
        }
        return;
    }
    if (G.resend) {
        simlog("지상", "→ 확인 못 한 명령 재전송 (seq %u, 번호가 같아 두 번 실행될 일 없음)", G.wait_seq);
        G.resend = false;
        G.waiting = true;
        G.tries = 1;
        G.deadline = S.t + ACK_TIMEOUT;
        uplink(G.frame, G.flen);
        return;
    }

    /* 빠진 실험 데이터 먼저 */
    if (G.gap_id != 0 && G.gap_tries < 4) {
        const uint8_t id = (uint8_t)G.gap_id;
        char d[64];
        snprintf(d, sizeof d, "실험 #%u 데이터 재요청", id);
        send_cmd(OP_EXP_DOWNLINK, &id, 1, d, false);
        return;
    }
    if (G.plan_pos < PLAN_N && PLAN[G.plan_pos].contact <= G.contact) {
        send_next();
    }
}

void gs_finish(void)
{
    simlog("지상", "지상국 합계: 프레임 %ld개 수신, %ld개는 CRC 오류로 폐기", G.rx_ok, G.rx_bad);
    FILE *f = fopen(G.steps_path, "w");
    if (f) {
        fprintf(f, "exp_id,ac,idx,ms\n");
        for (int id = 1; id <= MAX_EXP; id++) {
            const expdata_t *e = &G.x[id];
            for (unsigned i = 0; i < PL_RAW_MAX; i++) {
                if (e->got[i]) {
                    fprintf(f, "%d,%d,%u,%u\n", id, e->summary ? e->res.drive_ac : -1, i, e->ms[i]);
                }
            }
        }
        fclose(f);
    }
    if (G.tm) { fclose(G.tm); }
    if (G.passes) { fclose(G.passes); }
    if (G.results) { fclose(G.results); }
}
