// Minimal FTP server for receiving "Scan to FTP" uploads from network scanners / MFPs.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct FtpConfig {
    uint16_t port = 21;
    uint16_t pasvPortMin = 50000;  // 0 = let the OS pick
    uint16_t pasvPortMax = 50100;
    std::wstring rootDir;          // all uploads land under this folder
    std::wstring user;             // empty = any user name / password accepted
    std::wstring password;
    bool allowAnonymous = false;   // "anonymous" / "ftp" with any password
};

struct FtpFileEvent {
    std::wstring localPath;    // full path on disk
    std::wstring virtualPath;  // path as seen by the client, e.g. /scan/doc.pdf
    uint64_t size = 0;
    std::wstring peer;         // client IP
};

class FtpServer {
public:
    using LogHandler = std::function<void(const std::wstring&)>;
    using FileHandler = std::function<void(const FtpFileEvent&)>;

    FtpServer() = default;
    ~FtpServer() { Stop(); }
    FtpServer(const FtpServer&) = delete;
    FtpServer& operator=(const FtpServer&) = delete;

    // Handlers are called from worker threads.
    void SetLogHandler(LogHandler h) { log_ = std::move(h); }
    void SetFileHandler(FileHandler h) { onFile_ = std::move(h); }

    bool Start(const FtpConfig& cfg, std::wstring& error);
    void Stop();
    bool IsRunning() const { return running_; }
    const FtpConfig& Config() const { return cfg_; }

private:
    friend class FtpSession;

    void AcceptLoop();
    void Log(const std::wstring& msg) const;
    void NotifyFile(const FtpFileEvent& ev) const;
    uint16_t NextPasvPort();

    // Sockets registered here are shut down by Stop() so blocked threads wake up.
    void Track(SOCKET s);
    void CloseTracked(SOCKET& s);

    FtpConfig cfg_;
    LogHandler log_;
    FileHandler onFile_;
    SOCKET listenSock_ = INVALID_SOCKET;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    std::atomic<int> activeSessions_{0};
    std::atomic<uint32_t> pasvCursor_{0};
    std::mutex socketsMutex_;
    std::vector<SOCKET> sockets_;
};
