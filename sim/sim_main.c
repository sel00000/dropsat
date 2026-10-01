/*
 * DROPSAT-1 비행 시뮬레이터
 *
 *   ./dropsat_sim [--hours 24] [--seed 7] [--seu 8] [--u0 160] [--out out]
 *
 *   --seu H : 사출 H시간 뒤 우주방사선 단일사건(SEU)으로 OBC를 멈추게 한다 (음수면 끔)
 *
 * 매 100 ms마다: 지상국 → 비행 소프트웨어 1틱 → 물리 적분
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fsw.h"
#include "sim.h"

static void usage(void)
{
    fprintf(stderr, "사용법: dropsat_sim [--hours H] [--seed N] [--seu H] [--u0 DEG] [--out DIR]\n");
}

static void do_reset(hal_reset_cause_t cause)
{
    S.cpu_up = false;
    S.cpu_hung = false;
    S.reset_req = false;
    S.cpu_down_until = S.t + 0.5;
    S.reset_cause = cause;
    S.resets++;
    simhal_power_on_defaults();
    simlog("시뮬", "OBC 리셋 (%s) → 0.5초 뒤 재부팅", reset_cause_kr(cause));
}

static void hms(char *o, size_t n, double t)
{
    if (t < 0.0) {
        snprintf(o, n, "없음");
        return;
    }
    const long s = (long)t;
    snprintf(o, n, "T+%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
}

int main(int argc, char **argv)
{
    double hours = 24.0, seu_h = 8.0, u0 = 160.0;
    unsigned long long seed = 7;
    const char *out = "out";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hours") && i + 1 < argc) {
            hours = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--seu") && i + 1 < argc) {
            seu_h = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--u0") && i + 1 < argc) {
            u0 = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    mkdir(out, 0755);

    char path[600];
    snprintf(path, sizeof path, "%s/mission_log.txt", out);
    simlog_open(path);
    snprintf(path, sizeof path, "%s/truth.csv", out);
    FILE *tc = fopen(path, "w");
    if (!tc) {
        fprintf(stderr, "%s 를 열 수 없습니다\n", path);
        return 1;
    }
    /* 전력은 10초 구간 평균 (순간값을 찍으면 30초마다 오는 송신 순간과 겹쳐 착시가 생긴다) */
    fprintf(tc, "t,mode,pl_state,soc,soc_est,v_batt,i_batt,p_avail,p_solar,p_load,rate_dps,rate_est_dps,"
                "Ts,Te,Tb,Tp,eclipse,gs_el,batt_heater,charge_en,cpu_up,mtq,v_trap,boot\n");
    double acc_avail = 0.0, acc_used = 0.0, acc_load = 0.0;
    int acc_n = 0;

    phys_init((uint64_t)seed, u0);
    gs_init(out);
    simlog("시뮬", "DROPSAT-1 (2U) 비행 시뮬레이션: %.0f시간, 고도 600 km 태양동기궤도 (경사 %.2f°)", hours,
           S.inc * R2D);
    simlog("시뮬", "사출 2026-10-10 00:00 UTC, 초기 텀블링 %.1f°/s, 배터리 %.0f%%", v3_norm(S.w) * R2D, S.soc * 100);

    fsw_init();
    S.wdt_last = 0.0;

    const long nsteps = (long)(hours * 3600.0 / SIM_DT + 0.5);
    bool seu_done = seu_h < 0.0;
    double t_nominal = -1.0;
    double soc_min = 1.0, soc_max = 0.0, tb_min = 1e9, tb_max = -1e9, ts_min = 1e9, ts_max = -1e9;
    double soc_err_max = 0.0;
    fsw_view_t fv;

    for (long k = 0; k < nsteps; k++) {
        S.k = k;
        S.t = (double)k * SIM_DT;

        gs_tick();

        S.tx_air = 0.0;
        if (S.cpu_up) {
            if (!seu_done && S.t >= seu_h * 3600.0) {
                seu_done = true;
                S.cpu_hung = true;
                simlog("시뮬", "⚡ 우주방사선 단일사건(SEU)이 OBC 메모리 비트를 뒤집음 → 프로그램 멈춤");
            }
            if (!S.cpu_hung) {
                fsw_tick();
            }
            if (S.reset_req) {
                do_reset(RESET_SOFTWARE);
            } else if (S.t - S.wdt_last > 2.0) {
                do_reset(RESET_WATCHDOG);                 /* 2초간 워치독을 못 건드리면 하드웨어 리셋 */
            }
        } else if (S.t >= S.cpu_down_until) {
            S.cpu_up = true;
            fsw_init();
            S.wdt_last = S.t;
        }

        phys_step(SIM_DT);

        if (S.cpu_up) {
            fsw_view(&fv);
        } else {
            fv.mode = 255;
        }
        if (t_nominal < 0.0 && S.cpu_up && fv.mode == MODE_NOMINAL) {
            t_nominal = S.t;
        }
        soc_min = fmin(soc_min, S.soc);
        soc_max = fmax(soc_max, S.soc);
        tb_min = fmin(tb_min, S.Tb);
        tb_max = fmax(tb_max, S.Tb);
        ts_min = fmin(ts_min, S.Ts);
        ts_max = fmax(ts_max, S.Ts);
        const bool soc_ok = S.cpu_up && fv.soc_valid;
        if (soc_ok) {
            soc_err_max = fmax(soc_err_max, fabs(fv.soc_est - S.soc));
        }

        acc_avail += S.p_avail;
        acc_used += S.p_used;
        acc_load += S.p_load;
        acc_n++;
        if (k % 100 == 0) {
            const double mtq = v3_norm(S.m_applied);
            fprintf(tc, "%.1f,%d,%d,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%d,%d,%d,%.4f,%.2f,%u\n",
                    S.t, S.cpu_up ? fv.mode : -1, S.cpu_up ? fv.pl_state : -1, S.soc, soc_ok ? fv.soc_est : NAN,
                    S.v_batt, S.i_batt, acc_avail / acc_n, acc_used / acc_n, acc_load / acc_n, v3_norm(S.w) * R2D,
                    (S.cpu_up && fv.rate_valid) ? fv.rate_est_rad * R2D : NAN, S.Ts, S.Te, S.Tb, S.Tp,
                    S.eclipse ? 1 : 0, gs_elevation(), S.rail[RAIL_BATT_HEATER] ? 1 : 0, S.charge_en ? 1 : 0,
                    S.cpu_up ? 1 : 0, mtq, S.ew.v_trap, S.cpu_up ? fv.boot_count : 0u);
            acc_avail = acc_used = acc_load = 0.0;
            acc_n = 0;
        }
    }

    char a[32], b[32], c[32];
    hms(a, sizeof a, S.first_burn_t);
    hms(b, sizeof b, S.first_tx_t);
    hms(c, sizeof c, t_nominal);
    simlog("시뮬", "──────── 요약 ────────");
    simlog("시뮬", "첫 열선 가열 %s, 첫 송신 %s → 45분 전 송신 %ld건 (CubeSat 규격 %s)", a, b, S.tx_early,
           S.tx_early == 0 ? "준수" : "위반");
    simlog("시뮬", "디텀블링 완료(NOMINAL 진입) %s, 현재 각속도 %.2f°/s", c, v3_norm(S.w) * R2D);
    simlog("시뮬", "배터리 SoC %.0f~%.0f%%, 추정 오차 최대 %.1f%%p, 배터리 %.1f~%.1f°C, 외피 %.1f~%.1f°C",
           soc_min * 100, soc_max * 100, soc_err_max * 100, tb_min, tb_max, ts_min, ts_max);
    simlog("시뮬", "발전 %.1f Wh, 소비 %.1f Wh, 송신 %ld프레임, OBC 리셋 %d회", S.e_solar_wh, S.e_load_wh, S.tx_frames,
           S.resets);
    gs_finish();
    fclose(tc);
    simlog_close();
    return 0;
}
