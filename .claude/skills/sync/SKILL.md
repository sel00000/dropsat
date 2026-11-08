---
name: sync
description: 팀원들의 최신 변경을 받아와 나에게 온 요청, 새 결정, 데이터 형식 변경을 요약한다. 세션 시작 때나 "동기화", "팀 상황", "뭐 바뀌었어" 같은 요청에 쓴다.
---

# 팀 동기화

1. 내 역할을 확인한다: `team/me.local.md`가 있으면 읽고, 없으면 사용자에게 묻는다 (lead / ux / fe / data).
2. `git status`로 커밋하지 않은 변경이 있는지 본다. 있으면 건드리지 말고 사용자에게 알린 뒤 계속할지 묻는다.
3. 병합 전 위치를 기억한다: `git rev-parse HEAD`.
4. `git fetch origin` 후 현재 브랜치에 `origin/main`을 병합한다. 충돌이 나면 멈추고 충돌 파일을 사용자에게 보여 준다. `team/STATUS.md`·`team/DECISIONS.md` 충돌은 양쪽 내용을 모두 살려 해결해도 된다.
5. 3번 위치 이후 바뀐 것을 확인한다: `git log --oneline <이전>..HEAD`, `git diff <이전>..HEAD -- team/ AGENTS.md`.
6. 다시 읽는다: `team/STATUS.md`, `team/DECISIONS.md`, 바뀌었으면 `team/DATA_CONTRACT.md`.
7. 사용자에게 짧게 보고한다:
   - **나에게 온 요청**: STATUS.md의 `→<내 역할>:` 줄 중 ✅가 없는 것
   - **새 결정**: DECISIONS.md에 새로 생기거나 상태가 바뀐 항목
   - **데이터 형식 변경**: DATA_CONTRACT.md 버전·변경 이력
   - 다른 파트 진행 상황 한 줄씩
   - 오늘 먼저 할 일 제안 1~3개
