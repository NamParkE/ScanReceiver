#include "ftp_server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

namespace {

constexpr int kMaxSessions = 32;
constexpr int kDataWaitSeconds = 30;
constexpr DWORD kControlTimeoutMs = 10 * 60 * 1000;
constexpr DWORD kDataTimeoutMs = 2 * 60 * 1000;
constexpr size_t kMaxLine = 8192;
constexpr size_t kBufferSize = 64 * 1024;

std::wstring Widen(const std::string& s, UINT cp) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w, UINT cp) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool IsUtf8(const std::string& s) {
    return s.empty() ||
           MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0;
}

std::string UpperAscii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return s;
}

std::wstring IpString(const in_addr& a) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%u.%u.%u.%u", a.S_un.S_un_b.s_b1, a.S_un.S_un_b.s_b2, a.S_un.S_un_b.s_b3,
             a.S_un.S_un_b.s_b4);
    return buf;
}

bool SendAll(SOCKET s, const char* data, size_t len) {
    while (len > 0) {
        int n = send(s, data, (int)(len > 0x7fffffff ? 0x7fffffff : len), 0);
        if (n <= 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

// Joins cwd and arg into a normalized absolute virtual path ("/a/b"). ".." never climbs above "/".
std::wstring NormalizeVirtual(const std::wstring& cwd, std::wstring arg) {
    for (wchar_t& c : arg)
        if (c == L'\\') c = L'/';
    std::wstring full = (!arg.empty() && arg[0] == L'/') ? arg : cwd + L"/" + arg;
    std::vector<std::wstring> parts;
    size_t i = 0;
    while (i <= full.size()) {
        size_t j = full.find(L'/', i);
        if (j == std::wstring::npos) j = full.size();
        std::wstring seg = full.substr(i, j - i);
        if (seg.empty() || seg == L".") {
        } else if (seg == L"..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(seg);
        }
        i = j + 1;
    }
    std::wstring out;
    for (const auto& p : parts) out += L"/" + p;
    return out.empty() ? L"/" : out;
}

// Makes one path segment safe for NTFS (no reserved characters, no trailing dot/space).
std::wstring SanitizeSegment(std::wstring seg) {
    for (wchar_t& c : seg)
        if (c < 32 || wcschr(L"<>:\"|?*", c)) c = L'_';
    while (!seg.empty() && (seg.back() == L'.' || seg.back() == L' ')) seg.back() = L'_';
    return seg;
}

std::wstring LastSegment(const std::wstring& vpath) {
    size_t p = vpath.find_last_of(L'/');
    return p == std::wstring::npos ? vpath : vpath.substr(p + 1);
}

std::wstring ParentOf(const std::wstring& vpath) {
    size_t p = vpath.find_last_of(L'/');
    return (p == std::wstring::npos || p == 0) ? L"/" : vpath.substr(0, p);
}

std::wstring QuotePath(const std::wstring& p) {
    std::wstring out;
    for (wchar_t c : p) {
        out += c;
        if (c == L'"') out += L'"';
    }
    return L"\"" + out + L"\"";
}

std::string ListTime(const FILETIME& ft) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    SYSTEMTIME utc, st, now;
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &st);
    GetLocalTime(&now);
    char buf[32];
    if (st.wYear == now.wYear)
        snprintf(buf, sizeof buf, "%s %2u %02u:%02u", kMonths[st.wMonth - 1], st.wDay, st.wHour, st.wMinute);
    else
        snprintf(buf, sizeof buf, "%s %2u  %u", kMonths[st.wMonth - 1], st.wDay, st.wYear);
    return buf;
}

uint64_t FileSize(const WIN32_FIND_DATAW& fd) {
    return ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
}

bool FindOne(const std::wstring& path, WIN32_FIND_DATAW& fd) {
    HANDLE h = FindFirstFileW(path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// One control connection.

class FtpSession {
public:
    FtpSession(FtpServer* server, SOCKET ctrl, const sockaddr_in& peer)
        : server_(server), cfg_(server->cfg_), ctrl_(ctrl), peer_(peer), peerIp_(IpString(peer.sin_addr)) {}

    ~FtpSession() {
        ClosePassive();
        server_->CloseTracked(ctrl_);
    }

    void Run() {
        DWORD timeout = kControlTimeoutMs;
        setsockopt(ctrl_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
        Log(L"접속");
        if (server_->activeSessions_ > kMaxSessions) {
            Reply(421, L"Too many connections.");
            return;
        }
        Reply(220, L"ScanReceiver FTP server ready.");

        std::string line;
        while (!quit_ && server_->running_ && ReadLine(line)) {
            size_t sp = line.find(' ');
            std::string cmd = UpperAscii(line.substr(0, sp));
            std::wstring arg = sp == std::string::npos ? L"" : Decode(line.substr(sp + 1));
            if (cmd == "PASS")
                Log(L"> PASS ****");
            else
                Log(L"> " + Widen(cmd, CP_ACP) + (arg.empty() ? L"" : L" " + arg));
            Dispatch(cmd, arg);
        }
        Log(L"접속 종료");
    }

private:
    // ---- control channel I/O ----------------------------------------------------------------

    bool ReadLine(std::string& line) {
        for (;;) {
            size_t pos = recvBuf_.find('\n');
            if (pos != std::string::npos) {
                line = recvBuf_.substr(0, pos);
                recvBuf_.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                // Drop telnet IAC sequences (sent by some clients before ABOR).
                for (size_t i; (i = line.find('\xFF')) != std::string::npos;) line.erase(i, 2);
                return true;
            }
            if (recvBuf_.size() > kMaxLine) return false;
            char buf[2048];
            int n = recv(ctrl_, buf, sizeof buf, 0);
            if (n <= 0) return false;
            recvBuf_.append(buf, n);
        }
    }

    void Reply(int code, const std::wstring& text) {
        std::string msg = std::to_string(code) + " " + Narrow(text, codepage_) + "\r\n";
        SendAll(ctrl_, msg.data(), msg.size());
        Log(L"< " + std::to_wstring(code) + L" " + text);
    }

    // Scanners send either UTF-8 or the local ANSI code page (CP949 on Korean Windows).
    std::wstring Decode(const std::string& bytes) {
        if (IsUtf8(bytes)) return Widen(bytes, CP_UTF8);
        if (!utf8Forced_) codepage_ = CP_ACP;
        return Widen(bytes, CP_ACP);
    }

    void Log(const std::wstring& m) const { server_->Log(L"[" + peerIp_ + L"] " + m); }

    // ---- paths ------------------------------------------------------------------------------

    std::wstring Resolve(const std::wstring& arg) const { return NormalizeVirtual(cwd_, arg); }

    std::wstring LocalPath(const std::wstring& vpath) const {
        std::wstring root = cfg_.rootDir;
        if (vpath == L"/") return root;
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        std::wstring out = root;
        size_t i = 1;
        while (i <= vpath.size()) {
            size_t j = vpath.find(L'/', i);
            if (j == std::wstring::npos) j = vpath.size();
            out += L"\\" + SanitizeSegment(vpath.substr(i, j - i));
            i = j + 1;
        }
        return out;
    }

    bool IsDir(const std::wstring& local) const {
        DWORD a = GetFileAttributesW(local.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool IsFile(const std::wstring& local) const {
        DWORD a = GetFileAttributesW(local.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    // ---- data channel -----------------------------------------------------------------------

    void ClosePassive() { server_->CloseTracked(pasv_); }

    SOCKET OpenData() {
        SOCKET d = INVALID_SOCKET;
        if (pasv_ != INVALID_SOCKET) {
            for (int waited = 0; waited < kDataWaitSeconds && server_->running_; ++waited) {
                fd_set fs;
                FD_ZERO(&fs);
                FD_SET(pasv_, &fs);
                timeval tv{1, 0};
                int r = select(0, &fs, nullptr, nullptr, &tv);
                if (r < 0) break;
                if (r == 0) continue;
                sockaddr_in from{};
                int len = sizeof from;
                d = accept(pasv_, (sockaddr*)&from, &len);
                // Only the host that owns the control connection may use the data port.
                if (d != INVALID_SOCKET && from.sin_addr.s_addr != peer_.sin_addr.s_addr) {
                    Log(L"다른 주소에서 데이터 연결 시도 차단: " + IpString(from.sin_addr));
                    closesocket(d);
                    d = INVALID_SOCKET;
                }
                break;
            }
            ClosePassive();
        } else if (havePort_) {
            havePort_ = false;
            d = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (d != INVALID_SOCKET && connect(d, (const sockaddr*)&portAddr_, sizeof portAddr_) != 0) {
                closesocket(d);
                d = INVALID_SOCKET;
            }
        }
        if (d != INVALID_SOCKET) {
            DWORD timeout = kDataTimeoutMs;
            setsockopt(d, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
            setsockopt(d, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);
            server_->Track(d);
        }
        return d;
    }

    bool HasDataTarget() const { return pasv_ != INVALID_SOCKET || havePort_; }

    // ---- command dispatch -------------------------------------------------------------------

    void Dispatch(const std::string& cmd, const std::wstring& arg) {
        if (cmd == "USER") return CmdUser(arg);
        if (cmd == "PASS") return CmdPass(arg);
        if (cmd == "QUIT") {
            Reply(221, L"Goodbye.");
            quit_ = true;
            return;
        }
        if (cmd == "NOOP") return Reply(200, L"OK.");
        if (cmd == "SYST") return Reply(215, L"UNIX Type: L8");
        if (cmd == "FEAT") return CmdFeat();
        if (cmd == "OPTS") return CmdOpts(arg);
        if (cmd == "AUTH" || cmd == "PBSZ" || cmd == "PROT") return Reply(502, L"TLS is not supported.");
        if (cmd == "HELP") return Reply(214, L"Help OK.");

        if (!loggedIn_) return Reply(530, L"Please login with USER and PASS.");

        if (cmd == "PWD" || cmd == "XPWD") return Reply(257, QuotePath(cwd_) + L" is the current directory.");
        if (cmd == "CWD" || cmd == "XCWD") return CmdCwd(arg);
        if (cmd == "CDUP" || cmd == "XCUP") return CmdCwd(L"..");
        if (cmd == "TYPE") return CmdType(arg);
        if (cmd == "MODE") return Reply(_wcsicmp(arg.c_str(), L"S") == 0 ? 200 : 504, L"Mode set.");
        if (cmd == "STRU") return Reply(_wcsicmp(arg.c_str(), L"F") == 0 ? 200 : 504, L"Structure set.");
        if (cmd == "ALLO") return Reply(202, L"No storage allocation necessary.");
        if (cmd == "PASV") return CmdPasv(false);
        if (cmd == "EPSV") return CmdPasv(true);
        if (cmd == "PORT") return CmdPort(arg);
        if (cmd == "EPRT") return CmdEprt(arg);
        if (cmd == "REST") return CmdRest(arg);
        if (cmd == "STOR") return CmdStore(arg, false, false);
        if (cmd == "APPE") return CmdStore(arg, true, false);
        if (cmd == "STOU") return CmdStore(arg, false, true);
        if (cmd == "RETR") return CmdRetr(arg);
        if (cmd == "LIST") return CmdList(arg, false);
        if (cmd == "NLST") return CmdList(arg, true);
        if (cmd == "MKD" || cmd == "XMKD") return CmdMkd(arg);
        if (cmd == "RMD" || cmd == "XRMD") return CmdRmd(arg);
        if (cmd == "DELE") return CmdDele(arg);
        if (cmd == "RNFR") return CmdRnfr(arg);
        if (cmd == "RNTO") return CmdRnto(arg);
        if (cmd == "SIZE") return CmdSize(arg);
        if (cmd == "MDTM") return CmdMdtm(arg);
        if (cmd == "ABOR") return Reply(226, L"No transfer to abort.");
        if (cmd == "STAT") return Reply(211, L"ScanReceiver FTP server status OK.");
        if (cmd == "SITE") return Reply(202, L"SITE command ignored.");
        Reply(502, L"Command not implemented.");
    }

    // ---- login ------------------------------------------------------------------------------

    void CmdUser(const std::wstring& arg) {
        user_ = arg;
        loggedIn_ = false;
        Reply(331, L"Password required.");
    }

    void CmdPass(const std::wstring& arg) {
        bool anonymous = _wcsicmp(user_.c_str(), L"anonymous") == 0 || _wcsicmp(user_.c_str(), L"ftp") == 0;
        bool ok = cfg_.user.empty() || (anonymous && cfg_.allowAnonymous) ||
                  (_wcsicmp(user_.c_str(), cfg_.user.c_str()) == 0 && arg == cfg_.password);
        if (ok) {
            loggedIn_ = true;
            Log(L"로그인 성공: " + user_);
            Reply(230, L"Login successful.");
            return;
        }
        Log(L"로그인 실패: " + user_);
        Sleep(1000);
        Reply(530, L"Login incorrect.");
        if (++failedLogins_ >= 3) quit_ = true;
    }

    void CmdFeat() {
        const char* feat =
            "211-Features:\r\n UTF8\r\n SIZE\r\n MDTM\r\n EPSV\r\n EPRT\r\n PASV\r\n REST STREAM\r\n"
            "211 End\r\n";
        SendAll(ctrl_, feat, strlen(feat));
    }

    void CmdOpts(const std::wstring& arg) {
        if (_wcsicmp(arg.c_str(), L"UTF8 ON") == 0 || _wcsicmp(arg.c_str(), L"UTF8") == 0) {
            codepage_ = CP_UTF8;
            utf8Forced_ = true;
            return Reply(200, L"UTF8 mode enabled.");
        }
        if (_wcsicmp(arg.c_str(), L"UTF8 OFF") == 0) {
            codepage_ = CP_ACP;
            utf8Forced_ = false;
            return Reply(200, L"UTF8 mode disabled.");
        }
        Reply(501, L"Option not understood.");
    }

    // ---- navigation / settings --------------------------------------------------------------

    void CmdCwd(const std::wstring& arg) {
        std::wstring v = Resolve(arg);
        if (!IsDir(LocalPath(v))) return Reply(550, L"No such directory.");
        cwd_ = v;
        Reply(250, L"Directory changed to " + cwd_);
    }

    void CmdType(const std::wstring& arg) {
        wchar_t t = arg.empty() ? L' ' : towupper(arg[0]);
        if (t == L'A' || t == L'I' || t == L'L') return Reply(200, L"Type set to " + std::wstring(1, t) + L".");
        Reply(504, L"Type not supported.");
    }

    void CmdPasv(bool extended) {
        ClosePassive();
        havePort_ = false;

        sockaddr_in local{};
        int len = sizeof local;
        getsockname(ctrl_, (sockaddr*)&local, &len);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return Reply(425, L"Cannot open passive connection.");

        sockaddr_in addr = local;
        bool bound = false;
        int count = (cfg_.pasvPortMin != 0 && cfg_.pasvPortMax >= cfg_.pasvPortMin)
                        ? cfg_.pasvPortMax - cfg_.pasvPortMin + 1
                        : 0;
        if (count == 0) {
            addr.sin_port = 0;
            bound = bind(s, (sockaddr*)&addr, sizeof addr) == 0;
        }
        for (int i = 0; i < count && !bound; ++i) {
            addr.sin_port = htons(server_->NextPasvPort());
            bound = bind(s, (sockaddr*)&addr, sizeof addr) == 0;
        }
        if (!bound || listen(s, 1) != 0) {
            closesocket(s);
            return Reply(425, L"Cannot open passive connection.");
        }
        len = sizeof addr;
        getsockname(s, (sockaddr*)&addr, &len);
        server_->Track(s);
        pasv_ = s;

        unsigned port = ntohs(addr.sin_port);
        if (extended) return Reply(229, L"Entering Extended Passive Mode (|||" + std::to_wstring(port) + L"|)");
        const auto& b = local.sin_addr.S_un.S_un_b;
        wchar_t buf[96];
        swprintf(buf, 96, L"Entering Passive Mode (%u,%u,%u,%u,%u,%u).", b.s_b1, b.s_b2, b.s_b3, b.s_b4,
                 port >> 8, port & 0xff);
        Reply(227, buf);
    }

    bool SetActiveTarget(unsigned a, unsigned b, unsigned c, unsigned d, unsigned port) {
        if (a > 255 || b > 255 || c > 255 || d > 255 || port == 0 || port > 65535) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.S_un.S_un_b = {(UCHAR)a, (UCHAR)b, (UCHAR)c, (UCHAR)d};
        addr.sin_port = htons((u_short)port);
        // Refuse FTP bounce: the data connection must go back to the client itself.
        if (addr.sin_addr.s_addr != peer_.sin_addr.s_addr) return false;
        ClosePassive();
        portAddr_ = addr;
        havePort_ = true;
        return true;
    }

    void CmdPort(const std::wstring& arg) {
        unsigned h1, h2, h3, h4, p1, p2;
        if (swscanf(arg.c_str(), L"%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6 || p1 > 255 ||
            p2 > 255 || !SetActiveTarget(h1, h2, h3, h4, p1 * 256 + p2))
            return Reply(501, L"Illegal PORT command.");
        Reply(200, L"PORT command successful.");
    }

    void CmdEprt(const std::wstring& arg) {
        // |1|192.168.0.10|6275|
        if (arg.size() < 2) return Reply(501, L"Illegal EPRT command.");
        wchar_t delim = arg[0];
        std::vector<std::wstring> f;
        size_t i = 1;
        for (size_t j; (j = arg.find(delim, i)) != std::wstring::npos; i = j + 1) f.push_back(arg.substr(i, j - i));
        unsigned a, b, c, d, port;
        if (f.size() < 3 || f[0] != L"1") return Reply(522, L"Only IPv4 is supported, use (1).");
        if (swscanf(f[1].c_str(), L"%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
            swscanf(f[2].c_str(), L"%u", &port) != 1 || !SetActiveTarget(a, b, c, d, port))
            return Reply(501, L"Illegal EPRT command.");
        Reply(200, L"EPRT command successful.");
    }

    void CmdRest(const std::wstring& arg) {
        wchar_t* end = nullptr;
        unsigned long long v = wcstoull(arg.c_str(), &end, 10);
        if (arg.empty() || (end && *end)) return Reply(501, L"Invalid restart position.");
        restOffset_ = v;
        Reply(350, L"Restarting at " + std::to_wstring(v) + L".");
    }

    // ---- transfers --------------------------------------------------------------------------

    std::wstring UniqueName() const {
        SYSTEMTIME st;
        GetLocalTime(&st);
        for (int n = 0;; ++n) {
            wchar_t buf[64];
            swprintf(buf, 64, L"scan_%04u%02u%02u_%02u%02u%02u_%d", st.wYear, st.wMonth, st.wDay, st.wHour,
                     st.wMinute, st.wSecond, n);
            std::wstring v = NormalizeVirtual(cwd_, buf);
            if (GetFileAttributesW(LocalPath(v).c_str()) == INVALID_FILE_ATTRIBUTES) return v;
        }
    }

    void CmdStore(const std::wstring& arg, bool append, bool unique) {
        uint64_t rest = restOffset_;
        restOffset_ = 0;
        if (!unique && arg.empty()) return Reply(501, L"File name required.");
        std::wstring vpath = unique ? UniqueName() : Resolve(arg);
        if (vpath == L"/") return Reply(550, L"Invalid file name.");
        std::wstring local = LocalPath(vpath);
        if (IsDir(local)) return Reply(550, L"A directory with that name exists.");
        if (!HasDataTarget()) return Reply(425, L"Use PASV or PORT first.");

        bool existed = IsFile(local);
        DWORD disposition = (append || rest) ? OPEN_ALWAYS : CREATE_ALWAYS;
        HANDLE h = CreateFileW(local.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, disposition,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            Log(L"파일 생성 실패 (오류 " + std::to_wstring(GetLastError()) + L"): " + local);
            ClosePassive();
            havePort_ = false;
            return Reply(550, L"Cannot create file.");
        }
        LARGE_INTEGER pos{};
        if (append) {
            SetFilePointerEx(h, pos, nullptr, FILE_END);
        } else if (rest) {
            pos.QuadPart = (LONGLONG)rest;
            SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
            SetEndOfFile(h);
        }

        Reply(150, unique ? L"FILE: " + LastSegment(vpath) : L"Opening data connection.");
        SOCKET d = OpenData();
        if (d == INVALID_SOCKET) {
            CloseHandle(h);
            if (!existed) DeleteFileW(local.c_str());
            return Reply(425, L"Cannot open data connection.");
        }

        std::vector<char> buf(kBufferSize);
        uint64_t received = 0;
        bool ok = true;
        DWORD start = GetTickCount();
        for (;;) {
            int n = recv(d, buf.data(), (int)buf.size(), 0);
            if (n == 0) break;
            DWORD written = 0;
            if (n < 0 || !WriteFile(h, buf.data(), (DWORD)n, &written, nullptr) || written != (DWORD)n) {
                ok = false;
                break;
            }
            received += (uint64_t)n;
        }
        server_->CloseTracked(d);
        LARGE_INTEGER size{};
        GetFileSizeEx(h, &size);
        CloseHandle(h);

        if (!ok) {
            if (!append && !rest) DeleteFileW(local.c_str());
            Log(L"수신 실패: " + vpath);
            return Reply(426, L"Connection closed; transfer aborted.");
        }
        DWORD ms = GetTickCount() - start;
        Log(L"수신 완료: " + vpath + L" (" + std::to_wstring(received) + L" bytes, " + std::to_wstring(ms) +
            L" ms)");
        Reply(226, L"Transfer complete.");
        server_->NotifyFile({local, vpath, (uint64_t)size.QuadPart, peerIp_});
    }

    void CmdRetr(const std::wstring& arg) {
        uint64_t rest = restOffset_;
        restOffset_ = 0;
        std::wstring local = LocalPath(Resolve(arg));
        HANDLE h = CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE || IsDir(local)) {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            return Reply(550, L"File not found.");
        }
        if (!HasDataTarget()) {
            CloseHandle(h);
            return Reply(425, L"Use PASV or PORT first.");
        }
        LARGE_INTEGER pos{};
        pos.QuadPart = (LONGLONG)rest;
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        Reply(150, L"Opening data connection.");
        SOCKET d = OpenData();
        if (d == INVALID_SOCKET) {
            CloseHandle(h);
            return Reply(425, L"Cannot open data connection.");
        }
        std::vector<char> buf(kBufferSize);
        bool ok = true;
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &n, nullptr)) {
                ok = false;
                break;
            }
            if (n == 0) break;
            if (!SendAll(d, buf.data(), n)) {
                ok = false;
                break;
            }
        }
        CloseHandle(h);
        server_->CloseTracked(d);
        Reply(ok ? 226 : 426, ok ? L"Transfer complete." : L"Transfer aborted.");
    }

    void CmdList(std::wstring arg, bool namesOnly) {
        // Skip "ls"-style options such as "-la".
        while (!arg.empty() && arg[0] == L'-') {
            size_t sp = arg.find(L' ');
            arg = sp == std::wstring::npos ? L"" : arg.substr(sp + 1);
        }
        std::wstring local = LocalPath(Resolve(arg));
        DWORD attr = GetFileAttributesW(local.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            ClosePassive();
            havePort_ = false;
            return Reply(550, L"No such file or directory.");
        }

        std::string listing;
        auto add = [&](const WIN32_FIND_DATAW& fd) {
            std::string name = Narrow(fd.cFileName, codepage_);
            if (namesOnly) {
                listing += name + "\r\n";
                return;
            }
            bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            char head[96];
            snprintf(head, sizeof head, "%s 1 ftp ftp %14llu %s ", dir ? "drwxr-xr-x" : "-rw-r--r--",
                     dir ? 0ULL : (unsigned long long)FileSize(fd), ListTime(fd.ftLastWriteTime).c_str());
            listing += head + name + "\r\n";
        };

        WIN32_FIND_DATAW fd;
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring pattern = local;
            if (pattern.back() != L'\\') pattern += L'\\';
            pattern += L'*';
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) add(fd);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        } else if (FindOne(local, fd)) {
            add(fd);
        }

        if (!HasDataTarget()) return Reply(425, L"Use PASV or PORT first.");
        Reply(150, L"Here comes the directory listing.");
        SOCKET d = OpenData();
        if (d == INVALID_SOCKET) return Reply(425, L"Cannot open data connection.");
        bool ok = SendAll(d, listing.data(), listing.size());
        server_->CloseTracked(d);
        Reply(ok ? 226 : 426, ok ? L"Directory send OK." : L"Transfer aborted.");
    }

    // ---- file management --------------------------------------------------------------------

    void CmdMkd(const std::wstring& arg) {
        std::wstring v = Resolve(arg);
        std::wstring local = LocalPath(v);
        if (IsDir(local)) return Reply(550, L"Directory already exists.");
        if (!CreateDirectoryW(local.c_str(), nullptr)) return Reply(550, L"Cannot create directory.");
        Log(L"폴더 생성: " + v);
        Reply(257, QuotePath(v) + L" created.");
    }

    void CmdRmd(const std::wstring& arg) {
        std::wstring v = Resolve(arg);
        if (v == L"/" || !RemoveDirectoryW(LocalPath(v).c_str())) return Reply(550, L"Cannot remove directory.");
        Log(L"폴더 삭제: " + v);
        Reply(250, L"Directory removed.");
    }

    void CmdDele(const std::wstring& arg) {
        std::wstring v = Resolve(arg);
        if (!DeleteFileW(LocalPath(v).c_str())) return Reply(550, L"Cannot delete file.");
        Log(L"파일 삭제: " + v);
        Reply(250, L"File deleted.");
    }

    void CmdRnfr(const std::wstring& arg) {
        std::wstring v = Resolve(arg);
        if (v == L"/" || GetFileAttributesW(LocalPath(v).c_str()) == INVALID_FILE_ATTRIBUTES) {
            renameFrom_.clear();
            return Reply(550, L"File not found.");
        }
        renameFrom_ = v;
        Reply(350, L"Ready for RNTO.");
    }

    void CmdRnto(const std::wstring& arg) {
        if (renameFrom_.empty()) return Reply(503, L"RNFR required first.");
        std::wstring from = renameFrom_;
        renameFrom_.clear();
        std::wstring to = Resolve(arg);
        if (to == L"/") return Reply(553, L"Invalid name.");
        std::wstring localTo = LocalPath(to);
        if (!MoveFileExW(LocalPath(from).c_str(), localTo.c_str(), MOVEFILE_REPLACE_EXISTING))
            return Reply(553, L"Rename failed.");
        Log(L"이름 변경: " + from + L" -> " + to);
        Reply(250, L"Rename successful.");
        // Some scanners upload to a temp name and rename at the end; report the final file.
        WIN32_FIND_DATAW fd;
        if (IsFile(localTo) && FindOne(localTo, fd))
            server_->NotifyFile({localTo, to, FileSize(fd), peerIp_});
    }

    void CmdSize(const std::wstring& arg) {
        WIN32_FIND_DATAW fd;
        std::wstring local = LocalPath(Resolve(arg));
        if (!IsFile(local) || !FindOne(local, fd)) return Reply(550, L"File not found.");
        Reply(213, std::to_wstring(FileSize(fd)));
    }

    void CmdMdtm(const std::wstring& arg) {
        WIN32_FIND_DATAW fd;
        std::wstring local = LocalPath(Resolve(arg));
        if (!IsFile(local) || !FindOne(local, fd)) return Reply(550, L"File not found.");
        SYSTEMTIME st;
        FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
        wchar_t buf[32];
        swprintf(buf, 32, L"%04u%02u%02u%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                 st.wSecond);
        Reply(213, buf);
    }

    FtpServer* server_;
    const FtpConfig cfg_;
    SOCKET ctrl_;
    sockaddr_in peer_;
    std::wstring peerIp_;
    std::string recvBuf_;
    UINT codepage_ = CP_UTF8;
    bool utf8Forced_ = false;
    bool loggedIn_ = false;
    bool quit_ = false;
    int failedLogins_ = 0;
    std::wstring user_;
    std::wstring cwd_ = L"/";
    std::wstring renameFrom_;
    SOCKET pasv_ = INVALID_SOCKET;
    bool havePort_ = false;
    sockaddr_in portAddr_{};
    uint64_t restOffset_ = 0;
};

// ---------------------------------------------------------------------------------------------

bool FtpServer::Start(const FtpConfig& cfg, std::wstring& error) {
    if (running_) return true;
    cfg_ = cfg;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = L"소켓을 만들 수 없습니다 (오류 " + std::to_wstring(WSAGetLastError()) + L").";
        return false;
    }
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof exclusive);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg.port);
    if (bind(s, (sockaddr*)&addr, sizeof addr) != 0 || listen(s, SOMAXCONN) != 0) {
        int e = WSAGetLastError();
        closesocket(s);
        if (e == WSAEADDRINUSE || e == WSAEACCES)
            error = L"포트 " + std::to_wstring(cfg.port) +
                    L" 을(를) 이미 다른 프로그램이 사용 중입니다.\n(IIS FTP, FileZilla Server 등을 확인하거나 다른 포트를 사용하세요.)";
        else
            error = L"포트를 열 수 없습니다 (오류 " + std::to_wstring(e) + L").";
        return false;
    }

    listenSock_ = s;
    pasvCursor_ = GetTickCount();
    running_ = true;
    acceptThread_ = std::thread(&FtpServer::AcceptLoop, this);
    Log(L"서버 시작 - 포트 " + std::to_wstring(cfg.port) + L", 저장 폴더 " + cfg.rootDir);
    return true;
}

void FtpServer::Stop() {
    if (!running_.exchange(false)) return;
    closesocket(listenSock_);  // wakes the blocking accept()
    listenSock_ = INVALID_SOCKET;
    if (acceptThread_.joinable()) acceptThread_.join();
    {
        std::lock_guard<std::mutex> lock(socketsMutex_);
        for (SOCKET s : sockets_) shutdown(s, SD_BOTH);
    }
    for (int i = 0; i < 100 && activeSessions_ > 0; ++i) Sleep(50);
    Log(L"서버 중지");
}

void FtpServer::AcceptLoop() {
    while (running_) {
        sockaddr_in peer{};
        int len = sizeof peer;
        SOCKET c = accept(listenSock_, (sockaddr*)&peer, &len);
        if (c == INVALID_SOCKET) {
            if (!running_) break;
            Sleep(50);
            continue;
        }
        Track(c);
        ++activeSessions_;
        std::thread([this, c, peer] {
            {
                FtpSession session(this, c, peer);
                session.Run();
            }
            --activeSessions_;
        }).detach();
    }
}

void FtpServer::Log(const std::wstring& msg) const {
    if (log_) log_(msg);
}

void FtpServer::NotifyFile(const FtpFileEvent& ev) const {
    if (onFile_) onFile_(ev);
}

uint16_t FtpServer::NextPasvPort() {
    uint32_t count = (uint32_t)cfg_.pasvPortMax - cfg_.pasvPortMin + 1;
    return (uint16_t)(cfg_.pasvPortMin + pasvCursor_++ % count);
}

void FtpServer::Track(SOCKET s) {
    std::lock_guard<std::mutex> lock(socketsMutex_);
    sockets_.push_back(s);
}

void FtpServer::CloseTracked(SOCKET& s) {
    if (s == INVALID_SOCKET) return;
    {
        std::lock_guard<std::mutex> lock(socketsMutex_);
        for (size_t i = 0; i < sockets_.size(); ++i) {
            if (sockets_[i] == s) {
                sockets_[i] = sockets_.back();
                sockets_.pop_back();
                break;
            }
        }
    }
    closesocket(s);
    s = INVALID_SOCKET;
}
