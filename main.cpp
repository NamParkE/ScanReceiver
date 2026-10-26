// ScanReceiver - Win32 GUI that receives scanner "Scan to FTP" uploads.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "ftp_server.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <windowsx.h>

#include <algorithm>
#include <map>
// gdiplus.h uses unqualified min/max, which NOMINMAX (ftp_server.h) removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <chrono>
#include <cmath>
#include <cwchar>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")
#endif

namespace {

enum : int {
    IDI_APP = 1,
    // main widget
    IDC_STARTSTOP = 100,
    IDC_COPYADDR,
    IDC_SETTINGS,
    IDC_CLOSE,
    IDC_OPENDIR,
    IDC_SHOWLOG,
    IDC_FILELIST,
    // settings window
    IDC_ROOT = 120,
    IDC_BROWSE,
    IDC_PORT,
    IDC_PASVMIN,
    IDC_PASVMAX,
    IDC_USER,
    IDC_PASS,
    IDC_SHOWPASS,
    IDC_ANON,
    IDC_AUTOSTART,
    IDC_TOTRAY,
    IDC_RUNATLOGON,
    IDC_SAVE,
    IDC_CANCEL,
    IDC_SETCLOSE,
    // log window
    IDC_LOG = 140,
    IDC_VERBOSE,
    IDC_CLEARLOG,
    IDM_OPEN = 200,
    IDM_SHOW_IN_FOLDER,
    IDM_CLEAR_LIST,
    IDM_TRAY_SHOW,
    IDM_TRAY_SETTINGS,
    IDM_TRAY_LOG,
    IDM_TRAY_EXIT,
};

enum : UINT { WM_APP_LOG = WM_APP + 1, WM_APP_FILE, WM_APP_TRAY, WM_APP_THUMB };

enum : UINT_PTR { kCopyResetTimer = 1, kAnimTimer = 100, kToastHoldTimer = 101 };

constexpr wchar_t kWindowClass[] = L"ScanReceiverMainWindow";
constexpr wchar_t kLogClass[] = L"ScanReceiverLogWindow";
constexpr wchar_t kSettingsClass[] = L"ScanReceiverSettings";
constexpr wchar_t kFileListClass[] = L"ScanReceiverFileList";
constexpr wchar_t kToastClass[] = L"ScanReceiverToast";
constexpr wchar_t kAppTitle[] = L"ScanReceiver";
constexpr wchar_t kTestTitle[] = L"ScanReceiver (test)";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kPasswordChar = L'\x25CF';
constexpr size_t kMaxLogLines = 2000;
constexpr UINT_PTR kStatusTipId = 1;

constexpr int kToastFileHoldMs = 5000;  // long enough to reach 열기 / 폴더에서 보기
constexpr int kToastMessageHoldMs = 3000;
constexpr int kShowMs = 280;
constexpr int kHideMs = 200;

// Segoe Fluent Icons code points (the same in Segoe MDL2 Assets, used on Windows 10).
constexpr wchar_t kGlyphSettings[] = L"\xE713";
constexpr wchar_t kGlyphClose[] = L"\xE8BB";
constexpr wchar_t kGlyphCopy[] = L"\xE8C8";
constexpr wchar_t kGlyphCheck[] = L"\xE73E";
constexpr wchar_t kGlyphFolder[] = L"\xE8B7";

// Widget layout (DIPs). Same proportions as QuickLinks: 36 px header, 4 px corners inside the window.
constexpr int kW = 260;
constexpr int kHeaderH = 36;
constexpr int kCardTop = 36, kCardH = 76;
constexpr int kGroupTop = kCardTop + kCardH + 6, kGroupH = 24;
constexpr int kRowH = 30, kVisibleRows = 4;
constexpr int kListTop = kGroupTop + kGroupH, kListH = kRowH * kVisibleRows;
constexpr int kFooterTop = kListTop + kListH + 4, kFooterH = 34;
constexpr int kH = kFooterTop + kFooterH;

// Settings window layout (DIPs).
constexpr int kSetW = 340, kSetFooterTop = 314, kSetH = kSetFooterTop + 50;
constexpr int kFieldH = 30;

// Fluent (Windows 11) light palette, the same values as QuickLinks.
constexpr COLORREF kBg = RGB(243, 243, 243);
constexpr COLORREF kCard = RGB(251, 251, 251);
constexpr COLORREF kField = RGB(255, 255, 255);
constexpr COLORREF kCardBorder = RGB(229, 229, 229);
constexpr COLORREF kDivider = RGB(228, 228, 228);
constexpr COLORREF kFooterBg = RGB(235, 235, 235);
constexpr COLORREF kText = RGB(26, 26, 26);
constexpr COLORREF kTextSecondary = RGB(95, 99, 104);
constexpr COLORREF kTextTertiary = RGB(138, 138, 138);
constexpr COLORREF kTextDisabled = RGB(160, 160, 160);
constexpr COLORREF kAccent = RGB(0, 95, 184);
constexpr COLORREF kAccentHover = RGB(25, 110, 191);
constexpr COLORREF kAccentPressed = RGB(0, 84, 163);
constexpr COLORREF kControl = RGB(253, 253, 253);
constexpr COLORREF kControlHover = RGB(246, 246, 246);
constexpr COLORREF kControlPressed = RGB(240, 240, 240);
constexpr COLORREF kControlBorder = RGB(213, 213, 213);
constexpr COLORREF kHover = RGB(234, 234, 234);     // rgba(0,0,0,.037) over kBg
constexpr COLORREF kSelected = RGB(230, 230, 230);  // rgba(0,0,0,.055) over kBg
constexpr COLORREF kFieldUnderline = RGB(138, 138, 138);
constexpr COLORREF kDisabledFill = RGB(200, 200, 200);
constexpr COLORREF kDanger = RGB(196, 43, 28);
constexpr COLORREF kRunDot = RGB(15, 123, 15), kStopDot = RGB(138, 138, 138);
constexpr COLORREF kScrollThumb = RGB(190, 190, 190);

// ---- animation ------------------------------------------------------------------------------
// Fade + slide for a layered top-level window, driven by a WM_TIMER on that window.

struct Anim {
    bool active = false;
    bool showing = false;
    double start = 0;
    int duration = 0;
    POINT from{}, to{};
};

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

double EaseOutCubic(double t) { return 1 - std::pow(1 - t, 3); }
double EaseInCubic(double t) { return t * t * t; }

void SetLayered(HWND w, bool on) {
    LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    LONG_PTR next = on ? (ex | WS_EX_LAYERED) : (ex & ~(LONG_PTR)WS_EX_LAYERED);
    if (next != ex) SetWindowLongPtrW(w, GWL_EXSTYLE, next);
}

void AnimStart(HWND w, Anim& a, bool show, POINT from, POINT to, int ms) {
    if (!a.active) timeBeginPeriod(1);  // ~60 fps timer instead of the default 64 Hz tick
    SetLayered(w, true);
    a.active = true;
    a.showing = show;
    a.start = NowMs();
    a.duration = ms;
    a.from = from;
    a.to = to;
    if (show) {
        SetLayeredWindowAttributes(w, 0, 0, LWA_ALPHA);
        SetWindowPos(w, nullptr, from.x, from.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(w, SW_SHOWNOACTIVATE);
    }
    SetTimer(w, kAnimTimer, 10, nullptr);
}

// Advances one frame; returns true on the frame the animation finishes.
bool AnimStep(HWND w, Anim& a) {
    if (!a.active) return false;
    double t = (NowMs() - a.start) / a.duration;
    if (t > 1) t = 1;
    double e = a.showing ? EaseOutCubic(t) : EaseInCubic(t);
    int x = a.from.x + (int)std::lround((a.to.x - a.from.x) * e);
    int y = a.from.y + (int)std::lround((a.to.y - a.from.y) * e);
    SetWindowPos(w, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(w, 0, (BYTE)std::lround(255 * (a.showing ? e : 1 - e)), LWA_ALPHA);
    if (t < 1) return false;
    KillTimer(w, kAnimTimer);
    a.active = false;
    timeEndPeriod(1);
    if (!a.showing) ShowWindow(w, SW_HIDE);
    return true;
}

bool IsShownOrShowing(HWND w, const Anim& a) { return IsWindowVisible(w) && !(a.active && !a.showing); }

// ---- state ----------------------------------------------------------------------------------

struct Controls {
    // main widget
    HWND startStop, copyAddr, settings, close, openDir, showLog, list;
    // settings window
    HWND lblRoot, root, browse, lblPort, port, lblPasv, pasvMin, lblTilde, pasvMax;
    HWND lblUser, user, lblPass, pass, showPass, anon, autoStart, toTray, runAtLogon, save, cancel, setClose;
    // log window
    HWND verbose, clearLog, log;
};

// Owner-drawn buttons: look, the surface they sit on (for anti-aliased corners), and live state.
enum class BtnKind { Primary, Secondary, Subtle, Switch, Icon, Link, Check };

struct BtnInfo {
    BtnKind kind;
    COLORREF surface;
    bool toggles = false;  // Switch / Check, or a Subtle button that flips (password "show")
    bool checked = false;
    bool hover = false;
    bool danger = false;    // Icon: red on hover (close)
    bool bordered = false;  // Icon: Secondary-style box (folder picker)
    bool bold = false;      // Link
};

struct Settings {
    std::wstring root, user, pass;
    int port = 21, pasvMin = 50000, pasvMax = 50100;
    bool anon = false, autoStart = false, toTray = true, verbose = false;
};

struct FileItem {
    std::wstring path, name, size, time, peer;
    int icon = 0;
};

enum class ToastPart { None, Body, Open, Folder, Close };

struct Toast {
    HWND hwnd = nullptr;
    Anim anim;
    int count = 0;
    bool isFile = false;
    int holdMs = kToastMessageHoldMs;
    std::wstring path, header, title, meta;
    HBITMAP thumb = nullptr;    // shell thumbnail, arrives from a worker thread
    HICON typeIcon = nullptr;   // shown until (or instead of) the thumbnail
    RECT openRc{}, folderRc{}, closeRc{};
    ToastPart hot = ToastPart::None;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND logWnd = nullptr;
    HWND settingsWnd = nullptr;
    HWND tip = nullptr;
    Anim logAnim;
    Toast toast;
    HFONT font = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontIcon = nullptr;
    HFONT fontIconSmall = nullptr;
    HFONT fontMono = nullptr;
    HICON icon = nullptr;       // large (Alt+Tab, taskbar)
    HICON iconSmall = nullptr;  // tray, toast header: the 16/20/24 px frame, not a shrunken 32 px one
    HIMAGELIST sysImages = nullptr;
    int dpi = 96;
    int fontH = 16;
    std::vector<std::pair<HWND, RECT>> fields;  // settings edit -> the box painted around it
    std::map<HWND, BtnInfo> btns;
    HBRUSH bgBrush = nullptr, fieldBrush = nullptr;
    Controls c{};
    Settings cfg;
    FtpServer server;
    std::vector<FileItem> files;  // newest first
    int listTop = 0, listHover = -1, listSel = -1;
    std::deque<std::wstring> logLines;  // full log; the list box may show a filtered view
    std::wstring primaryIp;
    std::wstring statusTip;
    bool trayHintShown = false;
    bool testInstance = false;  // started with --ini: no single-instance lock, no registry writes
    NOTIFYICONDATAW nid{};
    std::wstring iniPath;
};

App g;

int S(int v) { return MulDiv(v, g.dpi, 96); }

RECT SR(int x, int y, int w, int h) { return {S(x), S(y), S(x + w), S(y + h)}; }

// ---- helpers --------------------------------------------------------------------------------

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(h, &s[0], n + 1);
    return s;
}

bool IsChecked(HWND h) {
    auto it = g.btns.find(h);
    return it != g.btns.end() ? it->second.checked : SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SetChecked(HWND h, bool on) {
    auto it = g.btns.find(h);
    if (it == g.btns.end()) {
        SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        return;
    }
    it->second.checked = on;
    InvalidateRect(h, nullptr, FALSE);
}

bool HasArg(const wchar_t* name) {
    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    for (int i = 1; argv && i < argc; ++i) found |= wcscmp(argv[i], name) == 0;
    LocalFree(argv);
    return found;
}

bool HasFontFamily(const wchar_t* name) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, name, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(
        dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int { return *(bool*)p = true, 0; },
        (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

std::wstring FormatSize(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024 && u < 3) {
        v /= 1024;
        ++u;
    }
    wchar_t buf[32];
    swprintf(buf, 32, u == 0 ? L"%.0f %s" : L"%.1f %s", v, units[u]);
    return buf;
}

std::wstring NowString(bool seconds = true) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    if (seconds)
        swprintf(buf, 32, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    else
        swprintf(buf, 32, L"%02u:%02u", st.wHour, st.wMinute);
    return buf;
}

// "C:\Users\me\Desktop\scan" -> "Desktop\scan": enough to recognise the folder on one line.
std::wstring ShortPath(const std::wstring& path) {
    size_t last = path.find_last_of(L'\\', path.size() >= 2 ? path.size() - 2 : 0);
    if (last == std::wstring::npos || last == 0) return path;
    size_t prev = path.find_last_of(L'\\', last - 1);
    return prev == std::wstring::npos ? path : path.substr(prev + 1);
}

std::wstring FormatIp(const in_addr& a) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%u.%u.%u.%u", a.S_un.S_un_b.s_b1, a.S_un.S_un_b.s_b2, a.S_un.S_un_b.s_b3,
             a.S_un.S_un_b.s_b4);
    return buf;
}

std::vector<std::wstring> LocalIPv4Addresses() {
    std::vector<std::wstring> out;
    char host[256] = {};
    if (gethostname(host, sizeof host) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* p = res; p; p = p->ai_next) {
        const in_addr& a = ((sockaddr_in*)p->ai_addr)->sin_addr;
        if (a.S_un.S_un_b.s_b1 == 127 || (a.S_un.S_un_b.s_b1 == 169 && a.S_un.S_un_b.s_b2 == 254)) continue;
        std::wstring ip = FormatIp(a);
        bool dup = false;
        for (const auto& s : out) dup |= s == ip;
        if (!dup) out.push_back(ip);
    }
    freeaddrinfo(res);
    return out;
}

// The address of the adapter that holds the default route (no packet is sent).
std::wstring PrimaryIPv4() {
    std::wstring ip;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return ip;
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr.s_addr = htonl(0x08080808);
    sockaddr_in local{};
    int len = sizeof local;
    if (connect(s, (sockaddr*)&dst, sizeof dst) == 0 && getsockname(s, (sockaddr*)&local, &len) == 0 &&
        local.sin_addr.s_addr != 0)
        ip = FormatIp(local.sin_addr);
    closesocket(s);
    return ip;
}

bool CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g.hwnd)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

void RoundCorners(HWND w) {
    const DWORD kCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE (Windows 11)
    const int kRound = 2;                // DWMWCP_ROUND
    DwmSetWindowAttribute(w, kCornerPreference, &kRound, sizeof kRound);
}

void SetCaptionColor(HWND w, COLORREF c) {
    const DWORD kCaptionColor = 35;  // DWMWA_CAPTION_COLOR (Windows 11)
    DwmSetWindowAttribute(w, kCaptionColor, &c, sizeof c);
}

RECT WorkAreaOf(HWND w) {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcWork;
}

std::wstring DefaultRoot() {
    PWSTR docs = nullptr;
    std::wstring root = L"C:\\Scans";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) root = std::wstring(docs) + L"\\Scans";
    CoTaskMemFree(docs);
    return root;
}

bool PickFolder(HWND owner, std::wstring& path) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"스캔 파일을 저장할 폴더 선택");
    IShellItem* start = nullptr;
    if (!path.empty() && SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dlg->SetFolder(start);
        start->Release();
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                path = p;
                CoTaskMemFree(p);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

// ---- settings (%APPDATA%\ScanReceiver\ScanReceiver.ini) --------------------------------------

// "--ini <path>" overrides the location (tests use it so the user's settings are never touched).
// Otherwise %APPDATA%\ScanReceiver; an ini left next to the exe by older versions is moved there once.
std::wstring ResolveIniPath() {
    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    for (int i = 1; argv && i + 1 < argc; ++i)
        if (wcscmp(argv[i], L"--ini") == 0) path = argv[i + 1];
    LocalFree(argv);
    if (!path.empty()) return path;

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring legacy = exe;
    legacy = legacy.substr(0, legacy.find_last_of(L'\\') + 1) + L"ScanReceiver.ini";

    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        CoTaskMemFree(appData);
        return legacy;
    }
    std::wstring dir = std::wstring(appData) + L"\\ScanReceiver";
    CoTaskMemFree(appData);
    CreateDirectoryW(dir.c_str(), nullptr);
    path = dir + L"\\ScanReceiver.ini";

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES &&
        !MoveFileExW(legacy.c_str(), path.c_str(), MOVEFILE_COPY_ALLOWED))
        CopyFileW(legacy.c_str(), path.c_str(), TRUE);  // exe folder not writable: copy instead
    return path;
}

std::wstring IniGet(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"Settings", key, def, buf, 1024, g.iniPath.c_str());
    return buf;
}

int IniGetInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(L"Settings", key, def, g.iniPath.c_str());
}

void IniSet(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"Settings", key, v.c_str(), g.iniPath.c_str());
}

void LoadSettings() {
    Settings& s = g.cfg;
    s.root = IniGet(L"RootDir", DefaultRoot().c_str());
    s.port = IniGetInt(L"Port", 21);
    s.pasvMin = IniGetInt(L"PasvMin", 50000);
    s.pasvMax = IniGetInt(L"PasvMax", 50100);
    s.user = IniGet(L"User", L"scan");
    s.pass = IniGet(L"Password", L"scan1234");
    s.anon = IniGet(L"AllowAnonymous", L"0") == L"1";
    s.autoStart = IniGet(L"AutoStart", L"0") == L"1";
    s.toTray = IniGet(L"MinimizeToTray", L"1") == L"1";
    s.verbose = IniGet(L"VerboseLog", L"0") == L"1";
}

void SaveSettings() {
    const Settings& s = g.cfg;
    IniSet(L"RootDir", s.root);
    IniSet(L"Port", std::to_wstring(s.port));
    IniSet(L"PasvMin", std::to_wstring(s.pasvMin));
    IniSet(L"PasvMax", std::to_wstring(s.pasvMax));
    IniSet(L"User", s.user);
    IniSet(L"Password", s.pass);
    IniSet(L"AllowAnonymous", s.anon ? L"1" : L"0");
    IniSet(L"AutoStart", s.autoStart ? L"1" : L"0");
    IniSet(L"MinimizeToTray", s.toTray ? L"1" : L"0");
    IniSet(L"VerboseLog", s.verbose ? L"1" : L"0");
}

// ---- run at logon (HKCU Run key, no admin rights) -------------------------------------------

std::wstring RunCommand() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return L"\"" + std::wstring(exe) + L"\" --startup";
}

std::wstring RunValue() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppTitle, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return buf;
}

bool RunAtLogonEnabled() { return !RunValue().empty(); }

void SetRunAtLogon(bool on) {
    if (g.testInstance) return;
    if (on) {
        std::wstring cmd = RunCommand();
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kAppTitle, REG_SZ, cmd.c_str(),
                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kAppTitle);
    }
}

// The exe was moved since run-at-logon was turned on: point the Run entry at this copy.
void RefreshRunAtLogon() {
    std::wstring v = RunValue();
    if (!v.empty() && _wcsicmp(v.c_str(), RunCommand().c_str()) != 0) SetRunAtLogon(true);
}

// ---- log ------------------------------------------------------------------------------------

// FTP command/reply traffic ("[ip] > CMD", "[ip] < 226 ...") is hidden unless verbose is on.
bool IsProtocolLine(const std::wstring& line) {
    return line.find(L"] > ") != std::wstring::npos || line.find(L"] < ") != std::wstring::npos;
}

void AppendLogRow(const std::wstring& line) {
    int idx = (int)SendMessageW(g.c.log, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    if (idx >= (int)kMaxLogLines) SendMessageW(g.c.log, LB_DELETESTRING, 0, 0);
    SendMessageW(g.c.log, LB_SETTOPINDEX, SendMessageW(g.c.log, LB_GETCOUNT, 0, 0) - 1, 0);
}

void AddLog(const std::wstring& msg) {
    std::wstring line = L"[" + NowString() + L"] " + msg;
    g.logLines.push_back(line);
    if (g.logLines.size() > kMaxLogLines * 4) g.logLines.pop_front();
    if (g.cfg.verbose || !IsProtocolLine(line)) AppendLogRow(line);
}

void RebuildLog() {
    SendMessageW(g.c.log, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.c.log, LB_RESETCONTENT, 0, 0);
    for (const auto& line : g.logLines)
        if (g.cfg.verbose || !IsProtocolLine(line)) AppendLogRow(line);
    SendMessageW(g.c.log, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.c.log, nullptr, TRUE);
}

// Popups (log, settings) dock beside the widget: right of it, else left, level with its top.
POINT PopupTargetPos(HWND w) {
    RECT m, r;
    GetWindowRect(g.hwnd, &m);
    GetWindowRect(w, &r);
    int pw = r.right - r.left, ph = r.bottom - r.top;
    RECT wa = WorkAreaOf(g.hwnd);
    int gap = S(6);
    int y = (std::max)((int)wa.top, (std::min)((int)m.top, (int)wa.bottom - ph));
    if (m.right + gap + pw <= wa.right) return {m.right + gap, y};
    if (m.left - gap - pw >= wa.left) return {m.left - gap - pw, y};
    return {(std::max)((int)wa.left, (int)wa.right - pw), y};
}

void ShowPopup(HWND w, Anim& a) {
    if (IsShownOrShowing(w, a)) return;
    POINT to = PopupTargetPos(w);
    AnimStart(w, a, true, {to.x, to.y + S(18)}, to, kShowMs);
}

void HidePopup(HWND w, Anim& a) {
    if (!IsShownOrShowing(w, a)) return;
    RECT r;
    GetWindowRect(w, &r);
    AnimStart(w, a, false, {r.left, r.top}, {r.left, r.top + S(10)}, kHideMs);
}

void TogglePopup(HWND w, Anim& a) { IsShownOrShowing(w, a) ? HidePopup(w, a) : ShowPopup(w, a); }

// ---- drawing (GDI+ for anti-aliased shapes, GDI for ClearType text) -------------------------

Gdiplus::Color ToColor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

void RoundPath(Gdiplus::GraphicsPath& p, const RECT& rc, float r) {
    float x = rc.left + 0.5f, y = rc.top + 0.5f;
    float w = (float)(rc.right - rc.left) - 1, h = (float)(rc.bottom - rc.top) - 1;
    float d = (std::min)(r * 2, (std::min)(w, h));
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRound(Gdiplus::Graphics& gr, const RECT& rc, float r, COLORREF fill, COLORREF border = CLR_INVALID) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, rc, r);
    Gdiplus::SolidBrush brush(ToColor(fill));
    gr.FillPath(&brush, &path);
    if (border != CLR_INVALID) {
        Gdiplus::Pen pen(ToColor(border), 1.0f);
        gr.DrawPath(&pen, &path);
    }
}

void StrokeRound(Gdiplus::Graphics& gr, const RECT& rc, float r, COLORREF color, float width) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, rc, r);
    Gdiplus::Pen pen(ToColor(color), width);
    gr.DrawPath(&pen, &path);
}

void FillCircle(Gdiplus::Graphics& gr, float cx, float cy, float r, COLORREF c) {
    Gdiplus::SolidBrush brush(ToColor(c));
    gr.FillEllipse(&brush, cx - r, cy - r, 2 * r, 2 * r);
}

void FillSolid(HDC dc, const RECT& rc, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

void DrawTextIn(HDC dc, const std::wstring& s, RECT rc, HFONT font, COLORREF color, UINT fmt) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, s.c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | fmt);
    SelectObject(dc, old);
}

int TextWidth(HDC dc, const std::wstring& s, HFONT font) {
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

// Back buffer for a WM_PAINT: draw into dc, then Blit().
struct Buffer {
    HDC target, dc;
    HBITMAP bmp;
    HGDIOBJ old;
    RECT rc;
    Buffer(HWND w, HDC t) : target(t) {
        GetClientRect(w, &rc);
        dc = CreateCompatibleDC(t);
        bmp = CreateCompatibleBitmap(t, rc.right, rc.bottom);
        old = SelectObject(dc, bmp);
    }
    ~Buffer() {
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
};

// Paints one owner-drawn button into a back buffer, then blits it.
void DrawButton(const DRAWITEMSTRUCT* d) {
    auto it = g.btns.find(d->hwndItem);
    if (it == g.btns.end()) return;
    const BtnInfo& b = it->second;
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC mem = CreateCompatibleDC(d->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    FillSolid(mem, rc, b.surface);

    bool disabled = d->itemState & ODS_DISABLED;
    bool pressed = d->itemState & ODS_SELECTED;
    bool hover = b.hover && !disabled;
    bool focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
    COLORREF fg = disabled ? kTextDisabled : kText;
    HFONT font = g.font;
    RECT textRc = rc;
    UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX;
    {
        Gdiplus::Graphics gr(mem);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        float radius = (float)S(4);
        switch (b.kind) {
        case BtnKind::Primary:
            FillRound(gr, rc, radius,
                      disabled ? kDisabledFill : pressed ? kAccentPressed : hover ? kAccentHover : kAccent);
            fg = RGB(255, 255, 255);
            break;
        case BtnKind::Secondary:
            FillRound(gr, rc, radius, pressed ? kControlPressed : hover ? kControlHover : kControl, kControlBorder);
            break;
        case BtnKind::Subtle:
            if (pressed || hover || b.checked) FillRound(gr, rc, radius, pressed ? kSelected : kHover);
            break;
        case BtnKind::Icon:
            font = g.fontIcon;
            fg = kTextSecondary;
            if (b.bordered) {
                FillRound(gr, rc, radius, pressed ? kControlPressed : hover ? kControlHover : kControl, kControlBorder);
            } else if (b.danger && (hover || pressed)) {
                FillRound(gr, rc, radius, kDanger);
                fg = RGB(255, 255, 255);
            } else if (hover || pressed) {
                FillRound(gr, rc, radius, pressed ? kSelected : kHover);
            }
            break;
        case BtnKind::Link: {
            RECT in = {rc.left + S(4), rc.top + S(3), rc.right - S(4), rc.bottom - S(3)};
            if (hover || pressed) FillRound(gr, in, radius, pressed ? kSelected : kHover);
            fg = pressed ? kAccentPressed : kAccent;
            if (b.bold) font = g.fontBold;
            break;
        }
        case BtnKind::Check: {
            int box = S(16), by = (h - box) / 2;
            RECT r = {0, by, box, by + box};
            if (b.checked)
                FillRound(gr, r, (float)S(3), hover ? kAccentHover : kAccent);
            else
                FillRound(gr, r, (float)S(3), hover ? kControlHover : kField, kFieldUnderline);
            if (focus) StrokeRound(gr, {r.left - 2, r.top - 2, r.right + 2, r.bottom + 2}, (float)S(4), kText, 1.0f);
            textRc.left = box + S(8);
            fmt = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX;
            break;
        }
        case BtnKind::Switch: {
            float th = (float)S(20), tw = (float)S(40), ty = (h - th) / 2;
            RECT track = {0, (LONG)ty, (LONG)tw, (LONG)(ty + th)};
            float knobY = ty + th / 2;
            if (b.checked) {
                FillRound(gr, track, th / 2, disabled ? kDisabledFill : hover ? kAccentHover : kAccent);
                FillCircle(gr, tw - th / 2, knobY, (float)S(hover ? 7 : 6), RGB(255, 255, 255));
            } else {
                FillRound(gr, track, th / 2, hover ? kControlHover : b.surface, disabled ? kDisabledFill : kFieldUnderline);
                FillCircle(gr, th / 2, knobY, (float)S(hover ? 6 : 5), disabled ? kDisabledFill : RGB(92, 92, 92));
            }
            if (focus) StrokeRound(gr, track, th / 2, kText, 1.0f);
            textRc.left = (LONG)tw + S(8);
            fmt = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX;
            break;
        }
        }
        if (focus && b.kind != BtnKind::Switch && b.kind != BtnKind::Check) StrokeRound(gr, rc, radius, kText, 1.5f);
    }

    if (b.kind == BtnKind::Check && b.checked) {
        int box = S(16), by = (h - box) / 2;
        DrawTextIn(mem, kGlyphCheck, {0, by, box, by + box}, g.fontIconSmall, RGB(255, 255, 255), DT_CENTER);
    }
    wchar_t text[128] = {};
    GetWindowTextW(d->hwndItem, text, 128);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, fg);
    HGDIOBJ oldFont = SelectObject(mem, font);
    DrawTextW(mem, text, -1, &textRc, fmt);
    SelectObject(mem, oldFont);

    BitBlt(d->hDC, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// Hover tracking, and toggling switches / check boxes before the parent sees BN_CLICKED.
LRESULT CALLBACK ButtonSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    auto it = g.btns.find(h);
    if (it != g.btns.end()) {
        BtnInfo& b = it->second;
        switch (msg) {
        case WM_MOUSEMOVE:
            if (!b.hover) {
                b.hover = true;
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            b.hover = false;
            InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_LBUTTONDBLCLK:  // a fast second click is still a click
            msg = WM_LBUTTONDOWN;
            break;
        case WM_LBUTTONUP:
            if (b.toggles && GetCapture() == h) {
                POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                RECT rc;
                GetClientRect(h, &rc);
                if (PtInRect(&rc, pt)) b.checked = !b.checked;
            }
            break;
        case WM_KEYUP:
            if (b.toggles && wp == VK_SPACE) b.checked = !b.checked;
            break;
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- controls -------------------------------------------------------------------------------

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, parent,
                             (HMENU)(INT_PTR)id, g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, FALSE);
    return h;
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, BtnKind kind, COLORREF surface) {
    HWND h = Make(parent, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, id);
    g.btns[h] = BtnInfo{kind, surface, kind == BtnKind::Switch || kind == BtnKind::Check};
    SetWindowSubclass(h, ButtonSubclass, 0, 0);
    return h;
}

HWND MakeEdit(HWND parent, int id, DWORD style = 0) {
    HWND h = Make(parent, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | style, id);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    return h;
}

void Place(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, S(x), S(y), S(w), S(hgt), FALSE); }

// An edit sits borderless inside a painted Fluent text box.
void PlaceField(HWND edit, int x, int y, int w) {
    RECT box = SR(x, y, w, kFieldH);
    MoveWindow(edit, box.left + S(9), box.top + ((box.bottom - box.top) - g.fontH) / 2, (box.right - box.left) - S(18),
               g.fontH, FALSE);
    for (auto& f : g.fields)
        if (f.first == edit) {
            f.second = box;
            return;
        }
    g.fields.push_back({edit, box});
}

// Borderless windows (widget, settings): DWM keeps the rounded corners, border and shadow; the
// painted header strip drags the window.
LRESULT BorderlessHitTest(HWND w, LPARAM lp) {
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(w, &p);
    return p.y < S(kHeaderH) ? HTCAPTION : HTCLIENT;
}

// ---- received files -------------------------------------------------------------------------

int ShellIconIndex(const std::wstring& path) {
    SHFILEINFOW sfi{};
    SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    return sfi.iIcon;
}

int ListMaxTop() { return (std::max)(0, (int)g.files.size() - kVisibleRows); }

void ListScroll(int rows) {
    int top = (std::max)(0, (std::min)(g.listTop + rows, ListMaxTop()));
    if (top == g.listTop) return;
    g.listTop = top;
    InvalidateRect(g.c.list, nullptr, FALSE);
}

int ListHit(int y) {
    if (y < 0) return -1;
    int i = g.listTop + y / S(kRowH);
    return i < (int)g.files.size() ? i : -1;
}

std::wstring SelectedFile() {
    return g.listSel >= 0 && g.listSel < (int)g.files.size() ? g.files[g.listSel].path : std::wstring();
}

void OpenFile(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g.hwnd, (L"파일이 없습니다 (이동/삭제됨):\n" + path).c_str(), kAppTitle, MB_ICONWARNING);
        return;
    }
    ShellExecuteW(g.hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowInFolder(const std::wstring& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(g.hwnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void ShowPopupMenu(std::initializer_list<std::pair<int, const wchar_t*>> items, int defaultId) {
    HMENU menu = CreatePopupMenu();
    for (const auto& it : items) AppendMenuW(menu, it.second ? MF_STRING : MF_SEPARATOR, it.first, it.second);
    if (defaultId) SetMenuDefaultItem(menu, defaultId, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void PaintList(HWND hwnd, HDC target) {
    Buffer buf(hwnd, target);
    HDC dc = buf.dc;
    RECT rc = buf.rc;
    FillSolid(dc, rc, kBg);

    if (g.files.empty()) {
        const wchar_t* text = g.server.IsRunning() ? L"아직 받은 파일이 없습니다" : L"서버를 켜면 받은 파일이 여기에 표시됩니다";
        DrawTextIn(dc, text, rc, g.fontSmall, kTextTertiary, DT_CENTER | DT_END_ELLIPSIS);
        return;
    }

    int rowH = S(kRowH);
    bool overflow = (int)g.files.size() > kVisibleRows;
    int right = rc.right - S(overflow ? 8 : 6);
    int last = (std::min)((int)g.files.size(), g.listTop + kVisibleRows + 1);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        for (int i = g.listTop; i < last; ++i) {
            if (i != g.listSel && i != g.listHover) continue;
            int y = (i - g.listTop) * rowH;
            FillRound(gr, {S(6), y, right, y + rowH}, (float)S(4), i == g.listSel ? kSelected : kHover);
        }
        if (overflow) {
            int maxTop = ListMaxTop();
            int thumbH = (std::max)(S(16), (int)(rc.bottom * kVisibleRows / (int)g.files.size()));
            int thumbY = maxTop ? (rc.bottom - thumbH) * g.listTop / maxTop : 0;
            FillRound(gr, {rc.right - S(5), thumbY, rc.right - S(2), thumbY + thumbH}, (float)S(1), kScrollThumb);
        }
    }
    for (int i = g.listTop; i < last; ++i) {
        const FileItem& f = g.files[i];
        int y = (i - g.listTop) * rowH;
        bool missing = GetFileAttributesW(f.path.c_str()) == INVALID_FILE_ATTRIBUTES;
        ImageList_Draw(g.sysImages, f.icon, dc, S(14), y + (rowH - S(16)) / 2, ILD_TRANSPARENT);
        int timeW = TextWidth(dc, f.time, g.fontSmall);
        DrawTextIn(dc, f.time, {right - S(8) - timeW, y, right - S(8), y + rowH}, g.fontSmall, kTextTertiary, DT_RIGHT);
        DrawTextIn(dc, f.name, {S(38), y, right - S(16) - timeW, y + rowH}, g.font,
                   missing ? kTextTertiary : kText, DT_END_ELLIPSIS);
    }
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintList(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int hit = ListHit(GET_Y_LPARAM(lp));
        if (hit != g.listHover) {
            g.listHover = hit;
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.listHover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        g.listSel = ListHit(GET_Y_LPARAM(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
        g.listSel = ListHit(GET_Y_LPARAM(lp));
        if (g.listSel >= 0) OpenFile(SelectedFile());
        return 0;
    case WM_RBUTTONUP:
        if (g.listSel >= 0)
            ShowPopupMenu({{IDM_OPEN, L"열기"}, {IDM_SHOW_IN_FOLDER, L"폴더에서 보기"}, {0, nullptr},
                           {IDM_CLEAR_LIST, L"목록 지우기"}},
                          IDM_OPEN);
        return 0;
    case WM_MOUSEWHEEL:
        ListScroll(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- toast (shown while the widget is in the tray) ------------------------------------------

void ToastHide() {
    Toast& t = g.toast;
    KillTimer(t.hwnd, kToastHoldTimer);
    if (!IsShownOrShowing(t.hwnd, t.anim)) return;
    RECT r;
    GetWindowRect(t.hwnd, &r);
    AnimStart(t.hwnd, t.anim, false, {r.left, r.top}, {r.left, r.top + S(12)}, kHideMs);
}

// Brings the toast up (or, if it is already up, restarts its hold) with the current content.
void ToastPresent() {
    Toast& t = g.toast;
    InvalidateRect(t.hwnd, nullptr, FALSE);
    if (IsShownOrShowing(t.hwnd, t.anim)) {
        if (!t.anim.active) SetTimer(t.hwnd, kToastHoldTimer, t.holdMs, nullptr);
        return;
    }
    RECT r, wa = WorkAreaOf(g.hwnd);
    GetWindowRect(t.hwnd, &r);
    int x = wa.right - (r.right - r.left) - S(12);
    int y = wa.bottom - (r.bottom - r.top) - S(12);
    AnimStart(t.hwnd, t.anim, true, {x, y + S(28)}, {x, y}, kShowMs);
}

void ToastSetThumb(HBITMAP bmp) {
    if (g.toast.thumb) DeleteObject(g.toast.thumb);
    g.toast.thumb = bmp;
}

void ToastSetTypeIcon(HICON icon) {
    if (g.toast.typeIcon) DestroyIcon(g.toast.typeIcon);
    g.toast.typeIcon = icon;
}

struct ThumbResult {
    std::wstring path;
    HBITMAP bmp;
};

// Shell thumbnails can be slow (PDF handlers, network folders), so they are made off the UI thread.
void RequestThumb(const std::wstring& path) {
    HWND target = g.toast.hwnd;
    SIZE size = {S(104), S(136)};
    std::thread([path, target, size] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        HBITMAP bmp = nullptr;
        IShellItemImageFactory* f = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) {
            if (FAILED(f->GetImage(size, SIIGBF_THUMBNAILONLY, &bmp))) bmp = nullptr;
            f->Release();
        }
        CoUninitialize();
        auto* r = new ThumbResult{path, bmp};
        if (!PostMessageW(target, WM_APP_THUMB, 0, (LPARAM)r)) {
            if (bmp) DeleteObject(bmp);
            delete r;
        }
    }).detach();
}

void ToastShowFile(const FileItem& f) {
    Toast& t = g.toast;
    t.count = IsShownOrShowing(t.hwnd, t.anim) && t.isFile ? t.count + 1 : 1;
    t.isFile = true;
    t.holdMs = kToastFileHoldMs;
    t.path = f.path;
    t.header = t.count > 1 ? L"ScanReceiver  ·  " + std::to_wstring(t.count) + L"개 받음" : std::wstring(L"ScanReceiver");
    t.title = f.name;
    t.meta = f.size + L"  ·  " + f.peer + L"  ·  " + f.time;
    SHFILEINFOW sfi{};
    SHGetFileInfoW(f.path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                   SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
    ToastSetTypeIcon(sfi.hIcon);
    ToastSetThumb(nullptr);
    RequestThumb(f.path);
    ToastPresent();
}

void ToastMessage(const std::wstring& title, const std::wstring& line1, const std::wstring& line2) {
    Toast& t = g.toast;
    t.count = 0;  // a file arriving next starts its own count at 1
    t.isFile = false;
    t.holdMs = kToastMessageHoldMs;
    t.path.clear();
    t.header = title;
    t.title = line1;
    t.meta = line2;
    ToastSetThumb(nullptr);
    ToastSetTypeIcon(nullptr);
    ToastPresent();
}

// Draws a shell thumbnail scaled to fit `box`, keeping its aspect ratio.
void DrawThumb(HDC dc, HBITMAP bmp, const RECT& box) {
    DIBSECTION ds{};
    if (!GetObjectW(bmp, sizeof ds, &ds)) return;
    int bw = ds.dsBm.bmWidth, bh = std::abs(ds.dsBm.bmHeight);
    if (bw <= 0 || bh <= 0) return;
    int boxW = box.right - box.left, boxH = box.bottom - box.top;
    double scale = (std::min)((double)boxW / bw, (double)boxH / bh);
    int w = (std::max)(1, (int)(bw * scale)), h = (std::max)(1, (int)(bh * scale));
    int x = box.left + (boxW - w) / 2, y = box.top + (boxH - h) / 2;

    // 32 bpp thumbnails with real alpha are premultiplied; JPEG-style ones leave alpha at 0.
    bool alpha = false;
    if (ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmBits) {
        const BYTE* p = (const BYTE*)ds.dsBm.bmBits;
        for (int i = 0; i < bw * bh && !alpha; ++i) alpha = p[i * 4 + 3] != 0;
    }
    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, bmp);
    if (alpha) {
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(dc, x, y, w, h, src, 0, 0, bw, bh, bf);
    } else {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchBlt(dc, x, y, w, h, src, 0, 0, bw, bh, SRCCOPY);
    }
    SelectObject(src, old);
    DeleteDC(src);
}

// T4 layout: page thumbnail on the left; app line, file name, details and 열기 · 폴더에서 보기.
void PaintToast(HWND hwnd, HDC target) {
    Toast& t = g.toast;
    Buffer buf(hwnd, target);
    HDC dc = buf.dc;
    RECT rc = buf.rc;
    FillSolid(dc, rc, RGB(250, 250, 250));
    HBRUSH frame = CreateSolidBrush(kCardBorder);
    FrameRect(dc, &rc, frame);
    DeleteObject(frame);

    RECT box = SR(12, 14, 52, 68);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        FillRound(gr, box, (float)S(4), RGB(255, 255, 255), kCardBorder);
        if (t.hot == ToastPart::Close) FillRound(gr, SR(306, 6, 26, 26), (float)S(4), kHover);
    }
    RECT inner = {box.left + S(3), box.top + S(3), box.right - S(3), box.bottom - S(3)};
    int iconSize = S(32);
    if (t.thumb)
        DrawThumb(dc, t.thumb, inner);
    else
        DrawIconEx(dc, (box.left + box.right - iconSize) / 2, (box.top + box.bottom - iconSize) / 2,
                   t.typeIcon ? t.typeIcon : g.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);

    int left = S(78), right = rc.right - S(36);
    DrawIconEx(dc, left, S(15), g.iconSmall, S(14), S(14), 0, nullptr, DI_NORMAL);
    DrawTextIn(dc, t.header, {left + S(20), S(12), right, S(32)}, g.fontSmall, kTextSecondary, DT_END_ELLIPSIS);
    DrawTextIn(dc, t.title, {left, S(31), rc.right - S(14), S(51)}, g.fontBold, kText, DT_END_ELLIPSIS);
    DrawTextIn(dc, t.meta, {left, S(50), rc.right - S(14), S(68)}, g.fontSmall, kTextSecondary, DT_END_ELLIPSIS);
    DrawTextIn(dc, kGlyphClose, SR(306, 6, 26, 26), g.fontIconSmall, kTextTertiary, DT_CENTER);

    t.openRc = t.folderRc = {};
    t.closeRc = SR(306, 6, 26, 26);
    if (t.isFile) {
        const std::wstring open = L"열기", folder = L"폴더에서 보기", dot = L"  ·  ";
        int y0 = S(68), y1 = S(88);
        int x = left;
        int ow = TextWidth(dc, open, g.fontSmall), dw = TextWidth(dc, dot, g.fontSmall);
        int fw = TextWidth(dc, folder, g.fontSmall);
        t.openRc = {x, y0, x + ow, y1};
        t.folderRc = {x + ow + dw, y0, x + ow + dw + fw, y1};
        DrawTextIn(dc, open, t.openRc, g.fontSmall, t.hot == ToastPart::Open ? kAccentHover : kAccent, DT_LEFT);
        DrawTextIn(dc, dot, {x + ow, y0, x + ow + dw, y1}, g.fontSmall, kTextTertiary, DT_LEFT);
        DrawTextIn(dc, folder, t.folderRc, g.fontSmall, t.hot == ToastPart::Folder ? kAccentHover : kAccent, DT_LEFT);
        for (const RECT* r : {&t.openRc, &t.folderRc})
            if ((r == &t.openRc && t.hot == ToastPart::Open) || (r == &t.folderRc && t.hot == ToastPart::Folder))
                FillSolid(dc, {r->left, y1 - S(4), r->right, y1 - S(4) + 1}, kAccentHover);  // underline
    }
}

ToastPart ToastHit(POINT p) {
    const Toast& t = g.toast;
    // Links get a few pixels of slack: the text itself is small.
    auto onLink = [&](const RECT& r) {  // not "near": a windows.h macro
        RECT x = {r.left - S(3), r.top, r.right + S(3), r.bottom};
        return !IsRectEmpty(&r) && PtInRect(&x, p);
    };
    if (PtInRect(&t.closeRc, p)) return ToastPart::Close;
    if (onLink(t.openRc)) return ToastPart::Open;
    if (onLink(t.folderRc)) return ToastPart::Folder;
    return ToastPart::Body;
}

// ---- UI state -------------------------------------------------------------------------------

void SetStatusTip(const std::wstring& text) {
    g.statusTip = text;
    TOOLINFOW ti{sizeof ti};
    ti.hwnd = g.hwnd;
    ti.uId = kStatusTipId;
    ti.lpszText = g.statusTip.empty() ? const_cast<wchar_t*>(L"") : &g.statusTip[0];
    SendMessageW(g.tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

void UpdateUi() {
    bool running = g.server.IsRunning();
    SetChecked(g.c.startStop, running);
    ShowWindow(g.c.copyAddr, running ? SW_SHOW : SW_HIDE);

    if (running) {
        const FtpConfig& cfg = g.server.Config();
        std::vector<std::wstring> ips = LocalIPv4Addresses();
        g.primaryIp = PrimaryIPv4();
        if (g.primaryIp.empty() && !ips.empty()) g.primaryIp = ips[0];
        std::wstring others;
        for (const auto& ip : ips)
            if (ip != g.primaryIp) others += (others.empty() ? L"" : L", ") + ip;
        SetStatusTip(L"스캐너에 입력할 값\n서버: " + (g.primaryIp.empty() ? L"-" : g.primaryIp) +
                     L"\n포트: " + std::to_wstring(cfg.port) + L"\n사용자: " +
                     (cfg.user.empty() ? L"(아무거나)" : cfg.user) + L"\n경로: /" +
                     (others.empty() ? L"" : L"\n\n다른 IP: " + others));
    } else {
        SetStatusTip(L"");
    }
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);

    lstrcpynW(g.nid.szTip, running ? L"ScanReceiver - 실행 중" : L"ScanReceiver - 중지됨", ARRAYSIZE(g.nid.szTip));
    g.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

bool ParsePort(HWND owner, HWND h, const wchar_t* name, int& out, bool allowZero) {
    std::wstring s = GetText(h);
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(s.c_str(), &end, 10);
    if (s.empty() || *end || v > 65535 || (!allowZero && v == 0)) {
        MessageBoxW(owner, (std::wstring(name) + L" 값이 올바르지 않습니다 (1~65535).").c_str(), kAppTitle,
                    MB_ICONWARNING);
        SetFocus(h);
        return false;
    }
    out = (int)v;
    return true;
}

void OpenSettings();

void StartServer() {
    const Settings& s = g.cfg;
    FtpConfig cfg;
    cfg.port = (uint16_t)s.port;
    cfg.pasvPortMin = (uint16_t)(std::min)(s.pasvMin, s.pasvMax);
    cfg.pasvPortMax = (uint16_t)(std::max)(s.pasvMin, s.pasvMax);
    cfg.rootDir = s.root;
    cfg.user = s.user;
    cfg.password = s.pass;
    cfg.allowAnonymous = s.anon;
    if (cfg.rootDir.empty()) {
        MessageBoxW(g.hwnd, L"저장 폴더를 지정하세요.", kAppTitle, MB_ICONWARNING);
        OpenSettings();
        UpdateUi();
        return;
    }
    int rc = SHCreateDirectoryExW(nullptr, cfg.rootDir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
        MessageBoxW(g.hwnd, (L"저장 폴더를 만들 수 없습니다:\n" + cfg.rootDir).c_str(), kAppTitle, MB_ICONERROR);
        UpdateUi();
        return;
    }
    if (cfg.user.empty() && MessageBoxW(g.hwnd,
                                        L"사용자 이름이 비어 있으면 누구나 접속해 파일을 올릴 수 있습니다.\n계속할까요?",
                                        kAppTitle, MB_ICONWARNING | MB_YESNO) != IDYES) {
        UpdateUi();
        return;
    }
    std::wstring err;
    if (!g.server.Start(cfg, err)) MessageBoxW(g.hwnd, err.c_str(), kAppTitle, MB_ICONERROR);
    UpdateUi();
}

void StopServer() {
    g.server.Stop();
    UpdateUi();
}

void OnFileReceived(const FtpFileEvent& ev) {
    // Re-uploads of the same path replace the existing row; temp uploads that were renamed or
    // deleted (connection tests, "*.part" files) disappear.
    g.files.erase(std::remove_if(g.files.begin(), g.files.end(),
                                 [&](const FileItem& f) {
                                     return _wcsicmp(f.path.c_str(), ev.localPath.c_str()) == 0 ||
                                            GetFileAttributesW(f.path.c_str()) == INVALID_FILE_ATTRIBUTES;
                                 }),
                  g.files.end());
    FileItem f;
    f.path = ev.localPath;
    f.name = ev.virtualPath.size() > 1 ? ev.virtualPath.substr(1) : ev.virtualPath;
    f.size = FormatSize(ev.size);
    f.time = NowString(false);
    f.peer = ev.peer;
    f.icon = ShellIconIndex(ev.localPath);
    g.files.insert(g.files.begin(), f);
    g.listSel = 0;
    g.listTop = 0;
    g.listHover = -1;
    InvalidateRect(g.c.list, nullptr, FALSE);
    RECT group = SR(0, kGroupTop, kW, kGroupH);
    InvalidateRect(g.hwnd, &group, FALSE);

    // In the tray (or minimized): a toast with the page preview. Open widget: the list shows it.
    if (!IsWindowVisible(g.hwnd) || IsIconic(g.hwnd)) ToastShowFile(f);
}

void RestoreWindow() {
    ShowWindow(g.hwnd, SW_SHOW);
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
}

void HideToTray() {
    HidePopup(g.logWnd, g.logAnim);
    ShowWindow(g.settingsWnd, SW_HIDE);
    ShowWindow(g.hwnd, SW_HIDE);
    // Once per run, say where the window went so closing doesn't look like quitting.
    if (!g.trayHintShown) {
        g.trayHintShown = true;
        ToastMessage(L"트레이에서 계속 실행 중", L"스캔 파일은 계속 받습니다.", L"끄려면 트레이 아이콘 우클릭 → 종료");
    }
}

void CloseWidget() {
    if (g.cfg.toTray)
        HideToTray();
    else
        DestroyWindow(g.hwnd);
}

// ---- main widget ----------------------------------------------------------------------------

void CreateMainControls() {
    Controls& c = g.c;
    HWND p = g.hwnd;
    c.settings = MakeButton(p, kGlyphSettings, IDC_SETTINGS, BtnKind::Icon, kBg);
    c.close = MakeButton(p, kGlyphClose, IDC_CLOSE, BtnKind::Icon, kBg);
    g.btns[c.close].danger = true;
    c.startStop = MakeButton(p, L"", IDC_STARTSTOP, BtnKind::Switch, kCard);
    c.copyAddr = MakeButton(p, kGlyphCopy, IDC_COPYADDR, BtnKind::Icon, kCard);
    c.openDir = MakeButton(p, L"폴더 열기", IDC_OPENDIR, BtnKind::Link, kBg);
    g.btns[c.openDir].bold = true;
    c.showLog = MakeButton(p, L"로그", IDC_SHOWLOG, BtnKind::Link, kBg);
    c.list = CreateWindowExW(0, kFileListClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, p, (HMENU)IDC_FILELIST,
                             g.inst, nullptr);

    Place(c.settings, kW - 8 - 28 - 28, 4, 28, 28);
    Place(c.close, kW - 8 - 28, 4, 28, 28);
    Place(c.startStop, kW - 10 - 12 - 40, kCardTop + 10, 40, 20);
    Place(c.copyAddr, kW - 10 - 6 - 26, kCardTop + 31, 26, 22);
    Place(c.list, 0, kListTop, kW, kListH);
    Place(c.openDir, 0, kFooterTop, kW / 2, kFooterH);
    Place(c.showLog, kW / 2, kFooterTop, kW / 2, kFooterH);

    // Full scanner settings live in a tooltip on the status card.
    g.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0,
                            0, g.hwnd, nullptr, g.inst, nullptr);
    TOOLINFOW ti{sizeof ti};
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = g.hwnd;
    ti.uId = kStatusTipId;
    ti.rect = SR(10, kCardTop, kW - 20, kCardH);
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(g.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(400));
    SendMessageW(g.tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
}

void PaintMain(HWND hwnd, HDC target) {
    Buffer buf(hwnd, target);
    HDC dc = buf.dc;
    RECT rc = buf.rc;
    FillSolid(dc, rc, kBg);

    bool running = g.server.IsRunning();
    RECT card = SR(10, kCardTop, kW - 20, kCardH);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        FillRound(gr, card, (float)S(4), kCard, kCardBorder);
        FillCircle(gr, (float)S(10 + 16), (float)S(kCardTop + 20), (float)S(4), running ? kRunDot : kStopDot);
    }
    DrawTextIn(dc, g.testInstance ? L"스캔 수신 (테스트)" : L"스캔 수신", SR(12, 0, 150, kHeaderH), g.font, kText, 0);

    int cl = 10 + 12, cr = kW - 10 - 12;
    DrawTextIn(dc, running ? L"실행 중" : L"중지됨", SR(cl + 12, kCardTop + 10, 120, 20), g.fontBold, kText, 0);
    std::wstring line2 = running ? L"ftp://" + (g.primaryIp.empty() ? std::wstring(L"IP 확인 불가") : g.primaryIp) +
                                       L":" + std::to_wstring(g.server.Config().port)
                                 : std::wstring(L"스위치를 켜면 수신을 시작합니다");
    DrawTextIn(dc, line2, SR(cl, kCardTop + 32, cr - cl - (running ? 30 : 0), 20), running ? g.fontMono : g.fontSmall,
               running ? kTextSecondary : kTextTertiary, DT_END_ELLIPSIS);
    std::wstring who = g.cfg.user.empty() ? std::wstring(L"사용자 없음") : L"사용자 " + g.cfg.user;
    DrawTextIn(dc, who + L"  ·  " + ShortPath(g.cfg.root), SR(cl, kCardTop + 52, cr - cl, 18), g.fontSmall,
               kTextTertiary, DT_END_ELLIPSIS);

    std::wstring group = L"받은 파일";
    int gw = TextWidth(dc, group, g.fontBold);
    DrawTextIn(dc, group, SR(12, kGroupTop, 120, kGroupH), g.fontBold, kTextSecondary, 0);
    if (!g.files.empty())
        DrawTextIn(dc, L"·  " + std::to_wstring(g.files.size()), {S(12) + gw + S(6), S(kGroupTop), S(kW), S(kGroupTop + kGroupH)},
                   g.fontSmall, kTextTertiary, 0);

    FillSolid(dc, {0, S(kFooterTop), rc.right, S(kFooterTop) + 1}, kDivider);
    FillSolid(dc, {S(kW / 2), S(kFooterTop + 8), S(kW / 2) + 1, S(kFooterTop + kFooterH - 8)}, kDivider);
}

// ---- settings window ------------------------------------------------------------------------

void CreateSettingsControls(HWND p) {
    Controls& c = g.c;
    c.setClose = MakeButton(p, kGlyphClose, IDC_SETCLOSE, BtnKind::Icon, kBg);
    g.btns[c.setClose].danger = true;
    c.lblRoot = Make(p, L"STATIC", L"저장 폴더", SS_LEFT | SS_CENTERIMAGE);
    c.root = MakeEdit(p, IDC_ROOT);
    c.browse = MakeButton(p, kGlyphFolder, IDC_BROWSE, BtnKind::Icon, kBg);
    g.btns[c.browse].bordered = true;
    c.lblPort = Make(p, L"STATIC", L"포트", SS_LEFT | SS_CENTERIMAGE);
    c.port = MakeEdit(p, IDC_PORT, ES_NUMBER);
    c.lblPasv = Make(p, L"STATIC", L"패시브", SS_CENTER | SS_CENTERIMAGE);
    c.pasvMin = MakeEdit(p, IDC_PASVMIN, ES_NUMBER);
    c.lblTilde = Make(p, L"STATIC", L"–", SS_CENTER | SS_CENTERIMAGE);
    c.pasvMax = MakeEdit(p, IDC_PASVMAX, ES_NUMBER);
    c.lblUser = Make(p, L"STATIC", L"사용자", SS_LEFT | SS_CENTERIMAGE);
    c.user = MakeEdit(p, IDC_USER);
    c.lblPass = Make(p, L"STATIC", L"비밀번호", SS_LEFT | SS_CENTERIMAGE);
    c.pass = MakeEdit(p, IDC_PASS, ES_PASSWORD);
    SendMessageW(c.pass, EM_SETPASSWORDCHAR, kPasswordChar, 0);
    c.showPass = MakeButton(p, L"표시", IDC_SHOWPASS, BtnKind::Subtle, kBg);
    g.btns[c.showPass].toggles = true;
    c.anon = MakeButton(p, L"익명 접속 허용", IDC_ANON, BtnKind::Check, kBg);
    c.autoStart = MakeButton(p, L"켤 때 서버 자동 시작", IDC_AUTOSTART, BtnKind::Check, kBg);
    c.toTray = MakeButton(p, L"닫기·최소화 시 트레이로", IDC_TOTRAY, BtnKind::Check, kBg);
    c.runAtLogon = MakeButton(p, L"Windows 시작 시 실행", IDC_RUNATLOGON, BtnKind::Check, kBg);
    c.save = MakeButton(p, L"저장", IDC_SAVE, BtnKind::Primary, kFooterBg);
    c.cancel = MakeButton(p, L"취소", IDC_CANCEL, BtnKind::Secondary, kFooterBg);

    const int L = 16, F = 100, R = kSetW - 16;
    Place(c.setClose, kSetW - 8 - 28, 4, 28, 28);
    int y = 44;
    Place(c.lblRoot, L, y, 80, kFieldH);
    PlaceField(c.root, F, y, R - F - 34);
    Place(c.browse, R - 30, y, 30, kFieldH);
    y += 38;
    Place(c.lblPort, L, y, 80, kFieldH);
    PlaceField(c.port, F, y, 56);
    Place(c.lblPasv, F + 58, y, 46, kFieldH);
    PlaceField(c.pasvMin, F + 104, y, 54);
    Place(c.lblTilde, F + 158, y, 12, kFieldH);
    PlaceField(c.pasvMax, F + 170, y, R - F - 170);
    y += 38;
    Place(c.lblUser, L, y, 80, kFieldH);
    PlaceField(c.user, F, y, R - F);
    y += 38;
    Place(c.lblPass, L, y, 80, kFieldH);
    PlaceField(c.pass, F, y, R - F - 48);
    Place(c.showPass, R - 44, y, 44, kFieldH);
    y += 42;
    for (HWND h : {c.anon, c.autoStart, c.toTray, c.runAtLogon}) {
        Place(h, F, y, R - F, 24);
        y += 26;
    }
    Place(c.save, kSetW - 12 - 72 - 6 - 72, kSetFooterTop + 11, 72, 28);
    Place(c.cancel, kSetW - 12 - 72, kSetFooterTop + 11, 72, 28);
}

void FillSettingsControls() {
    const Settings& s = g.cfg;
    Controls& c = g.c;
    SetWindowTextW(c.root, s.root.c_str());
    SetWindowTextW(c.port, std::to_wstring(s.port).c_str());
    SetWindowTextW(c.pasvMin, std::to_wstring(s.pasvMin).c_str());
    SetWindowTextW(c.pasvMax, std::to_wstring(s.pasvMax).c_str());
    SetWindowTextW(c.user, s.user.c_str());
    SetWindowTextW(c.pass, s.pass.c_str());
    SetChecked(c.showPass, false);
    SetWindowTextW(c.showPass, L"표시");
    SendMessageW(c.pass, EM_SETPASSWORDCHAR, kPasswordChar, 0);
    SetChecked(c.anon, s.anon);
    SetChecked(c.autoStart, s.autoStart);
    SetChecked(c.toTray, s.toTray);
    SetChecked(c.runAtLogon, RunAtLogonEnabled());
}

void OpenSettings() {
    if (!IsWindowVisible(g.hwnd)) RestoreWindow();
    if (!IsWindowVisible(g.settingsWnd)) {
        FillSettingsControls();
        POINT p = PopupTargetPos(g.settingsWnd);
        SetWindowPos(g.settingsWnd, nullptr, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(g.settingsWnd, SW_SHOW);
        SetFocus(g.c.root);
    }
    SetForegroundWindow(g.settingsWnd);
}

// Validates and stores the dialog; a running server restarts when a setting it uses changed.
bool SaveSettingsDialog() {
    Controls& c = g.c;
    HWND w = g.settingsWnd;
    Settings s = g.cfg;
    if (!ParsePort(w, c.port, L"FTP 포트", s.port, false) || !ParsePort(w, c.pasvMin, L"패시브 시작 포트", s.pasvMin, true) ||
        !ParsePort(w, c.pasvMax, L"패시브 끝 포트", s.pasvMax, true))
        return false;
    s.root = GetText(c.root);
    if (s.root.empty()) {
        MessageBoxW(w, L"저장 폴더를 지정하세요.", kAppTitle, MB_ICONWARNING);
        SetFocus(c.root);
        return false;
    }
    s.user = GetText(c.user);
    s.pass = GetText(c.pass);
    s.anon = IsChecked(c.anon);
    s.autoStart = IsChecked(c.autoStart);
    s.toTray = IsChecked(c.toTray);
    const Settings& o = g.cfg;
    bool serverChanged = s.root != o.root || s.port != o.port || s.pasvMin != o.pasvMin || s.pasvMax != o.pasvMax ||
                         s.user != o.user || s.pass != o.pass || s.anon != o.anon;
    g.cfg = s;
    SaveSettings();
    if (IsChecked(c.runAtLogon) != RunAtLogonEnabled()) SetRunAtLogon(IsChecked(c.runAtLogon));
    ShowWindow(w, SW_HIDE);
    if (serverChanged && g.server.IsRunning()) {
        g.server.Stop();
        StartServer();
    }
    UpdateUi();
    return true;
}

void PaintSettings(HWND hwnd, HDC target) {
    Buffer buf(hwnd, target);
    HDC dc = buf.dc;
    RECT rc = buf.rc;
    FillSolid(dc, rc, kBg);
    FillSolid(dc, {0, S(kSetFooterTop), rc.right, rc.bottom}, kFooterBg);
    FillSolid(dc, {0, S(kSetFooterTop), rc.right, S(kSetFooterTop) + 1}, kDivider);
    DrawTextIn(dc, L"설정", SR(12, 0, 200, kHeaderH), g.font, kText, 0);
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        for (const auto& f : g.fields) FillRound(gr, f.second, (float)S(4), kField, kCardBorder);
    }
    // Text-box underline: grey at rest, 2 px accent while focused.
    for (const auto& f : g.fields) {
        bool focused = GetFocus() == f.first;
        FillSolid(dc, {f.second.left + S(3), f.second.bottom - (focused ? S(2) : 1), f.second.right - S(3), f.second.bottom},
                  focused ? kAccent : kFieldUnderline);
    }
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        RoundCorners(hwnd);
        CreateSettingsControls(hwnd);
        return 0;
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_NCHITTEST:
        return BorderlessHitTest(hwnd, lp);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintSettings(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, kText);
        SetBkColor((HDC)wp, kField);
        return (LRESULT)g.fieldBrush;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, (HWND)lp == g.c.lblPasv || (HWND)lp == g.c.lblTilde ? kTextSecondary : kText);
        return (LRESULT)g.bgBrush;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {  // repaint the text-box underline
            for (const auto& f : g.fields)
                if (f.first == (HWND)lp) InvalidateRect(hwnd, &f.second, FALSE);
            return 0;
        }
        switch (id) {
        case IDOK:  // Enter
        case IDC_SAVE:
            SaveSettingsDialog();
            break;
        case IDCANCEL:  // Esc
        case IDC_CANCEL:
        case IDC_SETCLOSE:
            ShowWindow(hwnd, SW_HIDE);
            break;
        case IDC_BROWSE: {
            std::wstring path = GetText(g.c.root);
            if (PickFolder(hwnd, path)) SetWindowTextW(g.c.root, path.c_str());
            break;
        }
        case IDC_SHOWPASS:
            SendMessageW(g.c.pass, EM_SETPASSWORDCHAR, IsChecked(g.c.showPass) ? 0 : kPasswordChar, 0);
            SetWindowTextW(g.c.showPass, IsChecked(g.c.showPass) ? L"숨김" : L"표시");
            InvalidateRect(g.c.pass, nullptr, TRUE);
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- log window -----------------------------------------------------------------------------

LRESULT CALLBACK LogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetCaptionColor(hwnd, RGB(255, 255, 255));
        g.c.verbose = MakeButton(hwnd, L"FTP 명령까지 자세히 보기", IDC_VERBOSE, BtnKind::Switch, RGB(255, 255, 255));
        g.c.clearLog = MakeButton(hwnd, L"지우기", IDC_CLEARLOG, BtnKind::Secondary, RGB(255, 255, 255));
        g.c.log = Make(hwnd, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL, IDC_LOG);
        return 0;
    case WM_SIZE: {
        int W = MulDiv(LOWORD(lp), 96, g.dpi), H = MulDiv(HIWORD(lp), 96, g.dpi);
        Place(g.c.verbose, 12, 9, 260, 24);
        Place(g.c.clearLog, W - 12 - 72, 6, 72, 30);
        Place(g.c.log, 12, 47, W - 12, H - 47);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize = {S(360), S(200)};
        return 0;
    case WM_TIMER:
        if (wp == kAnimTimer && AnimStep(hwnd, g.logAnim) && g.logAnim.showing) {
            // Layered windows repaint children through a redirection bitmap; drop it once opaque.
            SetLayered(hwnd, false);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CLEARLOG) {
            g.logLines.clear();
            SendMessageW(g.c.log, LB_RESETCONTENT, 0, 0);
        } else if (LOWORD(wp) == IDC_VERBOSE && HIWORD(wp) == BN_CLICKED) {
            g.cfg.verbose = IsChecked(g.c.verbose);
            RebuildLog();
            SaveSettings();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillSolid(dc, {0, S(42), rc.right, S(42) + 1}, kDivider);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CLOSE:
        HidePopup(hwnd, g.logAnim);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- toast window ---------------------------------------------------------------------------

LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Toast& t = g.toast;
    switch (msg) {
    case WM_CREATE:
        RoundCorners(hwnd);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintToast(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        ToastPart hot = ToastHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hot != t.hot) {
            t.hot = hot;
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        t.hot = ToastPart::None;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (t.hot == ToastPart::Open || t.hot == ToastPart::Folder) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (wp == kAnimTimer && AnimStep(hwnd, t.anim) && t.anim.showing)
            SetTimer(hwnd, kToastHoldTimer, t.holdMs, nullptr);
        if (wp == kToastHoldTimer) {
            // Keep it up while the pointer rests on it.
            POINT pt;
            RECT r;
            GetCursorPos(&pt);
            GetWindowRect(hwnd, &r);
            if (PtInRect(&r, pt))
                SetTimer(hwnd, kToastHoldTimer, 600, nullptr);
            else
                ToastHide();
        }
        return 0;
    case WM_LBUTTONUP: {
        ToastPart part = ToastHit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        std::wstring path = t.path;
        ToastHide();
        if (part == ToastPart::Open)
            OpenFile(path);
        else if (part == ToastPart::Folder)
            ShowInFolder(path);
        else if (part == ToastPart::Body)
            RestoreWindow();
        return 0;
    }
    case WM_APP_THUMB: {
        std::unique_ptr<ThumbResult> r((ThumbResult*)lp);
        if (r->bmp && r->path == t.path) {
            ToastSetThumb(r->bmp);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (r->bmp) {
            DeleteObject(r->bmp);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- tray -----------------------------------------------------------------------------------

void AddTrayIcon() {
    g.nid.cbSize = sizeof g.nid;
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_APP_TRAY;
    g.nid.hIcon = g.iconSmall;
    lstrcpynW(g.nid.szTip, g.testInstance ? kTestTitle : kAppTitle, ARRAYSIZE(g.nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

void TrayMenu() {
    ShowPopupMenu({{IDM_TRAY_SHOW, L"열기"}, {IDM_TRAY_SETTINGS, L"설정"}, {IDM_TRAY_LOG, L"로그 보기"}, {0, nullptr},
                   {IDM_TRAY_EXIT, L"종료"}},
                  IDM_TRAY_SHOW);
}

// ---- main window procedure ------------------------------------------------------------------

void OnCommand(int id, int code) {
    switch (id) {
    case IDC_STARTSTOP:
        if (code == BN_CLICKED) g.server.IsRunning() ? StopServer() : StartServer();
        break;
    case IDC_SETTINGS:
    case IDM_TRAY_SETTINGS:
        OpenSettings();
        break;
    case IDC_CLOSE:
        CloseWidget();
        break;
    case IDC_OPENDIR:
        SHCreateDirectoryExW(nullptr, g.cfg.root.c_str(), nullptr);
        ShellExecuteW(g.hwnd, L"open", g.cfg.root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case IDC_COPYADDR:
        if (!g.primaryIp.empty() && CopyToClipboard(g.primaryIp)) {
            SetWindowTextW(g.c.copyAddr, kGlyphCheck);
            SetTimer(g.hwnd, kCopyResetTimer, 1500, nullptr);
        }
        break;
    case IDC_SHOWLOG:
        TogglePopup(g.logWnd, g.logAnim);
        break;
    case IDM_TRAY_LOG:
        RestoreWindow();
        ShowPopup(g.logWnd, g.logAnim);
        break;
    case IDM_OPEN:
        if (!SelectedFile().empty()) OpenFile(SelectedFile());
        break;
    case IDM_SHOW_IN_FOLDER:
        if (!SelectedFile().empty()) ShowInFolder(SelectedFile());
        break;
    case IDM_CLEAR_LIST:
        g.files.clear();
        g.listTop = 0;
        g.listSel = g.listHover = -1;
        RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        break;
    case IDM_TRAY_SHOW:
        RestoreWindow();
        break;
    case IDM_TRAY_EXIT:
        DestroyWindow(g.hwnd);
        break;
    }
}

void SaveWindowPos() {
    if (IsIconic(g.hwnd)) return;
    RECT r;
    GetWindowRect(g.hwnd, &r);
    IniSet(L"WindowX", std::to_wstring(r.left));
    IniSet(L"WindowY", std::to_wstring(r.top));
}

// Saved position if it is still on a monitor, else the bottom-right corner of the primary work area.
POINT InitialPos() {
    int w = S(kW), h = S(kH);
    // Read as text: GetPrivateProfileInt turns the negative coordinates of a left-hand monitor into 0.
    std::wstring sx = IniGet(L"WindowX", L""), sy = IniGet(L"WindowY", L"");
    int x = _wtoi(sx.c_str()), y = _wtoi(sy.c_str());
    RECT r = {x, y, x + w, y + h};
    if (!sx.empty() && !sy.empty() && MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return {x, y};
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    return {wa.right - w - S(24), wa.bottom - h - S(24)};
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        RoundCorners(hwnd);
        CreateMainControls();
        g.logWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kLogClass, L"로그", WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, S(600), S(300), hwnd, nullptr, g.inst, nullptr);
        g.settingsWnd = CreateWindowExW(0, kSettingsClass, L"ScanReceiver 설정", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
                                        0, 0, S(kSetW), S(kSetH), hwnd, nullptr, g.inst, nullptr);
        g.toast.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                       kToastClass, L"", WS_POPUP, 0, 0, S(340), S(96), nullptr, nullptr, g.inst,
                                       nullptr);
        SetChecked(g.c.verbose, g.cfg.verbose);
        AddTrayIcon();
        g.server.SetLogHandler([](const std::wstring& m) {
            auto* p = new std::wstring(m);
            if (!PostMessageW(g.hwnd, WM_APP_LOG, 0, (LPARAM)p)) delete p;
        });
        g.server.SetFileHandler([](const FtpFileEvent& ev) {
            auto* p = new FtpFileEvent(ev);
            if (!PostMessageW(g.hwnd, WM_APP_FILE, 0, (LPARAM)p)) delete p;
        });
        UpdateUi();
        if (g.cfg.autoStart) StartServer();
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;  // the client area covers the whole window; DWM still draws border and shadow
        break;
    case WM_NCHITTEST:
        return BorderlessHitTest(hwnd, lp);
    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP:  // the header: same menu as the tray icon
        if (wp == HTCAPTION) TrayMenu();
        return 0;
    case WM_CLOSE:
        // Alt+F4 behaves like ✕: to the tray, unless that is turned off. The tray menu's "종료" really exits.
        CloseWidget();
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            HidePopup(g.logWnd, g.logAnim);
            if (g.cfg.toTray) HideToTray();
        }
        return 0;
    case WM_EXITSIZEMOVE:
        SaveWindowPos();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintMain(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;  // WM_PAINT fills everything from a back buffer
    case WM_TIMER:
        if (wp == kCopyResetTimer) {
            KillTimer(hwnd, kCopyResetTimer);
            SetWindowTextW(g.c.copyAddr, kGlyphCopy);
        }
        return 0;
    case WM_MOUSEWHEEL:  // wheel over the widget scrolls the file list
        SendMessageW(g.c.list, msg, wp, lp);
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> p((std::wstring*)lp);
        AddLog(*p);
        return 0;
    }
    case WM_APP_FILE: {
        std::unique_ptr<FtpFileEvent> p((FtpFileEvent*)lp);
        OnFileReceived(*p);
        return 0;
    }
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
            RestoreWindow();
        else if (LOWORD(lp) == WM_RBUTTONUP)
            TrayMenu();
        return 0;
    case WM_DESTROY:
        SaveWindowPos();
        g.server.Stop();
        SaveSettings();
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        DestroyWindow(g.toast.hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterWindowClass(const wchar_t* name, WNDPROC proc, HBRUSH background, UINT style = 0) {
    WNDCLASSEXW wc{sizeof wc};
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = g.inst;
    wc.hIcon = g.icon;
    wc.hIconSm = g.iconSmall;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = background;
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g.inst = inst;
    g.iniPath = ResolveIniPath();
    g.testInstance = HasArg(L"--ini");

    // Test instances (--ini) run beside the user's copy; everything else is single-instance.
    HANDLE mutex = nullptr;
    if (!g.testInstance) {
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\ScanReceiver.SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND other = FindWindowW(kWindowClass, kAppTitle)) {
                ShowWindow(other, SW_SHOW);
                ShowWindow(other, SW_RESTORE);
                SetForegroundWindow(other);
            }
            return 0;
        }
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    HDC screen = GetDC(nullptr);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    g.fontBold = CreateFontIndirectW(&bold);
    LOGFONTW smaller = ncm.lfMessageFont;  // not "small": rpcndr.h #defines it
    smaller.lfHeight = smaller.lfHeight * 9 / 10;
    g.fontSmall = CreateFontIndirectW(&smaller);
    LOGFONTW icons{};
    icons.lfHeight = -S(14);
    icons.lfCharSet = DEFAULT_CHARSET;
    icons.lfQuality = ANTIALIASED_QUALITY;  // ClearType colour fringes look wrong on glyph icons
    lstrcpyW(icons.lfFaceName, HasFontFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets");
    g.fontIcon = CreateFontIndirectW(&icons);
    icons.lfHeight = -S(10);
    g.fontIconSmall = CreateFontIndirectW(&icons);
    LOGFONTW mono = ncm.lfMessageFont;
    mono.lfHeight = smaller.lfHeight;
    lstrcpyW(mono.lfFaceName, L"Consolas");
    g.fontMono = CreateFontIndirectW(&mono);
    {
        HDC sdc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(sdc, g.font);
        TEXTMETRICW tm{};
        GetTextMetricsW(sdc, &tm);
        g.fontH = tm.tmHeight;
        SelectObject(sdc, old);
        ReleaseDC(nullptr, sdc);
    }

    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    auto loadIcon = [inst](int cx, int cy) {
        HICON h = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(cx),
                                    GetSystemMetrics(cy), LR_DEFAULTCOLOR);
        return h ? h : LoadIconW(nullptr, IDI_APPLICATION);
    };
    g.icon = loadIcon(SM_CXICON, SM_CYICON);
    g.iconSmall = loadIcon(SM_CXSMICON, SM_CYSMICON);
    SHFILEINFOW sfi{};
    g.sysImages = (HIMAGELIST)SHGetFileInfoW(L".pdf", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                                             SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);

    LoadSettings();
    if (!g.testInstance) RefreshRunAtLogon();

    g.bgBrush = CreateSolidBrush(kBg);
    g.fieldBrush = CreateSolidBrush(kField);
    RegisterWindowClass(kWindowClass, WndProc, g.bgBrush);
    RegisterWindowClass(kLogClass, LogProc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    RegisterWindowClass(kSettingsClass, SettingsProc, g.bgBrush);
    RegisterWindowClass(kFileListClass, ListProc, g.bgBrush, CS_DBLCLKS);
    RegisterWindowClass(kToastClass, ToastProc, g.bgBrush, CS_DROPSHADOW);

    // Borderless widget (QuickLinks style): WS_THICKFRAME only so DWM draws the border and shadow.
    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    POINT pos = InitialPos();
    HWND hwnd = CreateWindowExW(0, kWindowClass, g.testInstance ? kTestTitle : kAppTitle, style, pos.x, pos.y, S(kW),
                                S(kH), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // Started by Windows at logon: stay in the tray.
    if (!HasArg(L"--startup")) {
        ShowWindow(hwnd, g.testInstance ? SW_SHOWNOACTIVATE : show);
        UpdateWindow(hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg) && !IsDialogMessageW(g.settingsWnd, &msg) &&
            !IsDialogMessageW(g.logWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    ToastSetThumb(nullptr);
    ToastSetTypeIcon(nullptr);
    for (HGDIOBJ o : {(HGDIOBJ)g.font, (HGDIOBJ)g.fontBold, (HGDIOBJ)g.fontSmall, (HGDIOBJ)g.fontIcon,
                      (HGDIOBJ)g.fontIconSmall, (HGDIOBJ)g.fontMono, (HGDIOBJ)g.bgBrush, (HGDIOBJ)g.fieldBrush})
        DeleteObject(o);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    WSACleanup();
    if (mutex) CloseHandle(mutex);
    return 0;
}
