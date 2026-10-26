# ScanReceiver — 작업 안내

복합기·스캐너의 **Scan to FTP**로 보낸 파일을 받는 Windows용 C++ Win32 GUI 앱. 사용 설명은 [README.md](README.md), 이 문서는 이어서 개발하는 사람(새 Claude 세션 포함)을 위한 것.

- 저장소: https://github.com/NamParkE/ScanReceiver (비공개, `main`)
- 로컬: `C:\Users\Administrator\WorkApps\ScanReceiver` (원래 `ScreenShot` 폴더 안에서 시작했다가 분리함 — ScreenShot(C# WPF 캡처 앱)과는 무관. 2026-10-26 `WorkApps` 아래로 옮김)

## 사용자 선호 (지킬 것)

- 답변·진행 안내는 **한국어**로.
- 불필요·생성 파일은 지우거나 추적 해제할 때 **같은 커밋에서 `.gitignore`도 갱신**.
- 디자인 변경은 먼저 **시안을 여러 개** 보여주고 고르게 함(아이콘도 3개 시안 → A안 → 모노톤 3종 → A2 실버로 확정).
- 사용자가 실행 중인 ScanReceiver는 **끄지 말 것**. `%APPDATA%\ScanReceiver\ScanReceiver.ini`에는 사용자의 실제 설정(저장 폴더 `Desktop\scan` 등)이 있으니 덮어쓰지 말 것 → 테스트는 `test\run_ftp_test.ps1`(`--ini <임시 파일>`로 실행해 사용자 설정을 안 건드림. 한 번에 하나만 실행되는 앱이라 사용자 앱이 켜져 있으면 거부).
- 화면 캡처는 **앱 창 영역만**. 창 사이 빈 곳에 사용자의 메일 등 개인 화면이 찍힌 적이 있음.

## 파일 구조

| 경로 | 내용 |
| --- | --- |
| `ftp_server.h/.cpp` | FTP 서버 (Winsock, 세션당 스레드). GUI와 무관 |
| `main.cpp` | GUI 전부: 메인 창, 받은 파일 창, 로그 창, 토스트, 트레이, 직접 그리는 컨트롤 |
| `app.rc`, `ScanReceiver.manifest`, `app.ico` | 아이콘, comctl32 v6, DPI 인식 |
| `build.bat` / `build_mingw.bat` / `CMakeLists.txt` | MSVC 빌드(검증됨) / MinGW·CMake(**미검증**) |
| `icon/` | 아이콘 SVG 원본 2개 + `build_icon.py`(Edge 헤드리스로 크기별 렌더 → `app.ico`) |
| `build/` | 앱 빌드 출력. git 무시. 테스트 산출물은 여기 두지 말 것 |
| `%APPDATA%\ScanReceiver\ScanReceiver.ini` | 사용자 설정(`ResolveIniPath`). `--ini <경로>`로 바꿀 수 있고, 예전 버전이 exe 옆에 만든 ini는 처음 실행할 때 여기로 옮김 |
| `test/` | 테스트 도구 전부: `ftp_test.py`(12개 FTP 검사), `run_ftp_test.ps1`(안전하게 실행), `wintool.cpp`(창 조작·화면 캡처 도구), `build_wintool.bat` |
| `test/out/` | `wintool.exe` 빌드 출력. git 무시 |

## 빌드·테스트

```bat
build.bat                                   :: MSVC (VS 2022 Build Tools, winget으로 설치됨)
python icon\build_icon.py                   :: SVG 수정 후 app.ico 재생성
pwsh -File test\run_ftp_test.ps1            :: 12/12 passed 여야 함 (앱 실행 중이면 거부)
test\build_wintool.bat                      :: 화면 확인용 도구 → test\out\wintool.exe
```

- 사용자가 앱을 실행 중이면 `build\ScanReceiver.exe`가 잠겨 링크가 실패함. 끄지 말고, 다른 출력 폴더로 컴파일해 검증(`cl ... /Fo<임시>\ /Fe<임시>\ScanReceiver.exe`)한 뒤 사용자에게 알릴 것.
- 화면 검증은 `wintool capture/area/burst`(화면 BitBlt). **PrintWindow는 강제로 다시 그려서** "마우스를 올려야 컨트롤이 그려지는" 버그를 가림(실제로 한 번 놓침).
- `wintool cmd <id>`의 숫자는 `main.cpp` 맨 위 enum 순서. 현재: 112 시작/중지, 113 복사, 114 로그, 115 받은 파일, 204 트레이 종료. enum을 고치면 번호가 밀림.
- 애니메이션은 `wintool burst`로 20~35ms 간격 연속 캡처 → 프레임별 변화량으로 이징 곡선과 유지 시간 확인(토스트 ≈ 270ms 등장 / 2.05s 유지 / 170ms 퇴장으로 측정됨).

## 구조 메모

**FTP 서버** (`ftp_server.cpp`)
- 인코딩: 인자가 유효한 UTF-8이면 UTF-8, 아니면 CP_ACP(한국 Windows = CP949)로 해석하고 이후 응답도 그 코드페이지로. `OPTS UTF8 ON`이면 UTF-8 고정.
- 경로: `NormalizeVirtual`로 `..`가 루트를 못 넘게 하고, `SanitizeSegment`로 NTFS 금지 문자 치환.
- 보안: PORT/EPRT는 제어 연결과 같은 IP만(바운스 차단), PASV 데이터 연결도 같은 IP만 수락, 로그인 3회 실패 시 종료.
- `Stop()`은 `Track`된 모든 소켓을 `shutdown`해서 블로킹 스레드를 깨움. 소켓 닫기는 반드시 `CloseTracked`.
- 파일 수신 완료(`STOR`)와 이름 변경(`RNTO`) 모두 `NotifyFile` → GUI의 `WM_APP_FILE`.

**GUI** (`main.cpp`, 섹션 순서대로)
- `Anim`: 레이어드 창 페이드+슬라이드, ease-out(나타남)/ease-in(사라짐), `timeBeginPeriod(1)`로 프레임 확보. 끝나면 레이어드 해제.
- 팝업(`PopupTargetPos`/`ShowPopup`/`HidePopup`): 받은 파일 창은 메인 창 오른쪽(없으면 왼쪽) 위쪽 맞춤, 로그 창은 받은 파일 창이 열려 있으면 그 아래.
- 토스트: 직접 그린 창(테마 따라 라이트/다크, DWM 둥근 모서리). 표시 중 새 파일이 오면 "N개 수신"으로 누적하고 2초 재시작, 마우스를 올리면 유지. `ToastMessage`로 일반 안내도 띄움.
- 직접 그리는 컨트롤: `BtnInfo`(Primary/Secondary/Subtle/Switch) + `ButtonSubclass`(호버 추적, 스위치는 BN_CLICKED 전에 토글) + `DrawButton`(GDI+ 안티앨리어싱, 글자는 GDI ClearType). `IsChecked/SetChecked`는 이 맵을 우선 사용.
- 입력칸: 테두리 없는 EDIT를 `PlaceField`가 30px 상자 안에 배치하고, `PaintMain`이 Fluent 텍스트 박스(포커스 시 2px 강조 밑줄)를 그림. `EN_SETFOCUS/KILLFOCUS`에서 다시 그림.
- 상태 칩은 `PaintMain`에서 그리고, 툴팁은 칩 사각형(`TTM_NEWTOOLRECT`)에 붙임.
- 색은 상단 Fluent 팔레트 상수(`kBg`, `kCard`, `kAccent`…)만 사용.

## 확정된 UI 결정

- 메인 창: 고정 크기 360×380 클라이언트. 세로 배치(한 줄에 한 항목) — 사용자가 "가로 간격 줄이고 줄 늘리기"를 요청해 확정.
- 1줄 상태 칩(+복사), 2줄 `받은 파일 N` · `로그` · `서버 시작/중지`(시작은 파란 Primary, 실행 중 중지는 Secondary), 아래 설정 카드.
- 받은 파일 목록과 로그는 메인 창에서 분리된 팝업. **창이 열려 있을 때 파일을 받으면 받은 파일 창만 자동으로** 뜸. 로그는 버튼으로만.
- 트레이에 있을 때(숨김/최소화) 파일을 받으면 **2초 토스트**. 등장·퇴장에 이징 애니메이션.
- **닫기(X)·Alt+F4·최소화 → 트레이**(스위치 `닫기·최소화 시 트레이로`가 켜져 있을 때). 실제 종료는 트레이 메뉴 `종료`. 실행마다 처음 숨길 때 안내 토스트 1회.
- 아이콘: 플랫베드 스캐너 + 수신 배지, **실버 모노톤(A2)**. 24px 이하는 단순화한 별도 SVG.

## 함정 (실제로 겪은 것)

- `WS_CLIPCHILDREN` 부모에서 `MoveWindow(..., FALSE)` 후 부모만 무효화하면 자식이 안 그려짐 → `RedrawWindow(..., RDW_ALLCHILDREN)`.
- `BS_GROUPBOX`는 안쪽 배경을 안 지워서 크기 변경 시 잔상 → 프레임은 부모가 직접 그림.
- 투명(NULL_BRUSH) static은 실제로 투명하지 않음(밑이 흰 사각형으로 남음) → 히트 영역이 필요하면 툴팁 사각형 도구 사용.
- `small`은 `rpcndr.h` 매크로(`char`), `RegisterClass`는 Win32 매크로 → 이름으로 쓰지 말 것.
- `ftp_server.h`가 `NOMINMAX`를 정의 → `gdiplus.h` 앞에 `namespace Gdiplus { using std::min; using std::max; }` 필요.
- 셸 도구: 긴 Python/여러 줄 코드는 heredoc 대신 파일로 써서 실행. PowerShell에서 `git commit -F -`에 here-string 파이프가 안 먹음 → 메시지 파일 사용. Windows PowerShell 5.1의 `Add-Type`은 임시 DLL 쓰기가 막혀 실패할 수 있음 → `wintool` 사용.
- 세션 임시 폴더(scratchpad)는 세션이 바뀌면 사라짐 → 남길 도구·스크립트는 저장소에 둘 것.

## 남은 작업 / 아이디어

- [ ] **실제 스캐너(복합기)로 전송 테스트** — 아직 한 번도 안 함. 방화벽 허용 여부도 실제 환경에서 확인 필요.
- [ ] 버튼 호버 색, 입력칸 포커스 밑줄은 코드만 있고 화면으로는 미확인.
- [ ] 다크 모드(현재 토스트만 테마를 따름).
- [ ] 받은 파일 목록·로그는 메모리에만 있음(재시작 시 비움) — 보관이 필요하면 파일로 저장.
- [ ] `build_mingw.bat`, `CMakeLists.txt` 빌드 검증.
- [ ] 비밀번호를 ini에 평문 저장 중(DPAPI 등으로 암호화 고려).
- [ ] FTP는 IPv4·평문만(FTPS 미지원).

## 작업 이력

| 날짜 | 내용 |
| --- | --- |
| 2026-10-20 | FTP 서버 + Win32 GUI 작성, MSVC 설치·빌드, FTP 테스트 12개 통과. GUI 1차 개선(상태 띠, 아이콘 목록, 로그 필터) |
| 2026-10-21 | 첫 화면 미표시 버그(자식 미갱신)·크기 변경 잔상 수정. ScreenShot 폴더에서 분리. 아이콘 시안 → 실버 확정, `app.ico` 생성. 컴팩트 GUI + 로그 팝업 + 트레이 토스트 + 이징, 받은 파일 창 분리, 세로 배치, Fluent 디자인, 닫기→트레이 |
| 2026-10-22 | GitHub 비공개 저장소 생성·푸시, 불필요 파일 정리(`fdbb926`) |
| 2026-10-26 | 이어 작업용 문서(`CLAUDE.md`)와 테스트 도구(`tests/`, `tools/`)를 저장소에 정리 → 같은 날 빌드 중간 파일 정리, 테스트 도구를 `test/` 한 폴더로 모으고 출력도 `test/out/`으로 분리. 저장소 공개 + v1.0.0 릴리즈. 설정 파일을 `%APPDATA%\ScanReceiver`로 옮김(기존 ini 자동 이전 확인) |
