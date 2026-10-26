// Test helper for driving and screenshotting ScanReceiver from a script (build: test\build_wintool.bat).
//
//   wintool top | untop               keep the main window topmost at (40,40) / release it
//   wintool start                     click "서버 시작/중지"
//   wintool cmd <id>                  post WM_COMMAND <id> to the main window (ids: enum in main.cpp)
//   wintool close | minimize | restore
//   wintool capture out.bmp           what is ON SCREEN in the main window rect
//   wintool area x y w h out.bmp      a screen rectangle
//   wintool burst x y w h n ms prefix n screen captures, ms apart (animation frames)
//   wintool rect <window class>       prints "left top right bottom visible"
//
// Captures use BitBlt from the screen on purpose: PrintWindow forces a repaint and hides
// "controls not painted until hovered" bugs. Capture only app windows: the desktop around them
// may show the user's private content.
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>

static void SaveArea(int x, int y, int w, int h, const wchar_t* path) {
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
    BITMAPINFOHEADER bi{sizeof bi, w, -h, 1, 24, BI_RGB};
    int stride = ((w * 3 + 3) & ~3);
    DWORD size = stride * h;
    char* bits = new char[size];
    GetDIBits(mem, bmp, 0, h, bits, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    BITMAPFILEHEADER bf{0x4D42, (DWORD)(sizeof bf + sizeof bi + size), 0, 0, (DWORD)(sizeof bf + sizeof bi)};
    FILE* f = _wfopen(path, L"wb");
    if (f) {
        fwrite(&bf, sizeof bf, 1, f);
        fwrite(&bi, sizeof bi, 1, f);
        fwrite(bits, size, 1, f);
        fclose(f);
    }
    delete[] bits;
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    if (argc < 2) return 1;
    const wchar_t* cmd = argv[1];
    if (!wcscmp(cmd, L"area") && argc > 6) {
        SaveArea(_wtoi(argv[2]), _wtoi(argv[3]), _wtoi(argv[4]), _wtoi(argv[5]), argv[6]);
        return 0;
    }
    if (!wcscmp(cmd, L"burst") && argc > 8) {
        int n = _wtoi(argv[6]), ms = _wtoi(argv[7]);
        timeBeginPeriod(1);
        for (int i = 0; i < n; ++i) {
            DWORD t0 = GetTickCount();
            std::wstring p = std::wstring(argv[8]) + std::to_wstring(i) + L".bmp";
            SaveArea(_wtoi(argv[2]), _wtoi(argv[3]), _wtoi(argv[4]), _wtoi(argv[5]), p.c_str());
            DWORD spent = GetTickCount() - t0;
            if ((int)spent < ms) Sleep(ms - spent);
        }
        timeEndPeriod(1);
        return 0;
    }
    if (!wcscmp(cmd, L"rect") && argc > 2) {
        HWND w = FindWindowW(argv[2], nullptr);
        RECT r{};
        if (w) GetWindowRect(w, &r);
        wprintf(L"%d %d %d %d %d\n", r.left, r.top, r.right, r.bottom, w ? IsWindowVisible(w) : -1);
        return 0;
    }
    // A test instance (--ini) first, so the user's own copy is left alone when both run.
    HWND h = FindWindowW(L"ScanReceiverMainWindow", L"ScanReceiver (test)");
    if (!h) h = FindWindowW(L"ScanReceiverMainWindow", nullptr);
    if (!h) {
        fwprintf(stderr, L"no window\n");
        return 1;
    }
    if (!wcscmp(cmd, L"top")) {
        SetWindowPos(h, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE);
        SetForegroundWindow(h);
    } else if (!wcscmp(cmd, L"untop")) {
        SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else if (!wcscmp(cmd, L"start")) {
        PostMessageW(h, WM_COMMAND, 100, 0);  // IDC_STARTSTOP
    } else if (!wcscmp(cmd, L"cmd") && argc > 2) {
        PostMessageW(h, WM_COMMAND, _wtoi(argv[2]), 0);
    } else if (!wcscmp(cmd, L"close")) {
        PostMessageW(h, WM_CLOSE, 0, 0);
    } else if (!wcscmp(cmd, L"minimize")) {
        PostMessageW(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    } else if (!wcscmp(cmd, L"restore")) {
        ShowWindow(h, SW_SHOW);
        ShowWindow(h, SW_RESTORE);
    } else if (!wcscmp(cmd, L"capture") && argc > 2) {
        RECT r;
        GetWindowRect(h, &r);
        SaveArea(r.left, r.top, r.right - r.left, r.bottom - r.top, argv[2]);
        wprintf(L"%d %d %d %d\n", r.left, r.top, r.right, r.bottom);
    }
    return 0;
}
