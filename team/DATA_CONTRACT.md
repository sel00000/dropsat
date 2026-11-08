# 데이터 약속 (DATA_CONTRACT)

> 데이터(`data`)와 화면(`fe`·`ux`)이 **동시에** 일할 수 있게 하는 형식 약속이다.
>
> - 화면 코드는 이 문서에 적힌 파일·필드만 읽는다. 데이터 쪽은 이 형식대로 만든다.
> - 화면 쪽은 3절 예시 모양으로 가짜 데이터를 만들어 먼저 작업해도 된다.
> - 형식을 바꿀 땐 **이 문서를 먼저** 고치고, 버전을 올리고, 4절 변경 이력에 적고, STATUS.md에 `→fe:`로 알린다.
> - 담당: data · 현재 버전: **v0 (초안)**

## 1. 원본 A: DROPSAT-1 시뮬레이션 (`out/`)

**고정 원본이다 (D-002). 이 폴더는 수정하거나 다시 만들지 않는다.**

### 시나리오

출처: `out/mission_log.txt`, `sim/physics.c`, `sim/ground.c`, `fsw/config.h`

| 항목 | 값 |
| --- | --- |
| 위성 | DROPSAT-1, 2U 큐브샛, 2.2 kg |
| 궤도 | 고도 600 km 태양동기궤도, 경사 97.79°, 승교점 지방시 22:30 |
| 사출 시각 | 2026-10-10 00:00 UTC (시뮬레이션 시나리오의 **가정값**) |
| 기간 | 24시간 (t = 0 ~ 86 400 s) |
| 지상국 | 대전 (36.372°N, 127.360°E, 해발 100 m), UHF 437 MHz, 9600 bps. 고도각 5° 이상이면 교신, 10° 이상에서 명령 송신 |
| 주요 사건 | T+8h 우주방사선 단일사건(SEU)으로 OBC 멈춤 → 워치독 리셋 |
| 생성 조건 | 시뮬레이터 기본 설정 (`--hours 24 --seed 7 --seu 8 --u0 160`). README 기준 2026-10-01 Ubuntu WSL |

**시간 기준:** 모든 파일의 `t`는 **사출 후 경과 초**다. UTC가 필요하면 `2026-10-10T00:00:00Z + t초`. 화면에는 `T+HH:MM:SS`로 쓴다.

### `truth.csv`: 위성의 실제 상태 (시뮬레이터가 아는 정답), 10초 간격, 8 640행

| 열 | 뜻 | 단위·값 |
| --- | --- | --- |
| `t` | 사출 후 경과 시간 | s |
| `mode` | 운용 모드 | 0~3 (코드표), OBC가 꺼진 순간이면 -1 |
| `pl_state` | EWOD 탑재체 상태 | 0~6 (코드표), OBC가 꺼진 순간이면 -1 |
| `soc` | 배터리 잔량 (실제) | 0~1 |
| `soc_est` | 비행 SW가 추정한 배터리 잔량 | 0~1, 추정 못 하면 `nan` |
| `v_batt` | 배터리 전압 | V |
| `i_batt` | 배터리 전류 | A, + 충전 / − 방전 |
| `p_avail` | 태양전지가 낼 수 있는 전력 (10초 평균) | W |
| `p_solar` | 실제로 쓴 태양 전력 (10초 평균). 배터리가 차면 `p_avail`보다 작다 | W |
| `p_load` | 소비 전력 (10초 평균) | W |
| `rate_dps` | 실제 회전 각속도 | °/s |
| `rate_est_dps` | 비행 SW가 추정한 각속도 | °/s, 추정 못 하면 `nan` |
| `Ts` `Te` `Tb` `Tp` | 온도: 구조체(외피), 내부 전자장비, 배터리, EWOD 칩 | °C |
| `eclipse` | 지구 그림자 안 | 0/1 |
| `gs_el` | 대전 지상국에서 본 위성 고도각 | °, 음수면 지평선 아래 |
| `batt_heater` | 배터리 히터 | 0/1 |
| `charge_en` | 충전 허용 | 0/1 |
| `cpu_up` | OBC 동작 | 0/1 |
| `mtq` | 자기토커 출력 크기 | A·m² |
| `v_trap` | EWOD 유전막 포획전하 때문에 깎인 전압 | V |
| `boot` | 부팅 횟수 | OBC가 꺼져 있으면 0 |

주의: `truth.csv`의 `soc`는 0~1, `gs_beacons.csv`의 `soc`는 %다.

### `gs_beacons.csv`: 지상국이 받은 비콘 (위성이 보낸 상태 데이터), 66행

| 열 | 뜻 | 단위·값 |
| --- | --- | --- |
| `t` | 수신 시각 (사출 후) | s |
| `met` | 위성 시계(MET). 재부팅하는 동안 멈춰서 `t`보다 조금 늦을 수 있다 | s |
| `mode` | 운용 모드 | 0~3 |
| `soc` | 배터리 잔량 | % |
| `batt_mv` | 배터리 전압 | mV |
| `batt_ma` | 배터리 전류 | mA, + 충전 / − 방전 |
| `batt_c` `pl_c` | 배터리 / EWOD 칩 온도 | °C |
| `solar_mw` | 태양 전력 | mW |
| `rate_cdps` | 각속도 | 0.01 °/s, `65535`면 알 수 없음 |
| `boot` | 부팅 횟수 | |
| `rx_ok` | 위성이 정상 수신한 명령 프레임 수 (누적) | |
| `auth_fail` | 인증 실패로 버린 명령 프레임 수 (누적) | |
| `replay` | 재전송 공격으로 판단해 막은 프레임 수 (누적) | |
| `last_seq` | 마지막으로 받은 명령 번호 | |
| `exp_count` | 수행한 EWOD 실험 수 | |
| `pl_state` | 탑재체 상태 | 0~6 |
| `reset_cause` | 마지막 리셋 원인 | 0 전원 인가, 1 워치독, 2 소프트웨어, 3 저전압 |
| `flags` | 상태 비트 (더한 값) | 1 안테나 전개, 2 햇빛 받음, 4 배터리 히터, 8 충전 금지, 16 SAFE 고정, 32 자이로 정상, 64 시각 동기화, 128 자력계 정상 |
| `el` | 수신 순간 고도각 | ° |

### `gs_passes.csv`: 교신 4회

| 열 | 뜻 | 단위 |
| --- | --- | --- |
| `contact` | 교신 번호 | |
| `aos_t` `los_t` | 교신 시작 / 끝 (사출 후) | s |
| `max_el` | 최고 고도각 | ° |
| `rx_ok` `rx_bad` | 지상국이 받은 프레임 / CRC 오류로 버린 프레임 | 개 |
| `cmds` | 완료한 명령 수 | 건 |

### `gs_results.csv`: EWOD 실험 결과 요약, 2건

| 열 | 뜻 | 단위·값 |
| --- | --- | --- |
| `exp_id` | 실험 번호 | |
| `start_met` | 시작 시각 (위성 시계) | s |
| `ac` | 구동 방식 | 1 AC, 0 DC |
| `volts` | 구동 전압 | V (Vrms) |
| `cycles_plan` `cycles_done` | 계획 / 완료한 왕복 횟수 | 회 |
| `steps_ok` `steps_retry` `steps_fail` | 전극 한 칸 이동 성공 / 재시도 / 실패 | 회 |
| `step_ms_mean` `step_ms_max` | 한 칸 이동 시간 평균 / 최대 | ms |
| `leak_na_start` `leak_na_end` | 유전막 누설전류 시작 / 끝 | nA |
| `chip_c` | 칩 온도 | °C |
| `result` | 결과 | 코드표 (0 OK, 9 STUCK 등) |
| `disp_ok` | 디스펜스(저장조에서 액적 떼어내기) 성공 횟수 | 회 |
| `duration_s` | 실험 시간 | s |

### `gs_steps.csv`: 칸별 이동 시간, 121행

| 열 | 뜻 | 단위·값 |
| --- | --- | --- |
| `exp_id` | 실험 번호 | |
| `ac` | 구동 방식 | 1 AC, 0 DC, -1 요약을 못 받음 |
| `idx` | 이동 순번 | 0부터 |
| `ms` | 그 칸 이동 시간 | ms. **`65535`는 값이 아니라 이동 실패 표시**라 차트 축에 그리지 않는다 |

### `mission_log.txt`: 사건 로그 (사람이 읽는 문장)

- 형식: `[T+HH:MM:SS] 주체  내용`. 주체는 `시뮬`(시뮬레이터 연출·요약), `위성`(비행 SW 사건), `지상`(지상국 송수신).
- `MET hh:mm:ss`로 시작하는 `지상` 줄은 위성이 내려보낸 이벤트 로그의 사본이다. 같은 사건이 `위성` 줄에도 있으니 타임라인에 **두 번 넣지 않는다**.
- 시각은 초 단위로 내림한 값이다.

### 코드표

출처: `fsw/proto.h`, `fsw/proto.c`. 화면 표기는 [GLOSSARY.md](GLOSSARY.md).

- 모드 `mode`: 0 DEPLOY_WAIT, 1 DETUMBLE, 2 NOMINAL, 3 SAFE
- 탑재체 상태 `pl_state`: 0 IDLE, 1 ARMED, 2 PREHEAT, 3 HVCHECK, 4 DISPENSE, 5 TRANSPORT, 6 RETURN
- 실험 결과 `result`: 0 OK, 1 ABORT_POWER, 2 ABORT_RATE, 3 ABORT_TEMP, 4 ABORT_LEAK, 5 ABORT_CMD, 6 ABORT_MODE, 7 PREHEAT_TIMEOUT, 8 DISPENSE_FAIL, 9 STUCK, 10 TIMEOUT, 11 EXPIRED, 12 INTERRUPTED, 13 TRACK_BLOCKED

## 2. 원본 B: NASA 위성 데이터 (미정)

쓸 데이터셋을 정하면 데이터셋마다 아래 표를 복사해 채운다. 채우기 전에는 화면에 NASA 수치를 넣지 않는다.

| 항목 | 내용 |
| --- | --- |
| 이름 | |
| 제공처 URL | |
| 라이선스·출처 표기 문구 | 화면에 그대로 표기할 문구 |
| 받은 날짜 | 데이터가 갱신되면 화면 수치도 바뀌므로 꼭 적는다 |
| 범위 | 기간, 지역, 변수 |
| 원본 형식·크기 | |
| 받는 방법 | `data/`의 스크립트 경로. API 키가 필요하면 `.env`의 `NASA_API_KEY` |
| 결과 파일 | `web/public/data/nasa/<이름>.json` (형식은 3절에 추가) |

큰 원본 파일(수십 MB 이상)은 커밋하지 말고, 받는 스크립트를 커밋한다.

## 3. 웹용 데이터 파일 (v0 초안, data가 확정)

위치: `web/public/data/` (D-003). `data/`의 스크립트로 만들고 손으로 고치지 않는다.

### 공통 규칙

- UTF-8 JSON. 숫자는 숫자로 넣는다 (문자열 X).
- 빈 값은 `null`. **JSON에는 `NaN`이 없다.** CSV의 `nan`과 '알 수 없음' 값(`65535` 등)은 `null`로 바꾼다.
- 시간 필드는 `t` (사출 후 초).
- 필드 이름은 원본 CSV 열 이름을 그대로 쓴다. 단위는 1절을 따른다.
- 코드값에는 이름을 붙일 수 있다: `<열>_name` (예: `"result": 9, "result_name": "STUCK"`). 화면에 보일 한국어 표기는 화면 쪽이 GLOSSARY.md로 바꾼다.
- 최상위 모양: `{ "version": "v0", "source": "<원본 경로>", "rows": [ ... ] }`

### 파일 목록

| 파일 | 원본 | 내용 |
| --- | --- | --- |
| `sim/meta.json` | 1절 시나리오 | 위성·궤도·지상국 정보, 사출 UTC |
| `sim/telemetry.json` | `truth.csv` | 60초 간격으로 줄인 상태값 (1 440행) |
| `sim/beacons.json` | `gs_beacons.csv` | 그대로, `rate_cdps` 65535 → `null` |
| `sim/passes.json` | `gs_passes.csv` | 그대로 |
| `sim/experiments.json` | `gs_results.csv` + `gs_steps.csv` | 실험별 요약 + `steps` 배열 |
| `sim/events.json` | `mission_log.txt` | `{ t, who, text }`. `who`는 `sim`/`sat`/`gs`. MET 사본 줄 제외 |
| `nasa/*.json` | 원본 B | 미정 |

특정 구간을 더 촘촘히 봐야 하면(예: 처음 2시간을 10초 간격으로) STATUS.md에 `→data:`로 요청한다.

### 예시

아래 값은 실제 `out/` 데이터의 첫 행들이다.

`sim/meta.json`

```json
{
  "version": "v0",
  "is_simulation": true,
  "satellite": { "name": "DROPSAT-1", "size": "2U", "mass_kg": 2.2 },
  "orbit": { "type": "SSO", "altitude_km": 600, "inclination_deg": 97.79, "ltan": "22:30" },
  "launch_utc": "2026-10-10T00:00:00Z",
  "duration_s": 86400,
  "ground_station": { "name": "대전", "lat": 36.372, "lon": 127.360, "min_el_deg": 5 }
}
```

`sim/telemetry.json`

```json
{
  "version": "v0",
  "source": "out/truth.csv",
  "step_s": 60,
  "rows": [
    { "t": 0, "mode": 0, "pl_state": 0, "soc": 0.78, "soc_est": 0.793, "v_batt": 7.956, "i_batt": 0.66,
      "p_avail": 5.9, "p_solar": 5.9, "p_load": 0.65, "rate_dps": 11.1821, "rate_est_dps": null,
      "Ts": 18.0, "Te": 18.0, "Tb": 18.0, "Tp": 18.0, "eclipse": 0, "gs_el": -6.06,
      "batt_heater": 0, "charge_en": 1, "cpu_up": 1, "mtq": 0.0, "v_trap": 0.0, "boot": 1 }
  ]
}
```

`sim/passes.json`

```json
{
  "version": "v0",
  "source": "out/gs_passes.csv",
  "rows": [
    { "contact": 1, "aos_t": 5269.9, "los_t": 5850.0, "max_el": 30.6, "rx_ok": 23, "rx_bad": 2, "cmds": 4 }
  ]
}
```

`sim/experiments.json` (`steps`의 `ok: false`는 원본의 `65535`)

```json
{
  "version": "v0",
  "source": ["out/gs_results.csv", "out/gs_steps.csv"],
  "rows": [
    { "exp_id": 2, "start_met": 12317, "ac": 0, "volts": 110, "cycles_plan": 5, "cycles_done": 4,
      "steps_ok": 59, "steps_retry": 2, "steps_fail": 1, "step_ms_mean": 410, "step_ms_max": 1000,
      "leak_na_start": 2, "leak_na_end": 3, "chip_c": 21, "result": 9, "result_name": "STUCK",
      "disp_ok": 1, "duration_s": 581,
      "steps": [ { "idx": 0, "ms": 300, "ok": true }, { "idx": 59, "ms": null, "ok": false } ] }
  ]
}
```

`sim/events.json`

```json
{
  "version": "v0",
  "source": "out/mission_log.txt",
  "rows": [
    { "t": 1871, "who": "sat", "text": "모드 DEPLOY_WAIT → DETUMBLE" },
    { "t": 5269, "who": "gs", "text": "━━━━ 교신 #1 시작 (대전 지상국 AOS) ━━━━" }
  ]
}
```

## 4. 변경 이력

| 버전 | 날짜 | 내용 | 누가 |
| --- | --- | --- | --- |
| v0 | 2026-11-08 | 초안: 원본 A 열 사전, 웹용 파일 목록과 예시 | Claude (협업 구조 세팅) |
