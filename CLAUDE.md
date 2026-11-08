# CLAUDE.md

Claude Code는 이 파일을 세션 시작 때 자동으로 읽는다. 규칙은 다른 에이전트와 똑같이 유지하려고 AGENTS.md 한 곳에만 쓰고, 여기서는 불러오기만 한다.

@AGENTS.md
@team/STATUS.md
@team/DECISIONS.md

## Claude Code 전용

- `/sync`: 팀원 변경을 받아오고, 나에게 온 요청·새 결정·데이터 형식 변경을 요약한다.
- `/handoff`: 세션 마무리. STATUS.md 내 섹션과 DECISIONS.md를 갱신하고 커밋한다.
- 세션 도중 다른 팀원이 push한 내용은 자동으로 보이지 않는다. 오래 작업했다면 `/sync`를 다시 실행한다.
