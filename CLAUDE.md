# ScanReceiver — 작업 안내

복합기·스캐너의 **Scan to FTP**로 보낸 파일을 받는 Windows용 C++ Win32 GUI 앱. 사용 설명은 [README.md](README.md), 이 문서는 이어서 개발하는 사람(새 Claude 세션 포함)을 위한 것.

- 저장소: https://github.com/NamParkE/ScanReceiver (**공개**, `main`). 릴리즈: v1.0.0 (2026-10-26, 위젯 UI 이전 버전)
- 로컬: `C:\Users\Administrator\WorkApps\ScanReceiver` (원래 `ScreenShot` 폴더 안에서 시작했다가 분리함 — ScreenShot(C# WPF 캡처 앱)과는 무관. 2026-10-26 `WorkApps` 아래로 옮김)
- 형제 프로젝트: `..\QuickLinks`(바로가기 위젯, 같은 C++ Win32), `..\ScreenShot`(C# WPF). **UI·아이콘은 QuickLinks와 같은 디자인 언어**(Fluent 단색, 36px 머리글, `#005FB8` 강조, 파란 둥근 사각형 + 흰 기호 아이콘)로 맞춤.

## 사용자 선호 (지킬 것)

- 답변·진행 안내는 **한국어**로.
- 불필요·생성 파일은 지우거나 추적 해제할 때 **같은 커밋에서 `.gitignore`도 갱신**.
- 디자인 변경은 먼저 **시안을 여러 개** 보여주고 고르게 함(2026-10-26: 위젯 4안 → B, 폴더 버튼 위치 3안 → B3, 설정 창 3안 → S3, 토스트 4안 → T4, 아이콘 4안 → R3).
- 사용자가 실행 중인 ScanReceiver는 **끄지 말 것**(끄거나 exe를 바꿔야 하면 먼저 물어볼 것). `%APPDATA%\ScanReceiver\ScanReceiver.ini`에는 사용자의 실제 설정(저장 폴더 `Desktop\scan` 등)이 있으니 덮어쓰지 말 것 → 테스트는 항상 `--ini <임시 파일>`로 띄운 **테스트 인스턴스**로.
- 화면 캡처는 **앱 창 영역만**. 창 사이 빈 곳에 사용자의 메일 등 개인 화면이 찍힌 적이 있음. 캡처 파일은 확인 후 지울 것.

## 파일 구조

| 경로 | 내용 |
| --- | --- |
| `ftp_server.h/.cpp` | FTP 서버 (Winsock, 세션당 스레드). GUI와 무관 |
| `main.cpp` | GUI 전부: 위젯(메인 창), 받은 파일 목록, 설정 창, 로그 창, 토스트, 트레이, 시작 시 실행, 직접 그리는 컨트롤 |
| `app.rc`, `ScanReceiver.manifest`, `app.ico` | 아이콘, comctl32 v6, DPI 인식 |
| `build.bat` / `build_mingw.bat` / `CMakeLists.txt` | MSVC 빌드(검증됨) / MinGW·CMake(**미검증**) |
| `icon/` | 아이콘 SVG 원본 2개(24px 이하용 `-small`) + `build_icon.py`(PyMuPDF로 크기별 렌더 → `app.ico`) |
| `build/` | 앱 빌드 출력(`ScanReceiver.exe`만 남길 것). git 무시. 테스트 산출물은 여기 두지 말 것 |
| `%APPDATA%\ScanReceiver\ScanReceiver.ini` | 사용자 설정(`ResolveIniPath`). `--ini <경로>`로 바꿀 수 있고, 예전 버전이 exe 옆에 만든 ini는 처음 실행할 때 여기로 옮김. 위젯 위치(`WindowX/Y`)도 저장 |
| `HKCU\...\CurrentVersion\Run` 값 `ScanReceiver` | "Windows 시작 시 실행". `"<exe>" --startup`(트레이로 시작). exe를 옮기면 다음 실행 때 경로를 고침(`RefreshRunAtLogon`) |
| `test/` | 테스트 도구 전부: `ftp_test.py`(12개 FTP 검사), `run_ftp_test.ps1`(테스트 인스턴스로 실행), `wintool.cpp`(창 조작·화면 캡처 도구), `build_wintool.bat` |
| `test/out/` | `wintool.exe` 빌드 출력. git 무시 |

## 빌드·테스트

```bat
build.bat                                   :: MSVC (VS 2022 Build Tools, winget으로 설치됨)
python icon\build_icon.py                   :: SVG 수정 후 app.ico 재생성 (pillow, pymupdf 필요)
pwsh -File test\run_ftp_test.ps1            :: 12/12 passed 여야 함. 포트 2199, 사용자 앱이 켜져 있어도 됨
test\build_wintool.bat                      :: 화면 확인용 도구 → test\out\wintool.exe
```

- **테스트 인스턴스**: `--ini <경로>`로 띄우면 단일 인스턴스 검사를 건너뛰고(사용자 앱 옆에서 같이 실행), 창 제목이 `ScanReceiver (test)`, 머리글이 "스캔 수신 (테스트)", 레지스트리(시작 시 실행)를 건드리지 않고, 포커스를 가져가지 않음. 포트는 사용자 설정(2121)과 겹치지 않게.
- `wintool`은 테스트 인스턴스 창을 먼저 찾고 없으면 사용자 창을 찾음. `wintool rect <창 클래스>` + `wintool area`로 설정 창(`ScanReceiverSettings`)·토스트(`ScanReceiverToast`)도 캡처.
- 사용자가 앱을 실행 중이면 `build\ScanReceiver.exe`가 잠겨 링크가 실패함. 끄지 말고, 다른 출력 폴더로 컴파일해 검증한 뒤(확인 후 삭제) 사용자에게 알릴 것.
- 화면 검증은 `wintool capture/area/burst`(화면 BitBlt). **PrintWindow는 강제로 다시 그려서** "마우스를 올려야 컨트롤이 그려지는" 버그를 가림(실제로 한 번 놓침).
- `wintool cmd <id>`의 숫자는 `main.cpp` 맨 위 enum. 현재: 100 서버 켜기/끄기, 101 주소 복사, 190 스캐너 설정 카드 표시(테스트 인스턴스만), 102 설정 창, 103 닫기(트레이로), 104 폴더 열기, 105 로그, 202 목록 지우기, 203 트레이 열기, 204 트레이 설정, 205 트레이 로그, 206 종료. enum을 고치면 번호가 밀림.
- 애니메이션은 `wintool burst`로 20~35ms 간격 연속 캡처 → 프레임별 변화량으로 이징 곡선과 유지 시간 확인.
- 마우스로만 확인할 수 있는 것(호버, 머리글 끌기, 휠 스크롤, 토스트 링크 클릭)은 사용자에게 부탁.

## 구조 메모

**FTP 서버** (`ftp_server.cpp`)
- 인코딩: 인자가 유효한 UTF-8이면 UTF-8, 아니면 CP_ACP(한국 Windows = CP949)로 해석하고 이후 응답도 그 코드페이지로. `OPTS UTF8 ON`이면 UTF-8 고정.
- 경로: `NormalizeVirtual`로 `..`가 루트를 못 넘게 하고, `SanitizeSegment`로 NTFS 금지 문자 치환.
- 보안: PORT/EPRT는 제어 연결과 같은 IP만(바운스 차단), PASV 데이터 연결도 같은 IP만 수락, 로그인 3회 실패 시 종료.
- `Stop()`은 `Track`된 모든 소켓을 `shutdown`해서 블로킹 스레드를 깨움. 소켓 닫기는 반드시 `CloseTracked`.
- 파일 수신 완료(`STOR`)와 이름 변경(`RNTO`) 모두 `NotifyFile` → GUI의 `WM_APP_FILE`.

**GUI** (`main.cpp`, 섹션 순서대로)
- 설정은 `Settings g.cfg`가 기준(`LoadSettings`/`SaveSettings`가 ini와 주고받음). 서버 시작은 `g.cfg`로, 설정 창 입력칸은 열 때 채우고 `저장`할 때만 `g.cfg`에 반영.
- 위젯·설정 창은 **테두리 없는 창**: `WS_POPUP | WS_THICKFRAME` + `WM_NCCALCSIZE`에서 0 → DWM이 둥근 모서리·테두리·그림자. 머리글(36px)은 `HTCAPTION`(끌어서 이동), 크기 조절 없음. 위젯 머리글 우클릭 = 트레이 메뉴.
- 위젯 레이아웃 상수는 `kW`, `kHeaderH`, `kCardTop`… (260×300). 상태 카드는 `PaintMain`이 그리고, 스위치·복사·톱니·✕·아래 링크 버튼만 자식 창.
- 받은 파일 목록은 직접 그린 자식 창(`ListProc`, 클래스 `ScanReceiverFileList`): 30px 줄 4개, 휠 스크롤(위젯 어디서 굴려도 목록으로 전달), 더블클릭 열기, 우클릭 메뉴(열기/폴더에서 보기/목록 지우기). 데이터는 `g.files`(최신이 앞).
- **글자는 DirectWrite**(QuickLinks와 같음): `MakeFont`가 GDI 글꼴(입력칸·로그 목록 같은 기본 컨트롤용)과 같은 크기의 DirectWrite 텍스트 형식을 짝지어 만들고, 손으로 그리는 글자는 전부 `DrawTextIn`/`TextWidth`가 공용 `ID2D1DCRenderTarget`(96 DPI, 크기는 px)으로 그림. 글꼴은 Segoe UI Variable Text(없으면 Segoe UI) + 한글은 맑은 고딕 자동 대체, 14px 본문 / 12px 보조, 12px 미만 금지. 2026-10-26 GDI 맑은 고딕이 얇고 각져 "밤티" 난다는 의견 → 5가지 글꼴·방식 비교 후 사용자가 이 조합 선택(프리텐다드는 기본 Windows에 없어서 제외). 설정 창 이름표도 STATIC 대신 직접 그림(`g.setLabels`).
- 아이콘 글리프는 `Segoe Fluent Icons`(없으면 `Segoe MDL2 Assets`, 코드 포인트 같음), ClearType 색 번짐 때문에 그레이스케일.
- 상태 카드에 마우스를 0.45초 올리면 스캐너 설정 카드(`TipProc`, 클래스 `ScanReceiverTip`)가 위젯 옆에 뜸. 시스템 툴팁은 위젯 내용을 가려서 바꿈(2026-10-26 사용자 요청).
- 직접 그리는 컨트롤: `BtnInfo`(Primary/Secondary/Subtle/Switch/Icon/Link/Check) + `ButtonSubclass`(호버 추적, 스위치·체크는 BN_CLICKED 전에 토글) + `DrawButton`(GDI+ 안티앨리어싱, 글자는 GDI ClearType). 서버 스위치는 클릭 후 `UpdateUi`가 실제 서버 상태로 다시 맞춤(시작 실패 시 꺼짐으로 돌아옴).
- 설정 창 입력칸: 테두리 없는 EDIT를 `PlaceField`가 30px 상자 안에 배치하고 `PaintSettings`가 Fluent 텍스트 박스(포커스 시 2px 강조 밑줄)를 그림. Enter = 저장, Esc = 취소(`IsDialogMessage`의 IDOK/IDCANCEL). 저장 시 서버가 쓰는 값이 바뀌었고 실행 중이면 서버를 다시 시작.
- 토스트(T4): 썸네일은 작업 스레드에서 `IShellItemImageFactory::GetImage(SIIGBF_THUMBNAILONLY)` → `WM_APP_THUMB`(그 사이엔 파일 형식 아이콘). 알파 없는 썸네일은 `StretchBlt`, 알파 있으면 `AlphaBlend`(msimg32). 파일 토스트 5초, 안내 토스트 3초, 마우스를 올리면 유지. 링크(열기/폴더에서 보기) 위에서 손 모양 커서.
- `Anim`: 레이어드 창 페이드+슬라이드(로그 창, 토스트), ease-out(나타남)/ease-in(사라짐), `timeBeginPeriod(1)`로 프레임 확보.
- 색은 상단 팔레트 상수(`kBg`, `kCard`, `kAccent`…, QuickLinks와 같은 값)만 사용.

## 확정된 UI 결정 (2026-10-26, 위젯 개편)

- 메인 창 = **QuickLinks 스타일 위젯 B3**: 260×300, 머리글 "스캔 수신" + 톱니(설정) + ✕. 상태 카드(● 실행 중/중지됨 + 켜기 스위치, `ftp://IP:포트` + 복사, `사용자 · 저장 폴더` 축약), "받은 파일 · N" 목록(휠 스크롤), 아래 `폴더 열기` · `로그`. 항상 위 버튼 없음. 위치는 저장, 처음엔 화면 오른쪽 아래.
- **받은 파일 창은 없앰**(폴더 열기·위젯 목록과 겹쳐서). 목록은 이번 실행에서 받은 파일만(재시작 시 비움).
- 설정 = **S3 대화상자**: 이름표 왼쪽·입력 오른쪽, 체크 상자 4개(익명 접속 허용 / 켤 때 서버 자동 시작 / 닫기·최소화 시 트레이로 / **Windows 시작 시 실행**), 저장·취소. 위젯 옆에 붙어서 열림.
- 토스트 = **T4 미리보기**: 왼쪽에 첫 장 썸네일, `ScanReceiver`(여러 개면 "· N개 받음"), 파일 이름, 크기·보낸 곳·시각, `열기 · 폴더에서 보기`. 트레이(숨김/최소화)에 있을 때만.
- **닫기(✕)·Alt+F4·최소화 → 트레이**(`닫기·최소화 시 트레이로`가 켜져 있을 때). 실제 종료는 트레이 메뉴 `종료`. 실행마다 처음 숨길 때 안내 토스트 1회.
- 아이콘 = **R3 받은편지함**: `#005FB8` 둥근 사각형 + 흰 아래 화살표·트레이. QuickLinks(책갈피)·ScreenShot(캡처 모서리)과 같은 틀. 24px 이하는 굵기를 키운 별도 SVG.

## 함정 (실제로 겪은 것)

- `WS_CLIPCHILDREN` 부모에서 `MoveWindow(..., FALSE)` 후 부모만 무효화하면 자식이 안 그려짐 → `RedrawWindow(..., RDW_ALLCHILDREN)`.
- `BS_GROUPBOX`는 안쪽 배경을 안 지워서 크기 변경 시 잔상 → 프레임은 부모가 직접 그림.
- 투명(NULL_BRUSH) static은 실제로 투명하지 않음(밑이 흰 사각형으로 남음) → 히트 영역이 필요하면 툴팁 사각형 도구 사용.
- `small`, `near`, `far`는 Windows 헤더 매크로, `RegisterClass`는 Win32 매크로 → 이름으로 쓰지 말 것.
- `ftp_server.h`가 `NOMINMAX`를 정의 → `gdiplus.h` 앞에 `namespace Gdiplus { using std::min; using std::max; }` 필요.
- `GetPrivateProfileInt`는 음수를 0으로 돌려줌 → 왼쪽 모니터의 창 좌표는 문자열로 읽어 `_wtoi`.
- static 이름표를 입력칸(30px)과 세로로 맞추려면 같은 높이로 두고 `SS_CENTERIMAGE`.
- Edge 헤드리스 `--screenshot`이 이 PC에서 파일을 안 쓰게 됨(샌드박스 밖, 별도 프로필로도) → `build_icon.py`는 PyMuPDF로 렌더.
- 셸 도구: 긴 Python/여러 줄 코드는 heredoc 대신 파일로 써서 실행. PowerShell에서 `git commit -F -`에 here-string 파이프가 안 먹음 → 메시지 파일 사용. Windows PowerShell 5.1의 `Add-Type`은 임시 DLL 쓰기가 막혀 실패할 수 있음 → `wintool` 사용. PowerShell에서 `cmd /c build.bat`은 상대 경로가 안 먹을 때가 있음 → 전체 경로로.
- 세션 임시 폴더(scratchpad)는 세션이 바뀌면 사라짐 → 남길 도구·스크립트는 저장소에 둘 것.

## 남은 작업 / 아이디어

- [ ] **실제 스캐너(복합기)로 전송 테스트** — 아직 한 번도 안 함. 방화벽 허용 여부도 실제 환경에서 확인 필요.
- [ ] 위젯 개편 후 사용자 확인: 호버 표시, 머리글 끌어 이동, 목록 휠 스크롤, 토스트 링크 클릭, Windows 시작 시 실행(로그인 후 트레이로 뜨는지).
- [ ] 다크 모드(QuickLinks는 시스템 테마를 따름. ScanReceiver는 지금 라이트만).
- [ ] PDF 썸네일은 PC에 미리보기 처리기가 있어야 보임(없으면 파일 아이콘).
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
| 2026-10-26 | 이어 작업용 문서(`CLAUDE.md`)와 테스트 도구를 `test/` 한 폴더로 정리. 저장소 공개 + v1.0.0 릴리즈. 설정 파일을 `%APPDATA%\ScanReceiver`로 옮김(기존 ini 자동 이전 확인). **QuickLinks 스타일 위젯으로 UI 개편**(B3 위젯, S3 설정 창 + Windows 시작 시 실행, T4 미리보기 토스트, R3 아이콘), 테스트 인스턴스 모드(`--ini`) |
