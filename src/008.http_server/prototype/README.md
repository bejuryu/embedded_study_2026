# 컨트롤 패널 웹 UI 시뮬레이션 백엔드 사양서

`prototype.html` 주요 기능(파일 탐색, 업로드, 다운로드, 대역폭 측정)의 로컬 검증용 FastAPI 백엔드 명세서. 표준 RESTful API 규격 준수.

## 1. 개요 및 목적

- **이식 전 기능 검증**: ESP32-P4 환경 C++ 웹 서버 이식 전, 브라우저 측 API 연동 및 제어 로직 검증.
- **파일 시스템 및 네트워크 시뮬레이션**: 정적 모크(Mock) 방식 배제, 실제 소켓 통신 및 로컬 디렉토리 I/O 기반 파일 전송·대역폭 측정 신뢰성 확인.

---

## 2. 디렉토리 구조

기존 작업 디렉토리 하위 `prototype/` 폴더 내 독립 구성.

```
008.http_server/
├── prototype/
│   ├── README.md                     <- 본 문서
│   ├── main.py                       <- FastAPI 백엔드 스크립트
│   ├── mock_sd/                      <- 모의 SD 카드 디렉토리
│   └── static/
│       └── index.html                <- 경량화 및 검증 완료된 웹 UI 파일 (prototype.html 복사본)
```

---

## 3. REST API 명세 요약

시뮬레이션 백엔드 제공 REST API 명세. 자원(Resource) 식별 구조 통일 및 HTTP 메서드 규격화.

| 기능                 | HTTP 메서드 | 엔드포인트                | 요청 데이터 구조 및 헤더                                                                 | 반환값 (성공 시)                      |
| :------------------- | :---------: | :------------------------ | :--------------------------------------------------------------------------------------- | :------------------------------------ |
| 연결 상태 확인       |    `GET`    | `/api/status`             | 없음                                                                                     | `{"status": "ok"}` (`200 OK`)         |
| 자원 목록 조회       |    `GET`    | `/api/files`              | 쿼리 파라미터 `path` (상대 경로)                                                         | 디렉토리 및 파일 목록 JSON (`200 OK`) |
| 디렉토리 생성        |   `POST`    | `/api/files`              | `Content-Type: application/json`<br>Body: `{"name": "디렉토리명", "path": "부모경로"}`   | Location 헤더 반환 (`201 Created`)    |
| 파일 업로드          |   `POST`    | `/api/files`              | `Content-Type: application/octet-stream` (또는 기타 바이너리)<br>바이너리 바디 스트림 직접 전송 (쿼리 `path` 포함) | `201 Created`                         |
| 리소스 삭제          |  `DELETE`   | `/api/files`              | 쿼리 파라미터 `path` (대상 상대 경로)                                                    | 반환 본문 없음 (`204 No Content`)     |
| 파일 다운로드        |    `GET`    | `/api/files/content`      | 쿼리 파라미터 `path` (대상 파일 경로)                                                    | 바이너리 스트림 전송 (`200 OK`)       |
| 다운로드 대역폭 측정 |    `GET`    | `/api/speedtest/download` | 쿼리 파라미터 `size` (전송 바이트 크기)                                                  | Null 바이트 비동기 전송 (`200 OK`)    |
| 업로드 대역폭 측정   |   `POST`    | `/api/speedtest/upload`   | 없음                                                                                     | 누적량 계측 후 데이터 폐기 (`200 OK`) |

---

## 4. API 상세 명세 및 동작 정의

### A. 서버 연결 확인 (`GET /api/status`)

- 클라이언트-백엔드 활성화 체크용 하트비트 API.
- HTTP 상태 코드 `200 OK` 및 `{"status": "ok"}` 반환.

### B. 자원 목록 조회 (`GET /api/files`)

- 지정 디렉토리 하위 자원 목록 탐색 및 정렬 반환.
- **물리 경로 매핑**: 요청 `path` 값은 `mock_sd/` 디렉토리 기준 상대 경로로 매핑.
- **응답 페이로드 스키마**:

  ```json
  {
    "files": [
      { "name": "..", "is_dir": true, "size": 0 },
      { "name": "work_dir", "is_dir": true, "size": 0 },
      { "name": "sensor_fusion.bin", "is_dir": false, "size": 4194304 }
    ]
  }
  ```

### C. 디렉토리 생성 및 파일 업로드 (`POST /api/files`)

- 단일 엔드포인트에서 `Content-Type` 헤더 기준 분기 처리.
  - **디렉토리 생성 (`application/json`)**: `mock_sd/` 하위 지정 경로에 물리 디렉토리 생성. 성공 시 `Location` 헤더에 생성 경로 첨부 및 `201 Created`
    반환.
  - **파일 업로드 (Raw Binary Streaming)**: 수신 바이너리 바디 스트림을 `mock_sd/path` 경로(쿼리 `path`로 지정된 전체 경로)에 파일로 직접 기록 후 `201 Created` 반환.

### D. 리소스 삭제 (`DELETE /api/files`)

- `mock_sd/` 내부 대상 파일 또는 디렉토리 제거.
- 완료 후 응답 본문 없이 `204 No Content` 반환.

### E. 파일 다운로드 (`GET /api/files/content`)

- 메타데이터 조회와 데이터 스트림 획득을 논리 격리하기 위해 sub-resource 경로(`/content`) 사용.
- 브라우저 다운로드 대화창 구동 강제를 위해 `Content-Disposition: attachment; filename="..."` 및 `Content-Type: application/octet-stream`
  헤더 지정 전송.

### F. 다운로드 대역폭 측정 (`GET /api/speedtest/download`)

- 웹 UI 다운로드 대역폭 측정 흐름 구동용 API.
- 요청 `size` 만큼 64KB 단위 Null 바이트(`\x00`) 데이터 생성 및 비동기 `StreamingResponse` 순차 송신.

### G. 업로드 대역폭 측정 (`POST /api/speedtest/upload`)

- 웹 UI 송신 대용량 스트림 수신.
- 백엔드 디스크 쓰기 병목으로 인한 대역폭 왜곡 방지를 위해, 네트워크 수신 청크의 바이트 수만 누적 계측 후 즉시 메모리 해제(디스크 기록 제외).

---

## 5. 단일 프로세스 듀얼 포트 구동 및 공통 함수 설계

리소스 상태 공유 효율성 극대화 및 코드 중복 최소화를 위한 백엔드 구조적 설계 지침.

- **비동기 듀얼 포트 동시 제어 (Asyncio.gather)**:
  - 물리적으로 독립된 두 개의 포트(HTTP `8000`, HTTPS `8443`)를 하나의 파이썬 프로세스 내 단일 이벤트 루프로 동시 실행.
  - 메모리(힙 영역) 격리 배제를 통한 실시간 측정 이력(History) 데이터의 무상태 공유 및 동기화 오버헤드 원천 차단.
- **물리 경로 및 이탈 보안 검증 공통화 (`get_safe_path`)**:
  - API 라우터들의 입력 파라미터(`path`, `name`)를 실제 OS 물리 경로로 변환하는 로직 공통 헬퍼 함수화.
  - 상위 디렉토리 참조 공격(Path Traversal) 방어 필터링 단일화.
- **서버 인스턴스 기동 표준화 (`run_server`)**:
  - HTTP 및 HTTPS 포트별 Uvicorn Config 설정 및 비동기 소켓 리스너 실행 제어 로직 일원화.

---

## 6. 다국어(한글) 자원 식별 지원 정책

웹 UI-백엔드 간 다국어 자원 식별자 전송 시 문자 깨짐 현상 방지 메커니즘.

- **클라이언트 (웹 UI)**: 경로 및 파일명 파라미터는 UTF-8 Percent-Encoding(`encodeURIComponent`) 후 송신.
- **시뮬레이션 백엔드 (FastAPI)**: ASGI 파서 엔진으로 자동 UTF-8 디코딩 후 `mock_sd/` 내부 실제 파일 시스템에 한글 명칭 반영.
- **임베디드 타겟 (ESP32-P4 / C++) 이식 핵심 지침**:
  - C++ 핸들러 진입 시 퍼센트 디코딩 처리 함수(`url_decode`) 구현 내재화 필요.
  - ESP-IDF FATFS 설정 내 긴 파일명(LFN) 활성화 및 API 인코딩 UTF-8(`CONFIG_FATFS_API_ENCODING_UTF_8`) 지정 필수.
  - 파일 다운로드 시 브라우저 한글 깨짐 방지를 위해 `Content-Disposition` 헤더에 RFC 5987 규격(`filename*=UTF-8''...`) 필수 구현.

---

## 7. 검증 및 구동 시나리오

1. **사전 준비**:
   - Python 3.9+ 가상환경(`.venv`) 생성 및 필수 종속성(`fastapi`, `uvicorn`, `python-multipart`, `aiofiles`) 설치.
   - `prototype/mock_sd/` 하위 테스트용 디렉토리 및 파일 수동 배치.
2. **백엔드 구동**: `uvicorn main:app --host 0.0.0.0 --port 8000` 실행.
3. **웹 UI 접속**: 동일 네트워크 대역 내 브라우저로 `http://[서버 IP]:8000/` 접속.
4. **동작 검증**:
   - 디렉토리 생성, 파일 업로드, 파일 다운로드, 리소스 삭제 동작 수행 후 `mock_sd/` 실제 파일 싱크 일치 확인.
   - 대용량(50MB, 100MB) 대역폭 측정 수행 시 CPU 자원 소모 및 진행률 표시 정합성 계측.
