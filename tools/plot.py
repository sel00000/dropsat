#!/usr/bin/env python3
"""DROPSAT-1 시뮬레이션 결과 그래프

    python3 tools/plot.py out        → out/mission_24h.png, out/early_ops_and_ewod.png
"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib import font_manager  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

# ---- 색 (참조 팔레트, 라이트 모드. 1~3번 범주색은 색각이상 검증 통과) ----
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
BASELINE = "#c3c2b7"
SHADE = "#efeee9"                               # 일식 띠
S1, S2, S3 = "#2a78d6", "#eb6834", "#1baf7a"
GOOD, CRITICAL = "#0ca30c", "#d03b3b"           # 상태색: 항상 기호·글자와 함께
MODE_FILL = {0: "#e1e0d9", 1: "#898781", 2: "#52514e", 3: "#ec835a"}
MODE_NAME = {0: "전개 대기", 1: "디텀블링", 2: "정상 운용", 3: "SAFE"}


def pick_font():
    names = {f.name for f in font_manager.fontManager.ttflist}
    for cand in ("Noto Sans CJK KR", "NanumGothic", "Malgun Gothic", "AppleGothic", "Noto Sans KR"):
        if cand in names:
            return cand
    return "DejaVu Sans"


plt.rcParams.update({
    "font.family": pick_font(),
    "font.size": 9.5,
    "axes.unicode_minus": False,
    "axes.facecolor": SURFACE,
    "figure.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
    "axes.edgecolor": BASELINE,
    "axes.linewidth": 0.8,
    "axes.labelcolor": INK2,
    "axes.titlesize": 10.5,
    "axes.titlecolor": INK,
    "axes.titleweight": "bold",
    "axes.titlelocation": "left",
    "axes.titlepad": 8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.6,
    "grid.linestyle": "-",
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "xtick.labelcolor": INK2,
    "ytick.labelcolor": INK2,
    "legend.frameon": False,
    "legend.fontsize": 8.5,
    "lines.linewidth": 1.4,
})


def load(path):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return None
    d = np.genfromtxt(path, delimiter=",", names=True, dtype=float, invalid_raise=False)
    return np.atleast_1d(d) if d.size else None


def segments(t, v):
    """같은 값이 이어지는 구간 (값, 시작, 끝)"""
    out, start = [], 0
    for i in range(1, len(v) + 1):
        if i == len(v) or v[i] != v[start]:
            out.append((v[start], t[start], t[min(i, len(v) - 1)]))
            start = i
    return out


def shade_eclipse(ax, t_h, ecl):
    for val, a, b in segments(t_h, ecl):
        if val == 1:
            ax.axvspan(a, b, color=SHADE, lw=0, zorder=0)


def smooth(y, n=6):
    return np.convolve(np.nan_to_num(y), np.ones(n) / n, mode="same")


def note(ax, xy, text, xytext=(8, 8), arrow=True, **kw):
    ax.annotate(text, xy, xytext=xytext, textcoords="offset points", fontsize=8.5, color=INK2,
                arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.8) if arrow else None,
                bbox=dict(boxstyle="round,pad=0.15", fc=SURFACE, ec="none"), **kw)


# ---------------------------------------------------------------- 24시간 개요

def fig_overview(out, T, B, P, R):
    t = T["t"] / 3600.0
    fig = plt.figure(figsize=(12, 13.6))
    gs = fig.add_gridspec(5, 1, height_ratios=[1.1, 1, 1, 1, 1.1], hspace=0.45,
                          left=0.08, right=0.85, top=0.875, bottom=0.05)
    axs = [fig.add_subplot(gs[i]) for i in range(5)]
    for a in axs[1:]:
        a.sharex(axs[0])
    fig.text(0.08, 0.963, "DROPSAT-1 하루 비행 시뮬레이션", fontsize=16, weight="bold", color=INK)
    fig.text(0.08, 0.941, "2U 큐브위성 · 고도 600 km 태양동기궤도 · 사출 2026-10-10 00:00 UTC · "
             "회색 띠 = 지구 그림자(일식)", fontsize=10, color=INK2)
    fig.text(0.08, 0.922, "선은 물리 모델의 실제값, 점은 대전 지상국이 비콘으로 받은 값. "
             "모드 전환과 실험 시작·중단은 모두 비행 소프트웨어가 스스로 판단했다.", fontsize=9, color=MUTED)

    # (1) 타임라인
    ax = axs[0]
    ax.set_title("운용 타임라인")
    ax.grid(False)
    ax.set_ylim(-0.75, 2.85)
    ax.set_yticks([2, 1, 0])
    ax.set_yticklabels(["모드", "대전 교신", "EWOD 실험"])
    ax.tick_params(axis="y", length=0)
    ax.spines["left"].set_visible(False)
    mode = np.where(T["cpu_up"] > 0, T["mode"], -1)
    segs = segments(t, mode)
    for val, a, b in segs:
        if val >= 0:
            ax.barh(2, b - a, left=a, height=0.62, color=MODE_FILL[int(val)], lw=0)
        if val == 2 and b - a > 2.5:
            ax.text((a + b) / 2, 2, "정상 운용 (NOMINAL)", ha="center", va="center", color="white", fontsize=8.5)
    boots = T["boot"]                                  # 부팅 횟수가 늘어난 곳 = 리셋
    for i in np.where((boots[1:] > boots[:-1]) & (boots[:-1] > 0))[0]:
        a = t[i + 1]
        ax.plot([a, a], [1.62, 2.4], color=CRITICAL, lw=2.0, zorder=4)
        ax.annotate("방사선(SEU)으로 멈춤 → 워치독 리셋, 3초 안에 재부팅", (a, 2.4), xytext=(4, 5),
                    textcoords="offset points", fontsize=8.5, color=INK, va="bottom")
    if P is not None:
        for row in P:
            a, b = row["aos_t"] / 3600, row["los_t"] / 3600
            ax.barh(1, b - a, left=a, height=0.45, color=INK, lw=0)
            ax.text((a + b) / 2, 1.33, f"#{int(row['contact'])} {row['max_el']:.0f}°", ha="center", va="bottom",
                    fontsize=8.3, color=INK2)
    run = (T["pl_state"] >= 2).astype(int)
    exps = [(a, b) for val, a, b in segments(t, run) if val == 1]
    for i, (a, b) in enumerate(exps):
        ok, label = True, f"#{i + 1}"
        if R is not None and i < len(R):
            ok = int(R[i]["result"]) == 0
            label = f"#{i + 1} {'AC' if int(R[i]['ac']) else 'DC'} " + ("✓" if ok else "× 멈춤")
        ax.barh(0, max(b - a, 0.08), left=a, height=0.45, color=GOOD if ok else CRITICAL, lw=0)
        ax.text(b + 0.12, 0, label, va="center", fontsize=8.5, color=INK)
    ax.legend(handles=[Patch(color=MODE_FILL[k], label=MODE_NAME[k]) for k in (0, 1, 2)], loc="upper left",
              bbox_to_anchor=(1.005, 1.0), title="모드", title_fontsize=8.5, handlelength=1.2)

    # (2) 각속도
    ax = axs[1]
    ax.set_title("각속도 |ω| (°/s, 로그 눈금) — 자기토커가 텀블링을 멈춘다")
    ax.semilogy(t, T["rate_dps"], color=S1, lw=1.3)
    ax.axhline(1.0, color=MUTED, lw=0.8, zorder=1)
    ax.text(24.15, 1.0, "디텀블 완료\n기준 1°/s", va="center", fontsize=8.3, color=INK2)
    ax.set_ylim(0.03, 40)
    note(ax, (0.5, T["rate_dps"][0]), f"사출 직후 {T['rate_dps'][0]:.0f}°/s로 텀블링", xytext=(14, 4), arrow=False)
    note(ax, (9.5, 0.42), "디텀블 뒤에는 궤도 각속도의 약 2배(0.12°/s)로 천천히 돈다 — B-dot의 정상 상태",
         xytext=(0, 0), arrow=False, ha="left", va="center")

    # (3) SoC
    ax = axs[2]
    ax.set_title("배터리 잔량 SoC (%)")
    shade_eclipse(ax, t, T["eclipse"])
    ax.plot(t, T["soc"] * 100, color=S1, lw=1.4, label="실제 (물리 모델)")
    ax.plot(t, T["soc_est"] * 100, color=S2, lw=1.1, label="비행 SW 추정")
    if B is not None:
        ax.plot(B["t"] / 3600, B["soc"], "o", ms=5, color=S3, mec=SURFACE, mew=1.2, label="지상국 수신 비콘", zorder=5)
    ax.set_ylim(70, 103)
    ax.legend(loc="upper left", bbox_to_anchor=(1.005, 1.0), handlelength=1.6)
    note(ax, (8.6, 86), "추정값이 100%에 붙는 구간: 정전압 충전이 끝나면 비행 SW가 100%로 맞춘다\n"
         "(실제 셀은 충전 전압 8.30 V에서 97%까지만 찬다)", xytext=(0, 0), arrow=False, ha="left", va="center")

    # (4) 전력
    ax = axs[3]
    ax.set_title("전력 (W, 1분 평균) — 배터리가 차면 남는 발전량은 버린다")
    shade_eclipse(ax, t, T["eclipse"])
    avail, load_w = smooth(T["p_avail"]), smooth(T["p_load"])
    ax.plot(t, avail, color=S1, lw=1.0, label="태양전지 발전 가능량")
    ax.plot(t, load_w, color=S2, lw=1.3, label="소비")
    ax.set_ylim(0, max(7.0, np.nanmax(avail) * 1.1))
    ax.legend(loc="upper left", bbox_to_anchor=(1.005, 1.0), handlelength=1.6)
    if exps:
        tx, ty = exps[-1][1] + 1.2, 1.9
        for i, (a, b) in enumerate(exps):
            k = np.argmax(load_w * ((t >= a) & (t <= b)))
            ax.annotate("실험 중 EWOD 칩 히터·고전압" if i == len(exps) - 1 else "", (t[k], load_w[k]),
                        xytext=(tx, ty), textcoords="data", fontsize=8.5, color=INK2, ha="left", va="center",
                        bbox=dict(boxstyle="round,pad=0.2", fc=SURFACE, ec="none"),
                        arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.8, shrinkA=0, shrinkB=2))

    # (5) 온도
    ax = axs[4]
    ax.set_title("온도 (°C)")
    shade_eclipse(ax, t, T["eclipse"])
    for key, name, col in (("Ts", "외피 구조체", S1), ("Tb", "배터리", S2), ("Tp", "EWOD 칩", S3)):
        ax.plot(t, T[key], color=col, lw=1.2, label=name)
    ax.axhline(0, color=MUTED, lw=0.8, zorder=1)
    ax.text(24.15, 0, "충전 금지\n0°C", va="center", fontsize=8.3, color=INK2)
    ax.legend(loc="upper left", bbox_to_anchor=(1.005, 1.0), handlelength=1.6)
    ax.set_xlabel("사출 후 시간 (h)")
    ax.set_xlim(0, 24)
    ax.set_xticks(range(0, 25, 2))
    for a in axs[:-1]:
        plt.setp(a.get_xticklabels(), visible=False)

    path = os.path.join(out, "mission_24h.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


# ---------------------------------------------------------------- 초기 운용 + EWOD

def fig_detail(out, T, S, R):
    fig = plt.figure(figsize=(13, 7.6))
    gs = fig.add_gridspec(2, 2, width_ratios=[1.05, 1], height_ratios=[1.6, 1], hspace=0.4, wspace=0.2,
                          left=0.06, right=0.97, top=0.84, bottom=0.14)
    fig.text(0.06, 0.945, "사출 직후 운용과 EWOD 실험 결과", fontsize=16, weight="bold", color=INK)
    fig.text(0.06, 0.912, "왼쪽: 사출부터 첫 교신까지 비행 소프트웨어가 혼자 처리한 일.   "
             "오른쪽: 지상국이 받은 실험 원시 데이터 (전극 한 칸마다 액적이 옮겨 가는 데 걸린 시간)",
             fontsize=10, color=INK2)

    m = T["t"] <= 100 * 60
    tm = T["t"][m] / 60.0

    ax = fig.add_subplot(gs[0, 0])
    ax.set_title("각속도 (°/s)")
    ax.plot(tm, T["rate_dps"][m], color=S1, lw=1.5, label="실제")
    ax.plot(tm, T["rate_est_dps"][m], color=S2, lw=1.0, label="자이로 추정 (비행 SW)")
    ax.set_ylim(0, 13.5)
    ax.set_xlim(0, 100)
    ax.legend(loc="lower left", bbox_to_anchor=(0.0, 0.02))
    events = [(30.0, "①"), (45.0, "②"), (59.7, "③"), (87.8, "④")]
    for x, num in events:
        ax.axvline(x, color=BASELINE, lw=0.8, zorder=1)
        ax.text(x, 12.9, num, ha="center", va="center", fontsize=10, color=INK,
                bbox=dict(boxstyle="round,pad=0.12", fc=SURFACE, ec="none"))
    ax.text(0.5, 0.66,
            "①  30분  안테나 열선 가열: 1회째 실패, 65초 뒤 2회째에 전개\n"
            "        → 바로 B-dot 디텀블링 시작\n"
            "②  45분  송신 금지 해제, 첫 비콘\n"
            "③  60분  1°/s 미만 5분 유지 → 정상 운용\n"
            "④  88분  대전 지상국 첫 교신",
            transform=ax.transAxes, fontsize=8.6, color=INK2, va="top", linespacing=1.6,
            bbox=dict(boxstyle="round,pad=0.5", fc=SURFACE, ec=GRID))
    plt.setp(ax.get_xticklabels(), visible=False)

    ax2 = fig.add_subplot(gs[1, 0], sharex=ax)
    ax2.set_title("자기토커 명령 크기 |m| (A·m²)")
    ax2.plot(tm, T["mtq"][m], color=S1, lw=1.0)
    ax2.axhline(0.2, color=MUTED, lw=0.8, zorder=1)
    ax2.text(1, 0.205, "축당 최대 0.2 A·m²", fontsize=8.3, color=INK2, va="bottom")
    ax2.set_ylim(0, 0.26)
    k = int(np.argmax(T["mtq"][m]))
    note(ax2, (tm[k], T["mtq"][m][k]), f"B-dot 시작 직후 {T['mtq'][m][k]:.2f} A·m²", xytext=(14, -6), arrow=False)
    note(ax2, (62, 0.012), "각속도가 줄면 명령도 0에 가까워진다", xytext=(0, 26), arrow=True, ha="left")
    ax2.set_xlabel("사출 후 시간 (분)")

    ax3 = fig.add_subplot(gs[:, 1])
    ax3.set_title("EWOD 액적 한 칸 이동 시간 — AC vs DC 구동, 110 Vrms")
    ax3.set_ylabel("이동 시간 (ms, 100 ms 단위로 측정)")
    if S is None:
        ax3.text(0.5, 0.5, "실험 데이터 없음", transform=ax3.transAxes, ha="center")
    else:
        runs = {}
        for row in S:
            runs.setdefault(int(row["exp_id"]), []).append((int(row["idx"]), int(row["ac"]), int(row["ms"])))
        stuck_y = 1150
        for eid, rows in sorted(runs.items()):
            rows.sort()
            ac = rows[0][1] == 1
            col = S1 if ac else S2
            idx = np.array([r[0] + 1 for r in rows])
            ms = np.array([r[2] for r in rows], dtype=float)
            ok = ms < 60000
            jitter = (-0.18 if ac else 0.18)
            ax3.plot(idx[ok] + jitter, ms[ok], "o", ms=3.6, color=col, alpha=0.55, mec="none", zorder=3)
            cx, cy = [], []
            for c in range(5):                        # 왕복 한 번(12칸)마다 평균
                sel = ok & (idx > 12 * c) & (idx <= 12 * c + 12)
                if sel.any():
                    cx.append(12 * c + 6.5)
                    cy.append(ms[sel].mean())
            name = f"실험 #{eid} {'AC' if ac else 'DC'} (선 = 왕복마다 평균)"
            ax3.plot(cx, cy, "-o", color=col, lw=2.0, ms=6, mec=SURFACE, mew=1.2, label=name, zorder=4)
            if (~ok).any():
                xs = idx[~ok][0]
                ax3.plot([xs], [stuck_y], "X", ms=10, color=CRITICAL, mec=SURFACE, mew=1.0, zorder=5)
                ax3.annotate("× 액적 멈춤\n재시도 2번 모두 실패", (xs, stuck_y), xytext=(-10, 0), textcoords="offset points",
                             ha="right", va="center", fontsize=8.5, color=INK)
        for c in range(1, 6):
            ax3.axvline(12 * c + 0.5, color=GRID, lw=0.9, zorder=1)
            ax3.text(12 * c - 5.5, 1262, f"왕복 {c}", ha="center", fontsize=8, color=MUTED)
        ax3.set_xlim(0, 63)
        ax3.set_ylim(0, 1300)
        ax3.set_yticks([0, 200, 400, 600, 800, 1000])
        ax3.set_xlabel("이동 순서 (E2→E8→E2 왕복 × 5, 마지막 칸은 저장조 복귀)")
        ax3.legend(loc="upper left", bbox_to_anchor=(0.0, 0.95))
        if R is not None:
            lines = []
            for r in R:
                drive = "AC" if int(r["ac"]) else "DC"
                status = "정상 종료" if int(r["result"]) == 0 else "멈춤"
                lines.append(f"#{int(r['exp_id'])} {drive}  {int(r['steps_ok'])}칸 성공 · 평균 {int(r['step_ms_mean'])} ms · "
                             f"재시도 {int(r['steps_retry'])} · 누설 {int(r['leak_na_start'])}→{int(r['leak_na_end'])} nA · {status}")
            ax3.text(0.02, 0.73, "\n".join(lines), transform=ax3.transAxes, fontsize=8.5, color=INK2, va="top",
                     linespacing=1.6, bbox=dict(boxstyle="round,pad=0.4", fc=SURFACE, ec=GRID))
        ax3.text(0.0, -0.15, "시뮬레이터 가정: DC 구동이면 유전막에 포획전하가 0.8 V/s로 쌓이고 AC면 0.01 V/s.\n"
                 "실제 값은 칩으로 측정해서 sim/ewod_sim.c에 넣어야 한다.", transform=ax3.transAxes,
                 fontsize=8, color=MUTED, va="top")

    path = os.path.join(out, "early_ops_and_ewod.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "out"
    T = load(os.path.join(out, "truth.csv"))
    if T is None:
        sys.exit(f"{out}/truth.csv 가 없습니다. 먼저 make run")
    B = load(os.path.join(out, "gs_beacons.csv"))
    P = load(os.path.join(out, "gs_passes.csv"))
    S = load(os.path.join(out, "gs_steps.csv"))
    R = load(os.path.join(out, "gs_results.csv"))
    print(fig_overview(out, T, B, P, R))
    print(fig_detail(out, T, S, R))


if __name__ == "__main__":
    main()
