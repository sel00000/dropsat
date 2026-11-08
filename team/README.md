# 팀 협업 가이드

에이전트 4개가 각자 다른 세션에서 일해도 **같은 맥락**을 갖게 하는 방법입니다.

## 원리: 저장소가 팀의 공유 기억

에이전트는 다른 세션의 대화를 볼 수 없습니다. 대신 **저장소 파일은 모두가 봅니다.** 그래서

1. 팀 맥락(정한 것, 진행 상황, 데이터 형식, 용어)을 `team/` 파일로 적어 두고,
2. 모든 에이전트가 **시작할 때 읽고 끝날 때 갱신**하게 합니다. 이 규칙은 [AGENTS.md](../AGENTS.md)에 있고, 대부분의 에이전트가 자동으로 읽습니다.

| 파일 | 내용 | 누가 고치나 | 언제 |
| --- | --- | --- | --- |
| [AGENTS.md](../AGENTS.md) | 에이전트 공통 규칙, 역할별 담당 경로 | lead | 거의 안 바뀜 |
| [BRIEF.md](BRIEF.md) | 대회 정보, 만들 것, 팀원 | lead | 킥오프 + 가끔 |
| [STATUS.md](STATUS.md) | 파트별 지금 상태, 서로 요청 | 각자 자기 섹션 | **세션 끝날 때마다** |
| [DECISIONS.md](DECISIONS.md) | 정한 것과 이유 | 누구나 (추가만) | 정할 때마다 |
| [DATA_CONTRACT.md](DATA_CONTRACT.md) | 데이터 ↔ 화면 형식 약속, 원본 CSV 설명 | data | 형식 바뀔 때 |
| [GLOSSARY.md](GLOSSARY.md) | 위성 용어와 화면 표기 | 누구나 | 새 용어 나올 때 |

## 처음 한 번 (각자)

1. 저장소를 **자기 PC에** clone 합니다: `git clone https://github.com/sel00000/dropsat.git`
2. `team/me.local.md` 파일을 만들고 자기 역할을 적습니다. 커밋되지 않는 개인 파일이고, 에이전트가 누구를 돕는지 알게 해 줍니다.
   ```
   역할: ux
   이름: 홍길동
   ```
3. 에이전트 확인
   - Claude Code: 따로 할 것 없음. `CLAUDE.md`가 AGENTS.md·STATUS.md·DECISIONS.md를 자동으로 불러옵니다.
   - Codex, Cursor, GitHub Copilot 등: 대부분 AGENTS.md를 자동으로 읽습니다.
   - 자동으로 안 읽는 도구: 첫 메시지에 "AGENTS.md 먼저 읽고 시작해"라고 쓰세요.

## 매일 흐름

| 언제 | 할 일 | Claude Code |
| --- | --- | --- |
| 시작 | 최신 main 받기 → 나에게 온 요청·새 결정 확인 | `/sync` |
| 작업 중 | 남에게 영향 주는 결정은 DECISIONS에, 데이터 형식 변경은 DATA_CONTRACT부터 | |
| 마무리 | STATUS 내 섹션 갱신 → 커밋·push → PR | `/handoff` |

- 하루 **2번 이상** main에 합치세요. 오래 혼자 들고 있을수록 충돌이 커집니다.
- 다른 에이전트를 쓰는 사람은 "AGENTS.md의 '세션 끝날 때' 절차대로 마무리해줘"라고 하면 됩니다.

## VS Code Live Share는 이렇게 쓰세요

Live Share는 같이 보며 고치기엔 좋지만, **에이전트 작업의 기반으로 쓰면 문제가 생깁니다.**

- 게스트 쪽 파일은 호스트에서 그때그때 받아 보여 주는 가상 파일이라 게스트 PC 디스크에는 없습니다. 그래서 게스트가 자기 PC에서 돌리는 Claude Code 같은 에이전트는 호스트 파일을 읽지 못합니다.
- 호스트 폴더 하나를 에이전트 여럿이 동시에 고치면 서로의 변경을 덮어씁니다.

| 상황 | 방법 |
| --- | --- |
| 각자 맡은 작업 (에이전트와 함께) | 자기 clone + 자기 브랜치 |
| 회의, 화면·데이터 연결, 같이 디버깅, 최종 점검 | Live Share. 에이전트는 **호스트 한 명만** 사용 |

## 에이전트에게 이렇게 말해 보세요

- "나 ux야. STATUS.md에서 나한테 온 요청이랑 최근 결정 요약해 줘. 오늘은 메인 페이지 와이어프레임 잡자."
- "나 data야. DATA_CONTRACT.md 3절대로 `out/gs_passes.csv`를 `web/public/data/sim/passes.json`으로 바꾸는 스크립트 짜 줘."
- "나 fe야. DATA_CONTRACT.md 예시 모양으로 가짜 데이터 만들어서 배터리 차트부터 붙이자."
- "이거 DECISIONS.md에 남겨 줘: 차트 라이브러리는 ○○로."

## 킥오프 체크리스트

- [ ] [BRIEF.md](BRIEF.md) 채우기: 대회명, 마감, 제출물, 심사 기준, 팀원
- [ ] 역할 나누기 (lead / ux / fe / data)
- [ ] 웹 스택 정하기: [DECISIONS.md](DECISIONS.md) D-005 확정 후 AGENTS.md 명령어·경로 갱신
- [ ] 쓸 NASA 데이터 정하기: [DATA_CONTRACT.md](DATA_CONTRACT.md) 2절
- [ ] 주요 화면 확정 (BRIEF.md 화면 후보 참고)
- [ ] DECISIONS.md의 `제안` 항목 확정 또는 폐기
