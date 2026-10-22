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
#endif

namespace {

enum : int {
    IDI_APP = 1,
    IDC_ROOT = 100,
    IDC_BROWSE,
    IDC_OPENDIR,
    IDC_PORT,
    IDC_PASVMIN,
    IDC_PASVMAX,
    IDC_USER,
    IDC_PASS,
    IDC_SHOWPASS,
    IDC_ANON,
    IDC_AUTOSTART,
    IDC_TOTRAY,
    IDC_STARTSTOP,
    IDC_STATUS,
    IDC_COPYADDR,
    IDC_SHOWLOG,
    IDC_SHOWFILES,
    IDC_FILES,
    IDC_CLEARFILES,
    IDC_LOG,
    IDC_VERBOSE,
    IDC_CLEARLOG,
    IDM_OPEN = 200,
    IDM_SHOW_IN_FOLDER,
    IDM_TRAY_SHOW,
    IDM_TRAY_LOG,
    IDM_TRAY_EXIT,
};

enum : UINT { WM_APP_LOG = WM_APP + 1, WM_APP_FILE, WM_APP_TRAY };

enum : UINT_PTR { kCopyResetTimer = 1, kAnimTimer = 100, kToastHoldTimer = 101 };

constexpr wchar_t kWindowClass[] = L"ScanReceiverMainWindow";
constexpr wchar_t kLogClass[] = L"ScanReceiverLogWindow";
constexpr wchar_t kFilesClass[] = L"ScanReceiverFilesWindow";
constexpr wchar_t kToastClass[] = L"ScanReceiverToast";
constexpr wchar_t kAppTitle[] = L"ScanReceiver";
constexpr wchar_t kPasswordChar = L'\x25CF';
constexpr size_t kMaxLogLines = 2000;
constexpr UINT_PTR kStatusTipId = 1;

constexpr int kToastHoldMs = 2000;
constexpr int kShowMs = 280;
constexpr int kHideMs = 200;

// Fluent (Windows 11) light palette.
constexpr COLORREF kBg = RGB(243, 243, 243);
constexpr COLORREF kCard = RGB(255, 255, 255);
constexpr COLORREF kCardBorder = RGB(229, 229, 229);
constexpr COLORREF kDivider = RGB(235, 235, 235);
constexpr COLORREF kText = RGB(26, 26, 26);
constexpr COLORREF kTextSecondary = RGB(96, 96, 96);
constexpr COLORREF kTextDisabled = RGB(160, 160, 160);
constexpr COLORREF kAccent = RGB(0, 103, 192);
constexpr COLORREF kAccentHover = RGB(25, 117, 197);
constexpr COLORREF kAccentPressed = RGB(49, 131, 202);
constexpr COLORREF kControl = RGB(253, 253, 253);
constexpr COLORREF kControlHover = RGB(246, 246, 246);
constexpr COLORREF kControlPressed = RGB(240, 240, 240);
constexpr COLORREF kControlBorder = RGB(213, 213, 213);
constexpr COLORREF kSubtleHover = RGB(234, 234, 234);
constexpr COLORREF kFieldDisabled = RGB(249, 249, 249);
constexpr COLORREF kFieldUnderline = RGB(138, 138, 138);
constexpr COLORREF kDisabledFill = RGB(200, 200, 200);
constexpr COLORREF kRunBg = RGB(223, 246, 221), kRunFg = RGB(16, 124, 16);
constexpr COLORREF kStopBg = RGB(233, 233, 233), kStopFg = RGB(95, 95, 95), kStopDot = RGB(138, 138, 138);

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
    HWND copyAddr, showFiles, showLog, startStop;
    HWND lblRoot, root, browse, openDir;
    HWND lblPort, port, lblUser, user, lblPass, pass, showPass;
    HWND lblPasv, pasvMin, lblTilde, pasvMax, anon, autoStart, toTray;
    // files window
    HWND clearFiles, files;
    // log window
    HWND verbose, clearLog, log;
};

// Owner-drawn buttons: look, the surface they sit on (for anti-aliased corners), and live state.
enum class BtnKind { Primary, Secondary, Subtle, Switch };

struct BtnInfo {
    BtnKind kind;
    COLORREF surface;
    bool toggles = false;  // Switch, or a Subtle button that flips (password "show")
    bool checked = false;
    bool hover = false;
};

struct Toast {
    HWND hwnd = nullptr;
    Anim anim;
    int count = 0;
    std::wstring title, line1, line2;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND logWnd = nullptr;
    HWND filesWnd = nullptr;
    HWND tip = nullptr;
    Anim logAnim;
    Anim filesAnim;
    Toast toast;
    HFONT font = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontSmall = nullptr;
    HICON icon = nullptr;       // large (Alt+Tab, taskbar, toast)
    HICON iconSmall = nullptr;  // title bar, tray: the 16/20/24 px frame, not a shrunken 32 px one
    int dpi = 96;
    int fontH = 16;
    RECT chip{};  // status pill (painted)
    RECT card{};  // settings card (painted)
    std::wstring statusText;
    std::vector<std::pair<HWND, RECT>> fields;  // edit -> the box painted around it
    std::map<HWND, BtnInfo> btns;
    HBRUSH bgBrush = nullptr, cardBrush = nullptr, fieldDisabledBrush = nullptr;
    Controls c{};
    FtpServer server;
    std::vector<std::wstring> receivedPaths;  // indexed by list-view item lParam
    std::deque<std::wstring> logLines;        // full log; the list box may show a filtered view
    std::wstring primaryIp;
    std::wstring statusTip;
    bool trayHintShown = false;
    NOTIFYICONDATAW nid{};
    std::wstring iniPath;
};

App g;

int S(int v) { return MulDiv(v, g.dpi, 96); }

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

std::wstring NowString(bool withDate = false) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    if (withDate)
        swprintf(buf, 32, L"%02u-%02u %02u:%02u:%02u", st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    else
        swprintf(buf, 32, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
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

bool SystemUsesLightTheme() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
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

// ---- settings (ScanReceiver.ini next to the exe) ---------------------------------------------

std::wstring IniGet(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"Settings", key, def, buf, 1024, g.iniPath.c_str());
    return buf;
}

void IniSet(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(L"Settings", key, v.c_str(), g.iniPath.c_str());
}

void LoadSettings() {
    SetWindowTextW(g.c.root, IniGet(L"RootDir", DefaultRoot().c_str()).c_str());
    SetWindowTextW(g.c.port, IniGet(L"Port", L"21").c_str());
    SetWindowTextW(g.c.pasvMin, IniGet(L"PasvMin", L"50000").c_str());
    SetWindowTextW(g.c.pasvMax, IniGet(L"PasvMax", L"50100").c_str());
    SetWindowTextW(g.c.user, IniGet(L"User", L"scan").c_str());
    SetWindowTextW(g.c.pass, IniGet(L"Password", L"scan1234").c_str());
    SetChecked(g.c.anon, IniGet(L"AllowAnonymous", L"0") == L"1");
    SetChecked(g.c.autoStart, IniGet(L"AutoStart", L"0") == L"1");
    SetChecked(g.c.toTray, IniGet(L"MinimizeToTray", L"1") == L"1");
    SetChecked(g.c.verbose, IniGet(L"VerboseLog", L"0") == L"1");
}

void SaveSettings() {
    IniSet(L"RootDir", GetText(g.c.root));
    IniSet(L"Port", GetText(g.c.port));
    IniSet(L"PasvMin", GetText(g.c.pasvMin));
    IniSet(L"PasvMax", GetText(g.c.pasvMax));
    IniSet(L"User", GetText(g.c.user));
    IniSet(L"Password", GetText(g.c.pass));
    IniSet(L"AllowAnonymous", IsChecked(g.c.anon) ? L"1" : L"0");
    IniSet(L"AutoStart", IsChecked(g.c.autoStart) ? L"1" : L"0");
    IniSet(L"MinimizeToTray", IsChecked(g.c.toTray) ? L"1" : L"0");
    IniSet(L"VerboseLog", IsChecked(g.c.verbose) ? L"1" : L"0");
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
    if (IsChecked(g.c.verbose) || !IsProtocolLine(line)) AppendLogRow(line);
}

void RebuildLog() {
    bool verbose = IsChecked(g.c.verbose);
    SendMessageW(g.c.log, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.c.log, LB_RESETCONTENT, 0, 0);
    for (const auto& line : g.logLines)
        if (verbose || !IsProtocolLine(line)) AppendLogRow(line);
    SendMessageW(g.c.log, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.c.log, nullptr, TRUE);
}

// Popups dock beside the main window (right, else left): the files window level with its top,
// the log window under the files window when that is open.
POINT PopupTargetPos(HWND w) {
    RECT m, r;
    GetWindowRect(g.hwnd, &m);
    GetWindowRect(w, &r);
    int pw = r.right - r.left, ph = r.bottom - r.top;
    RECT wa = WorkAreaOf(g.hwnd);
    int gap = S(4);
    int y = m.top;
    if (w == g.logWnd && IsShownOrShowing(g.filesWnd, g.filesAnim)) {
        RECT f;
        GetWindowRect(g.filesWnd, &f);
        int top = g.filesAnim.active ? g.filesAnim.to.y : f.top;  // where it is heading, mid-animation
        y = top + (f.bottom - f.top) + gap;
    }
    y = (std::max)((int)wa.top, (std::min)(y, (int)wa.bottom - ph));
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

// ---- toast (shown while the main window is in the tray) -------------------------------------

void ToastHide() {
    Toast& t = g.toast;
    KillTimer(t.hwnd, kToastHoldTimer);
    if (!IsShownOrShowing(t.hwnd, t.anim)) return;
    RECT r;
    GetWindowRect(t.hwnd, &r);
    AnimStart(t.hwnd, t.anim, false, {r.left, r.top}, {r.left, r.top + S(12)}, kHideMs);
}

// Brings the toast up (or, if it is already up, restarts its 2 s hold) with the current text.
void ToastPresent() {
    Toast& t = g.toast;
    InvalidateRect(t.hwnd, nullptr, FALSE);
    if (IsShownOrShowing(t.hwnd, t.anim)) {
        // Already up: restart the 2 s hold (or let the running show animation start it).
        if (!t.anim.active) SetTimer(t.hwnd, kToastHoldTimer, kToastHoldMs, nullptr);
        return;
    }
    RECT r, wa = WorkAreaOf(g.hwnd);
    GetWindowRect(t.hwnd, &r);
    int x = wa.right - (r.right - r.left) - S(12);
    int y = wa.bottom - (r.bottom - r.top) - S(12);
    AnimStart(t.hwnd, t.anim, true, {x, y + S(28)}, {x, y}, kShowMs);
}

void ToastShow(const std::wstring& name, const std::wstring& size, const std::wstring& peer) {
    Toast& t = g.toast;
    t.count = IsShownOrShowing(t.hwnd, t.anim) ? t.count + 1 : 1;
    t.title = t.count > 1 ? L"스캔 파일 " + std::to_wstring(t.count) + L"개 수신" : L"스캔 파일 수신";
    t.line1 = name;
    t.line2 = size + L"  ·  " + peer + L"  ·  " + NowString();
    ToastPresent();
}

void ToastMessage(const std::wstring& title, const std::wstring& line1, const std::wstring& line2) {
    Toast& t = g.toast;
    t.count = 0;  // a file arriving next starts its own count at 1
    t.title = title;
    t.line1 = line1;
    t.line2 = line2;
    ToastPresent();
}

void PaintToast(HWND hwnd, HDC target) {
    const Toast& t = g.toast;
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    bool light = SystemUsesLightTheme();
    COLORREF bg = light ? RGB(250, 250, 250) : RGB(44, 44, 44);
    COLORREF border = light ? RGB(220, 220, 220) : RGB(64, 64, 64);
    COLORREF fg = light ? RGB(26, 26, 26) : RGB(255, 255, 255);
    COLORREF sub = light ? RGB(96, 96, 96) : RGB(190, 190, 190);

    HBRUSH b = CreateSolidBrush(bg);
    FillRect(dc, &rc, b);
    DeleteObject(b);
    b = CreateSolidBrush(border);
    FrameRect(dc, &rc, b);
    DeleteObject(b);

    int iconSize = S(32);
    DrawIconEx(dc, S(16), (rc.bottom - iconSize) / 2, g.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);

    SetBkMode(dc, TRANSPARENT);
    const UINT fmt = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    int left = S(62), right = rc.right - S(16);
    RECT r1 = {left, S(14), right, S(34)};
    SelectObject(dc, g.fontBold);
    SetTextColor(dc, fg);
    DrawTextW(dc, t.title.c_str(), -1, &r1, fmt);
    RECT r2 = {left, S(36), right, S(55)};
    SelectObject(dc, g.font);
    DrawTextW(dc, t.line1.c_str(), -1, &r2, fmt | DT_PATH_ELLIPSIS);
    RECT r3 = {left, S(57), right, S(75)};
    SelectObject(dc, g.fontSmall);
    SetTextColor(dc, sub);
    DrawTextW(dc, t.line2.c_str(), -1, &r3, fmt);

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---- UI state -------------------------------------------------------------------------------

void UpdateFileCount() {
    int n = ListView_GetItemCount(g.c.files);
    std::wstring count = n ? std::to_wstring(n) : L"";
    SetWindowTextW(g.filesWnd, (n ? L"받은 파일 (" + count + L")" : std::wstring(L"받은 파일")).c_str());
    SetWindowTextW(g.c.showFiles, (n ? L"받은 파일 " + count : std::wstring(L"받은 파일")).c_str());
    EnableWindow(g.c.clearFiles, n > 0);
}

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
    for (HWND h : {g.c.root, g.c.browse, g.c.port, g.c.pasvMin, g.c.pasvMax, g.c.user, g.c.pass, g.c.anon})
        EnableWindow(h, !running);
    SetWindowTextW(g.c.startStop, running ? L"■  서버 중지" : L"▶  서버 시작");
    g.btns[g.c.startStop].kind = running ? BtnKind::Secondary : BtnKind::Primary;
    ShowWindow(g.c.copyAddr, running ? SW_SHOW : SW_HIDE);

    if (running) {
        const FtpConfig& cfg = g.server.Config();
        std::vector<std::wstring> ips = LocalIPv4Addresses();
        g.primaryIp = PrimaryIPv4();
        if (g.primaryIp.empty() && !ips.empty()) g.primaryIp = ips[0];
        std::wstring others;
        for (const auto& ip : ips)
            if (ip != g.primaryIp) others += (others.empty() ? L"" : L", ") + ip;

        std::wstring addr = (g.primaryIp.empty() ? L"IP 확인 불가" : g.primaryIp) + L" : " + std::to_wstring(cfg.port);
        g.statusText = L"실행 중  ·  " + addr;
        SetStatusTip(L"스캐너에 입력할 값\n서버: " + (g.primaryIp.empty() ? L"-" : g.primaryIp) +
                     L"\n포트: " + std::to_wstring(cfg.port) + L"\n사용자: " +
                     (cfg.user.empty() ? L"(아무거나)" : cfg.user) + L"\n경로: /" +
                     (others.empty() ? L"" : L"\n\n다른 IP: " + others));
    } else {
        g.statusText = L"중지됨";
        SetStatusTip(L"");
    }
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    InvalidateRect(g.c.files, nullptr, TRUE);

    lstrcpynW(g.nid.szTip, running ? L"ScanReceiver - 실행 중" : L"ScanReceiver - 중지됨", ARRAYSIZE(g.nid.szTip));
    g.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

bool ParsePort(HWND h, const wchar_t* name, uint16_t& out, bool allowZero) {
    std::wstring s = GetText(h);
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(s.c_str(), &end, 10);
    if (s.empty() || *end || v > 65535 || (!allowZero && v == 0)) {
        MessageBoxW(g.hwnd, (std::wstring(name) + L" 값이 올바르지 않습니다 (1~65535).").c_str(), kAppTitle,
                    MB_ICONWARNING);
        SetFocus(h);
        return false;
    }
    out = (uint16_t)v;
    return true;
}

void StartServer() {
    FtpConfig cfg;
    if (!ParsePort(g.c.port, L"FTP 포트", cfg.port, false) ||
        !ParsePort(g.c.pasvMin, L"패시브 시작 포트", cfg.pasvPortMin, true) ||
        !ParsePort(g.c.pasvMax, L"패시브 끝 포트", cfg.pasvPortMax, true))
        return;
    if (cfg.pasvPortMin > cfg.pasvPortMax) std::swap(cfg.pasvPortMin, cfg.pasvPortMax);

    cfg.rootDir = GetText(g.c.root);
    cfg.user = GetText(g.c.user);
    cfg.password = GetText(g.c.pass);
    cfg.allowAnonymous = IsChecked(g.c.anon);
    if (cfg.rootDir.empty()) {
        MessageBoxW(g.hwnd, L"저장 폴더를 지정하세요.", kAppTitle, MB_ICONWARNING);
        return;
    }
    int rc = SHCreateDirectoryExW(nullptr, cfg.rootDir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
        MessageBoxW(g.hwnd, (L"저장 폴더를 만들 수 없습니다:\n" + cfg.rootDir).c_str(), kAppTitle, MB_ICONERROR);
        return;
    }
    if (cfg.user.empty() && MessageBoxW(g.hwnd,
                                        L"사용자 이름이 비어 있으면 누구나 접속해 파일을 올릴 수 있습니다.\n계속할까요?",
                                        kAppTitle, MB_ICONWARNING | MB_YESNO) != IDYES)
        return;

    std::wstring err;
    if (!g.server.Start(cfg, err)) {
        MessageBoxW(g.hwnd, err.c_str(), kAppTitle, MB_ICONERROR);
        return;
    }
    SaveSettings();
    UpdateUi();
}

void StopServer() {
    g.server.Stop();
    UpdateUi();
}

int ShellIconIndex(const std::wstring& path) {
    SHFILEINFOW sfi{};
    SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    return sfi.iIcon;
}

void OnFileReceived(const FtpFileEvent& ev) {
    // Re-uploads of the same path replace the existing row; temp uploads that were renamed or
    // deleted (connection tests, "*.part" files) disappear.
    for (int i = ListView_GetItemCount(g.c.files) - 1; i >= 0; --i) {
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = i;
        ListView_GetItem(g.c.files, &it);
        const std::wstring& path = g.receivedPaths[it.lParam];
        if (_wcsicmp(path.c_str(), ev.localPath.c_str()) == 0 ||
            GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            ListView_DeleteItem(g.c.files, i);
    }

    g.receivedPaths.push_back(ev.localPath);
    std::wstring name = ev.virtualPath.size() > 1 ? ev.virtualPath.substr(1) : ev.virtualPath;
    std::wstring size = FormatSize(ev.size);
    std::wstring time = NowString(true);

    LVITEMW it{};
    it.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
    it.iItem = 0;
    it.pszText = &name[0];
    it.iImage = ShellIconIndex(ev.localPath);
    it.lParam = (LPARAM)(g.receivedPaths.size() - 1);
    int idx = ListView_InsertItem(g.c.files, &it);
    ListView_SetItemText(g.c.files, idx, 1, &size[0]);
    ListView_SetItemText(g.c.files, idx, 2, &time[0]);
    ListView_SetItemText(g.c.files, idx, 3, const_cast<wchar_t*>(ev.peer.c_str()));
    ListView_SetItemState(g.c.files, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(g.c.files, idx, LVIS_SELECTED, LVIS_SELECTED);
    ListView_EnsureVisible(g.c.files, idx, FALSE);
    UpdateFileCount();

    // Window open: pop the received-files window beside it. In the tray (or minimized): a 2 s toast.
    if (IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd))
        ShowPopup(g.filesWnd, g.filesAnim);
    else
        ToastShow(name, size, ev.peer);
}

std::wstring SelectedFile() {
    int sel = ListView_GetNextItem(g.c.files, -1, LVNI_SELECTED);
    if (sel < 0) return {};
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = sel;
    ListView_GetItem(g.c.files, &it);
    return g.receivedPaths[it.lParam];
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

void RestoreWindow() {
    ShowWindow(g.hwnd, SW_SHOW);
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
}

void HideToTray() {
    HidePopup(g.filesWnd, g.filesAnim);
    HidePopup(g.logWnd, g.logAnim);
    ShowWindow(g.hwnd, SW_HIDE);
    // Once per run, say where the window went so closing doesn't look like quitting.
    if (!g.trayHintShown) {
        g.trayHintShown = true;
        ToastMessage(L"트레이에서 계속 실행 중", L"스캔 파일은 계속 받습니다.", L"끄려면 트레이 아이콘 우클릭 → 종료");
    }
}

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
    HBRUSH surface = CreateSolidBrush(b.surface);
    FillRect(mem, &rc, surface);
    DeleteObject(surface);

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
            font = g.fontBold;
            break;
        case BtnKind::Secondary:
            FillRound(gr, rc, radius, pressed ? kControlPressed : hover ? kControlHover : kControl, kControlBorder);
            break;
        case BtnKind::Subtle:
            if (pressed || hover || b.checked) FillRound(gr, rc, radius, pressed ? kControlPressed : kSubtleHover);
            break;
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
            if (focus) {
                RECT ring = {track.left, track.top, track.right, track.bottom};
                StrokeRound(gr, ring, th / 2, kText, 1.0f);
            }
            textRc.left = (LONG)tw + S(8);
            fmt = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX;
            break;
        }
        }
        if (focus && b.kind != BtnKind::Switch) StrokeRound(gr, rc, radius, kText, 1.5f);
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

// Hover tracking, and toggling switches before the parent sees BN_CLICKED.
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

// ---- controls & layout ----------------------------------------------------------------------

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, 0, 0, 0, 0, parent,
                             (HMENU)(INT_PTR)id, g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, FALSE);
    return h;
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, BtnKind kind, COLORREF surface) {
    HWND h = Make(parent, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, id);
    g.btns[h] = BtnInfo{kind, surface, kind == BtnKind::Switch};
    SetWindowSubclass(h, ButtonSubclass, 0, 0);
    return h;
}

HWND MakeEdit(HWND parent, int id, DWORD style = 0) {
    HWND h = Make(parent, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | style, id);
    SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    return h;
}

void CreateControls() {
    Controls& c = g.c;
    HWND p = g.hwnd;

    c.copyAddr = MakeButton(p, L"복사", IDC_COPYADDR, BtnKind::Subtle, kBg);
    c.showFiles = MakeButton(p, L"받은 파일", IDC_SHOWFILES, BtnKind::Secondary, kBg);
    c.showLog = MakeButton(p, L"로그", IDC_SHOWLOG, BtnKind::Secondary, kBg);
    c.startStop = MakeButton(p, L"", IDC_STARTSTOP, BtnKind::Primary, kBg);

    c.lblRoot = Make(p, L"STATIC", L"저장 폴더", SS_LEFT);
    c.root = MakeEdit(p, IDC_ROOT);
    c.browse = MakeButton(p, L"···", IDC_BROWSE, BtnKind::Secondary, kCard);
    c.openDir = MakeButton(p, L"열기", IDC_OPENDIR, BtnKind::Secondary, kCard);
    c.lblPort = Make(p, L"STATIC", L"포트", SS_LEFT);
    c.port = MakeEdit(p, IDC_PORT, ES_NUMBER);
    c.lblPasv = Make(p, L"STATIC", L"패시브", SS_LEFT);
    c.pasvMin = MakeEdit(p, IDC_PASVMIN, ES_NUMBER);
    c.lblTilde = Make(p, L"STATIC", L"~", SS_CENTER);
    c.pasvMax = MakeEdit(p, IDC_PASVMAX, ES_NUMBER);
    c.lblUser = Make(p, L"STATIC", L"사용자", SS_LEFT);
    c.user = MakeEdit(p, IDC_USER);
    c.lblPass = Make(p, L"STATIC", L"비밀번호", SS_LEFT);
    c.pass = MakeEdit(p, IDC_PASS, ES_PASSWORD);
    SendMessageW(c.pass, EM_SETPASSWORDCHAR, kPasswordChar, 0);
    c.showPass = MakeButton(p, L"표시", IDC_SHOWPASS, BtnKind::Subtle, kCard);
    g.btns[c.showPass].toggles = true;
    c.anon = MakeButton(p, L"익명 허용", IDC_ANON, BtnKind::Switch, kCard);
    c.toTray = MakeButton(p, L"닫기·최소화 시 트레이로", IDC_TOTRAY, BtnKind::Switch, kCard);
    c.autoStart = MakeButton(p, L"실행 시 서버 자동 시작", IDC_AUTOSTART, BtnKind::Switch, kCard);

    // Full scanner settings live in a tooltip on the one-line status.
    g.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0,
                            0, g.hwnd, nullptr, g.inst, nullptr);
    TOOLINFOW ti{sizeof ti};
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = g.hwnd;
    ti.uId = kStatusTipId;
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(g.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(g.tip, TTM_SETMAXTIPWIDTH, 0, S(400));
    SendMessageW(g.tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
}

void CreateFilesControls(HWND w) {
    Controls& c = g.c;
    c.clearFiles = MakeButton(w, L"목록 지우기", IDC_CLEARFILES, BtnKind::Secondary, kCard);
    c.files = Make(w, WC_LISTVIEWW, L"",
                   WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, IDC_FILES);

    ListView_SetExtendedListViewStyle(c.files, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SHFILEINFOW sfi{};
    auto sysImages = (HIMAGELIST)SHGetFileInfoW(L".pdf", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                                                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    ListView_SetImageList(c.files, sysImages, LVSIL_SMALL);
    struct Col {
        const wchar_t* title;
        int width;
        int fmt;
    } cols[] = {{L"파일", 300, LVCFMT_LEFT},
                {L"크기", 76, LVCFMT_RIGHT},
                {L"받은 시각", 112, LVCFMT_LEFT},
                {L"보낸 곳", 100, LVCFMT_LEFT}};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = const_cast<wchar_t*>(cols[i].title);
        col.cx = S(cols[i].width);
        col.fmt = cols[i].fmt;
        ListView_InsertColumn(c.files, i, &col);
    }
}

void Place(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, S(x), S(y), S(w), S(hgt), FALSE); }

// Main window: status line + settings only (files and log live in their own windows).
// Narrow, stacked layout: one setting per row, settings on a white card.
constexpr int kMainClientW = 360, kMainClientH = 380;
constexpr int kFieldH = 30;

// An edit sits borderless inside a painted Fluent text box.
void PlaceField(HWND edit, int x, int y, int w) {
    RECT box = {S(x), S(y), S(x + w), S(y + kFieldH)};
    MoveWindow(edit, box.left + S(9), box.top + ((box.bottom - box.top) - g.fontH) / 2, (box.right - box.left) - S(18),
               g.fontH, FALSE);
    for (auto& f : g.fields)
        if (f.first == edit) {
            f.second = box;
            return;
        }
    g.fields.push_back({edit, box});
}

void Layout(int cx, int) {
    Controls& c = g.c;
    int W = MulDiv(cx, 96, g.dpi);
    const int L = 12, R = W - 12;

    // Row 1: status pill (painted) + copy. Row 2: files / log on the left, start-stop on the right.
    Place(c.copyAddr, R - 52, 14, 52, 24);
    Place(c.showFiles, L, 50, 100, 32);
    Place(c.showLog, L + 106, 50, 64, 32);
    Place(c.startStop, R - 120, 50, 120, 32);

    // Settings card: label column + field column, one item per row.
    const int cardTop = 94, CL = L + 12, CR = R - 12, F = CL + 64;
    int y = cardTop + 14;
    Place(c.lblRoot, CL, y + 6, 62, 20);
    Place(c.openDir, CR - 48, y, 48, kFieldH);
    Place(c.browse, CR - 48 - 4 - 34, y, 34, kFieldH);
    PlaceField(c.root, F, y, CR - 48 - 4 - 34 - 6 - F);

    y += 38;
    Place(c.lblPort, CL, y + 6, 62, 20);
    PlaceField(c.port, F, y, 58);
    Place(c.lblPasv, F + 68, y + 6, 40, 20);
    PlaceField(c.pasvMin, F + 108, y, 58);
    Place(c.lblTilde, F + 166, y + 6, 14, 20);
    PlaceField(c.pasvMax, F + 180, y, 58);

    y += 38;
    Place(c.lblUser, CL, y + 6, 62, 20);
    PlaceField(c.user, F, y, CR - F);

    y += 38;
    Place(c.lblPass, CL, y + 6, 62, 20);
    PlaceField(c.pass, F, y, CR - 56 - 6 - F);
    Place(c.showPass, CR - 56, y, 56, kFieldH);

    y += 44;
    Place(c.anon, CL, y, CR - CL, 24);
    y += 32;
    Place(c.toTray, CL, y, CR - CL, 24);
    y += 32;
    Place(c.autoStart, CL, y, CR - CL, 24);
    g.card = {S(L), S(cardTop), S(R), S(y + 24 + 14)};

    // Controls were moved without repainting; the main window has WS_CLIPCHILDREN, so a plain
    // InvalidateRect on it would leave every child unpainted until hovered.
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void PaintMain(HDC target) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, g.bgBrush);

    bool running = g.server.IsRunning();
    HGDIOBJ oldFont = SelectObject(dc, g.fontBold);
    SIZE ts{};
    GetTextExtentPoint32W(dc, g.statusText.c_str(), (int)g.statusText.size(), &ts);
    int chipH = S(28), dotX = S(12) + S(12);
    g.chip = {S(12), S(12), dotX + S(4) + S(8) + ts.cx + S(14), S(12) + chipH};

    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        FillRound(gr, g.chip, chipH / 2.0f, running ? kRunBg : kStopBg);
        FillCircle(gr, (float)dotX, (g.chip.top + g.chip.bottom) / 2.0f, (float)S(4), running ? kRunFg : kStopDot);
        FillRound(gr, g.card, (float)S(8), kCard, kCardBorder);
        for (const auto& f : g.fields) {
            bool enabled = IsWindowEnabled(f.first);
            FillRound(gr, f.second, (float)S(4), enabled ? kCard : kFieldDisabled, enabled ? kControlBorder : kCardBorder);
        }
    }
    // Text-box underline: grey at rest, 2 px accent while focused.
    for (const auto& f : g.fields) {
        if (!IsWindowEnabled(f.first)) continue;
        bool focused = GetFocus() == f.first;
        RECT u = {f.second.left + S(3), f.second.bottom - (focused ? S(2) : 1), f.second.right - S(3), f.second.bottom};
        HBRUSH ub = CreateSolidBrush(focused ? kAccent : kFieldUnderline);
        FillRect(dc, &u, ub);
        DeleteObject(ub);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, running ? kRunFg : kStopFg);
    RECT tr = {dotX + S(4) + S(8), g.chip.top, g.chip.right, g.chip.bottom};
    DrawTextW(dc, g.statusText.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);

    TOOLINFOW ti{sizeof ti};
    ti.hwnd = g.hwnd;
    ti.uId = kStatusTipId;
    ti.rect = g.chip;
    SendMessageW(g.tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// Popup header strip: optional hint text and a hairline under it.
void PaintPopupHeader(HWND hwnd, HDC dc, const wchar_t* hint) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    RECT line = {0, S(42), rc.right, S(42) + 1};
    HBRUSH b = CreateSolidBrush(kDivider);
    FillRect(dc, &line, b);
    DeleteObject(b);
    if (!hint) return;
    HGDIOBJ old = SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextSecondary);
    RECT r = {S(12), 0, rc.right - S(120), S(42)};
    DrawTextW(dc, hint, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

// Empty-state hint drawn over the file list.
LRESULT FilesCustomDraw(NMLVCUSTOMDRAW* cd) {
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYPOSTPAINT;
    if (cd->nmcd.dwDrawStage == CDDS_POSTPAINT && ListView_GetItemCount(g.c.files) == 0) {
        RECT rc, hdr{};
        GetClientRect(g.c.files, &rc);
        GetWindowRect(ListView_GetHeader(g.c.files), &hdr);
        rc.top += hdr.bottom - hdr.top;
        HDC dc = cd->nmcd.hdc;
        HGDIOBJ old = SelectObject(dc, g.font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(140, 140, 140));
        const wchar_t* text = g.server.IsRunning()
                                  ? L"아직 받은 파일이 없습니다.\n스캐너에서 FTP로 보내면 여기에 표시됩니다."
                                  : L"서버를 시작하면 스캐너가 보낸 파일이 여기에 표시됩니다.";
        RECT measure = rc;
        DrawTextW(dc, text, -1, &measure, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        int h = measure.bottom - measure.top;
        rc.top += ((rc.bottom - rc.top) - h) / 2;
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_WORDBREAK);
        SelectObject(dc, old);
    }
    return CDRF_DODEFAULT;
}

// ---- tray -----------------------------------------------------------------------------------

void AddTrayIcon() {
    g.nid.cbSize = sizeof g.nid;
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_APP_TRAY;
    g.nid.hIcon = g.iconSmall;
    lstrcpynW(g.nid.szTip, L"ScanReceiver", ARRAYSIZE(g.nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

void ShowPopupMenu(std::initializer_list<std::pair<int, const wchar_t*>> items, int defaultId) {
    HMENU menu = CreatePopupMenu();
    for (const auto& it : items) AppendMenuW(menu, MF_STRING, it.first, it.second);
    if (defaultId) SetMenuDefaultItem(menu, defaultId, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ---- window procedures ----------------------------------------------------------------------

void OnCommand(int id, int code) {
    switch (id) {
    case IDC_STARTSTOP:
        g.server.IsRunning() ? StopServer() : StartServer();
        break;
    case IDC_BROWSE: {
        std::wstring path = GetText(g.c.root);
        if (PickFolder(g.hwnd, path)) SetWindowTextW(g.c.root, path.c_str());
        break;
    }
    case IDC_OPENDIR: {
        std::wstring path = GetText(g.c.root);
        SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
        ShellExecuteW(g.hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case IDC_SHOWPASS:
        SendMessageW(g.c.pass, EM_SETPASSWORDCHAR, IsChecked(g.c.showPass) ? 0 : kPasswordChar, 0);
        SetWindowTextW(g.c.showPass, IsChecked(g.c.showPass) ? L"숨김" : L"표시");
        InvalidateRect(g.c.pass, nullptr, TRUE);
        break;
    case IDC_COPYADDR:
        if (!g.primaryIp.empty() && CopyToClipboard(g.primaryIp)) {
            SetWindowTextW(g.c.copyAddr, L"✓");
            SetTimer(g.hwnd, kCopyResetTimer, 1500, nullptr);
        }
        break;
    case IDC_SHOWFILES:
        TogglePopup(g.filesWnd, g.filesAnim);
        break;
    case IDC_SHOWLOG:
        TogglePopup(g.logWnd, g.logAnim);
        break;
    case IDM_TRAY_LOG:
        RestoreWindow();
        ShowPopup(g.logWnd, g.logAnim);
        break;
    case IDC_CLEARFILES:
        ListView_DeleteAllItems(g.c.files);
        g.receivedPaths.clear();
        UpdateFileCount();
        break;
    case IDC_CLEARLOG:
        g.logLines.clear();
        SendMessageW(g.c.log, LB_RESETCONTENT, 0, 0);
        break;
    case IDC_VERBOSE:
        if (code == BN_CLICKED) {
            RebuildLog();
            SaveSettings();
        }
        break;
    case IDC_AUTOSTART:
    case IDC_TOTRAY:
        if (code == BN_CLICKED) SaveSettings();
        break;
    case IDM_OPEN: {
        std::wstring f = SelectedFile();
        if (!f.empty()) OpenFile(f);
        break;
    }
    case IDM_SHOW_IN_FOLDER: {
        std::wstring f = SelectedFile();
        if (!f.empty()) ShowInFolder(f);
        break;
    }
    case IDM_TRAY_SHOW:
        RestoreWindow();
        break;
    case IDM_TRAY_EXIT:
        DestroyWindow(g.hwnd);
        break;
    }
}

LRESULT CtlColorStatic(WPARAM wp, LPARAM lp) {
    HDC dc = (HDC)wp;
    HWND ctl = (HWND)lp;
    SetBkMode(dc, TRANSPARENT);
    for (const auto& f : g.fields)
        if (f.first == ctl) {  // disabled edit
            SetTextColor(dc, kTextDisabled);
            SetBkColor(dc, kFieldDisabled);
            return (LRESULT)g.fieldDisabledBrush;
        }
    SetTextColor(dc, kText);  // labels on the settings card
    return (LRESULT)g.cardBrush;
}

LRESULT CtlColorEdit(WPARAM wp) {
    HDC dc = (HDC)wp;
    SetTextColor(dc, kText);
    SetBkColor(dc, kCard);
    return (LRESULT)g.cardBrush;
}

LRESULT CALLBACK LogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetCaptionColor(hwnd, kCard);
        g.c.verbose = MakeButton(hwnd, L"FTP 명령까지 자세히 보기", IDC_VERBOSE, BtnKind::Switch, kCard);
        g.c.clearLog = MakeButton(hwnd, L"지우기", IDC_CLEARLOG, BtnKind::Secondary, kCard);
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
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintPopupHeader(hwnd, dc, nullptr);
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

LRESULT CALLBACK FilesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetCaptionColor(hwnd, kCard);
        CreateFilesControls(hwnd);
        return 0;
    case WM_SIZE: {
        int W = MulDiv(LOWORD(lp), 96, g.dpi), H = MulDiv(HIWORD(lp), 96, g.dpi);
        Place(g.c.clearFiles, W - 12 - 96, 6, 96, 30);
        Place(g.c.files, 0, 43, W, H - 43);
        // The file-name column takes whatever width the other columns leave.
        RECT lr;
        GetClientRect(g.c.files, &lr);
        int fixed = 0;
        for (int i = 1; i < 4; ++i) fixed += ListView_GetColumnWidth(g.c.files, i);
        ListView_SetColumnWidth(g.c.files, 0, (std::max)((int)S(140), (int)(lr.right - fixed)));
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintPopupHeader(hwnd, dc, L"더블클릭: 열기  ·  우클릭: 폴더에서 보기");
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize = {S(420), S(200)};
        return 0;
    case WM_TIMER:
        if (wp == kAnimTimer && AnimStep(hwnd, g.filesAnim) && g.filesAnim.showing) {
            SetLayered(hwnd, false);
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr->idFrom == IDC_FILES) {
            if (hdr->code == NM_CUSTOMDRAW) return FilesCustomDraw((NMLVCUSTOMDRAW*)lp);
            if (hdr->code == NM_DBLCLK) {
                std::wstring f = SelectedFile();
                if (!f.empty()) OpenFile(f);
            } else if (hdr->code == NM_RCLICK && !SelectedFile().empty()) {
                ShowPopupMenu({{IDM_OPEN, L"열기"}, {IDM_SHOW_IN_FOLDER, L"폴더에서 보기"}}, IDM_OPEN);
            }
        }
        return 0;
    }
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CLOSE:
        HidePopup(hwnd, g.filesAnim);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

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
    case WM_TIMER:
        if (wp == kAnimTimer && AnimStep(hwnd, t.anim) && t.anim.showing)
            SetTimer(hwnd, kToastHoldTimer, kToastHoldMs, nullptr);
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
    case WM_LBUTTONUP:
        ToastHide();
        RestoreWindow();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        SetCaptionColor(hwnd, kBg);
        CreateControls();
        g.filesWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kFilesClass, L"받은 파일", WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, S(600), S(320), hwnd, nullptr, g.inst, nullptr);
        g.logWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kLogClass, L"로그", WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, S(600), S(300), hwnd, nullptr, g.inst, nullptr);
        g.toast.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                       kToastClass, L"", WS_POPUP, 0, 0, S(360), S(88), nullptr, nullptr, g.inst,
                                       nullptr);
        LoadSettings();
        AddTrayIcon();
        g.server.SetLogHandler([](const std::wstring& m) {
            auto* p = new std::wstring(m);
            if (!PostMessageW(g.hwnd, WM_APP_LOG, 0, (LPARAM)p)) delete p;
        });
        g.server.SetFileHandler([](const FtpFileEvent& ev) {
            auto* p = new FtpFileEvent(ev);
            if (!PostMessageW(g.hwnd, WM_APP_FILE, 0, (LPARAM)p)) delete p;
        });
        UpdateFileCount();
        UpdateUi();
        if (IsChecked(g.c.autoStart)) StartServer();
        return 0;
    }
    case WM_CLOSE:
        // The close button (and Alt+F4) hides to the tray; the tray menu's "종료" really exits.
        if (IsChecked(g.c.toTray)) {
            HideToTray();
            return 0;
        }
        break;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            HidePopup(g.filesWnd, g.filesAnim);
            HidePopup(g.logWnd, g.logAnim);
            if (IsChecked(g.c.toTray)) HideToTray();
        } else {
            Layout(LOWORD(lp), HIWORD(lp));
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintMain(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == kCopyResetTimer) {
            KillTimer(hwnd, kCopyResetTimer);
            SetWindowTextW(g.c.copyAddr, L"복사");
        }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) {  // repaint the text-box underline
            for (const auto& f : g.fields)
                if (f.first == (HWND)lp) InvalidateRect(hwnd, &f.second, FALSE);
            return 0;
        }
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_ERASEBKGND:
        return 1;  // WM_PAINT fills everything from a back buffer
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CTLCOLOREDIT:
        return CtlColorEdit(wp);
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
            ShowPopupMenu({{IDM_TRAY_SHOW, L"열기"}, {IDM_TRAY_LOG, L"로그 보기"}, {IDM_TRAY_EXIT, L"종료"}},
                          IDM_TRAY_SHOW);
        return 0;
    case WM_CTLCOLORSTATIC:
        return CtlColorStatic(wp, lp);
    case WM_DESTROY:
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
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ScanReceiver.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(other, SW_SHOW);
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    g.inst = inst;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g.iniPath = exe;
    g.iniPath = g.iniPath.substr(0, g.iniPath.find_last_of(L'\\') + 1) + L"ScanReceiver.ini";

    HDC screen = GetDC(nullptr);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_BOLD;
    g.fontBold = CreateFontIndirectW(&bold);
    LOGFONTW smaller = ncm.lfMessageFont;  // not "small": rpcndr.h #defines it
    smaller.lfHeight = smaller.lfHeight * 9 / 10;
    g.fontSmall = CreateFontIndirectW(&smaller);
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

    g.bgBrush = CreateSolidBrush(kBg);
    g.cardBrush = CreateSolidBrush(kCard);
    g.fieldDisabledBrush = CreateSolidBrush(kFieldDisabled);
    RegisterWindowClass(kWindowClass, WndProc, g.bgBrush);
    RegisterWindowClass(kLogClass, LogProc, g.cardBrush);
    RegisterWindowClass(kFilesClass, FilesProc, g.cardBrush);
    RegisterWindowClass(kToastClass, ToastProc, g.cardBrush, CS_DROPSHADOW);

    // Fixed-size main window sized to its client content.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr = {0, 0, S(kMainClientW), S(kMainClientH)};
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"ScanReceiver - 스캔 파일 FTP 수신", style, CW_USEDEFAULT,
                                CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg) && !IsDialogMessageW(g.filesWnd, &msg) &&
            !IsDialogMessageW(g.logWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    for (HGDIOBJ o : {(HGDIOBJ)g.font, (HGDIOBJ)g.fontBold, (HGDIOBJ)g.fontSmall, (HGDIOBJ)g.bgBrush,
                      (HGDIOBJ)g.cardBrush, (HGDIOBJ)g.fieldDisabledBrush})
        DeleteObject(o);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    WSACleanup();
    if (mutex) CloseHandle(mutex);
    return 0;
}
