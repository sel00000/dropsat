# AGENTS.md — 팀 공통 에이전트 규칙

이 파일은 팀원 4명이 쓰는 **모든 코딩 에이전트**(Claude Code, Codex, Cursor, Copilot 등)가 세션을 시작할 때 읽는 공통 진입점이다.
에이전트끼리는 대화를 공유하지 못한다. 그래서 **팀의 맥락은 전부 이 저장소의 파일로 주고받는다.** 사람용 설명은 [team/README.md](team/README.md).

## 이 저장소

- **DROPSAT-1**: 2U 큐브샛 비행 소프트웨어(C99)와 위성·지상국 시뮬레이터. 빌드·구성은 [README.md](README.md).
- **지금 하는 일**: 4인 팀으로 온라인 대회용 **웹사이트**를 만든다. 대회 정보와 만들 것은 [team/BRIEF.md](team/BRIEF.md).

## 세션 시작할 때

1. 최신 상태를 받는다: 작업 브랜치에서 `git fetch origin && git merge origin/main`.
2. 읽는다 (Claude Code는 `CLAUDE.md`가 ①②를 자동으로 불러온다):
   - ① [team/STATUS.md](team/STATUS.md) — 파트별 현재 상태, **내 역할 앞으로 온 요청**
   - ② [team/DECISIONS.md](team/DECISIONS.md) — 이미 정한 것. 다시 논쟁하지 않는다
   - ③ 데이터·차트를 다루면 [team/DATA_CONTRACT.md](team/DATA_CONTRACT.md)
   - ④ 화면 문구·용어를 쓰면 [team/GLOSSARY.md](team/GLOSSARY.md)
3. 누구를 돕는지 확인한다: `team/me.local.md`(커밋되지 않는 개인 파일)가 있으면 읽고, 없으면 사용자에게 역할을 묻는다.

## 역할과 담당 경로

| 역할 | 맡는 일 |
| --- | --- |
| `lead` | 리드·통합: 범위·일정, main 합치기·배포, 제출물·발표 |
| `ux` | UX/UI: 화면 구조, 와이어프레임, 디자인 토큰(색·글꼴·간격), 문구 톤 |
| `fe` | 프론트엔드: 페이지 구현, 차트·지도 시각화, 데이터 불러오기 |
| `data` | 데이터: NASA 위성 데이터 수집·정리, 시뮬레이션 데이터 변환, 수치 검증 |

사람 이름은 [team/BRIEF.md](team/BRIEF.md)의 팀원 표에 있다.

| 경로 | 담당 | 메모 |
| --- | --- | --- |
| `AGENTS.md`, `CLAUDE.md`, `.claude/`, `team/BRIEF.md` | lead | 규칙 변경은 팀 합의 후 |
| `team/STATUS.md` | 모두 | **자기 역할 섹션만** 고친다 |
| `team/DECISIONS.md` | 모두 | 맨 아래에 **추가만** 한다 |
| `team/DATA_CONTRACT.md` | data | 바꾸기 전에 fe와 합의 |
| `team/GLOSSARY.md` | 모두 | 추가는 자유, 기존 항목 변경은 data(뜻)·ux(표기) |
| `design/` | ux | 와이어프레임·시안, 디자인 토큰 |
| `web/` | fe | 웹 앱. 스타일·공용 UI 컴포넌트는 ux와 함께 (세부 경로는 스택 확정 후 갱신) |
| `web/public/data/` | data | 스크립트가 만드는 파일. **손으로 고치지 않는다** |
| `data/` | data | 수집·변환 스크립트 |
| `fsw/`, `sim/`, `tests/`, `tools/`, `Makefile`, `out/` | — | 기존 위성 SW와 원본 데이터. 웹 작업 중엔 **읽기 전용** |

남의 담당 경로를 고쳐야 하면 직접 고치지 말고, STATUS.md 내 섹션의 "요청"에 `→fe: …`처럼 적고 사용자에게 알린다. 오타·깨진 링크 정도는 예외.

## 반드시 지킬 것

1. **`out/`은 고정 원본이다 (D-002).** `make clean`, `make run`, `make plot`은 기본값(`OUT=out`)으로 `out/`을 지우거나 덮어쓰므로 그대로 실행하지 않는다. 필요하면 `OUT=build/…`처럼 다른 폴더를 준다. 같은 설정으로 다시 돌려도 환경에 따라 결과가 달라진다.
2. **화면의 숫자·사양에는 출처가 있어야 한다.** `out/` 데이터, 코드(예: `fsw/config.h`), DATA_CONTRACT.md에 등록된 NASA 데이터셋 중 하나를 가리킬 수 있어야 하며, 추측한 숫자를 쓰지 않는다. 시뮬레이션 결과는 화면에 **"시뮬레이션"**이라고 밝힌다.
3. **데이터 형식의 기준은 DATA_CONTRACT.md다.** 형식을 바꾸려면 그 문서를 먼저 고치고 버전을 올린 뒤 STATUS.md에 `→fe:`로 알린다. 화면 코드는 문서에 없는 필드를 기대하지 않는다.
4. **결정은 기록하고, 정해진 결정은 따른다.** 기술 선택, 의존성 추가, 폴더 구조, 화면 구성처럼 남에게 영향을 주는 결정은 DECISIONS.md에 남긴다. 기존 결정과 다르게 하고 싶으면 구현하지 말고 사용자에게 제안한다. 웹 스택이 정해지기 전(D-005)에는 웹 프로젝트를 새로 만들지 않는다.
5. **비밀 값은 커밋하지 않는다.** API 키(예: `NASA_API_KEY`)는 `.env`에 두고, 변수 이름만 `.env.example`에 적는다.
6. **충돌을 만들지 않는다.** 내가 고친 파일만 포맷한다(저장소 전체 포맷 금지). 파일 이동·이름 변경·대규모 리팩터링은 먼저 사용자에게 확인한다.

## 세션 끝날 때 (핸드오프)

사용자가 마무리·정리·핸드오프를 말하거나 작업 단위가 끝나면:

1. `team/STATUS.md`의 **내 역할 섹션**을 갱신한다: 업데이트 시각, 한 일, 다음, 막힘, 요청.
2. 새로 정한 것이 있으면 `team/DECISIONS.md` 맨 아래에 추가한다.
3. 데이터 형식을 바꿨으면 DATA_CONTRACT.md의 버전·변경 이력을, 새 용어가 생겼으면 GLOSSARY.md를 갱신한다.
4. 사용자 확인 후 커밋하고 push 한다.
5. 다음 사람이 알아야 할 것을 사용자에게 3줄 이내로 알려준다.

Claude Code에서는 `/sync`(시작), `/handoff`(마무리)로 이 절차를 실행할 수 있다.

## Git

- `main`은 항상 빌드되는 상태로 둔다. 작업은 `<역할>/<주제>` 브랜치에서 하고 PR로 합친다. 예: `ux/landing-wireframe`, `data/nasa-fetch`
- 커밋 메시지는 `[역할] 무엇을`. 예: `[data] 교신 CSV를 passes.json으로 변환`
- STATUS.md·DECISIONS.md에서 충돌이 나면 **양쪽 내용을 모두 살린다.**

## 명령어

```sh
make                              # 위성 SW 빌드 → build/ (C99 컴파일러 + GNU Make)
make test                         # 비행 SW 테스트 (133개 검사)
make run OUT=build/sim-local      # 시뮬레이터 재실행: 반드시 out/ 말고 다른 폴더로
```

웹·데이터 명령어는 스택이 정해지면(D-005) 여기에 추가한다.
