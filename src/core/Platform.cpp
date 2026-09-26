#include "Platform.h"
#include "Log.h"
#include <cstdio>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>
#include <pdh.h>
#include <avrt.h>
#include <timeapi.h>
#include <direct.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <sys/resource.h>
#include <cstring>
#include <fstream>
#endif

namespace mob {

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring buildCmdLine(const std::vector<std::string>& args) {
    std::wstring cmd;
    for (auto& a : args) {
        if (!cmd.empty()) cmd += L' ';
        bool q = a.find_first_of(" \t\"") != std::string::npos || a.empty();
        if (!q) { cmd += widen(a); continue; }
        cmd += L'"';
        for (wchar_t c : widen(a)) {
            if (c == L'"') cmd += L'\\';
            cmd += c;
        }
        cmd += L'"';
    }
    return cmd;
}

struct PipeProc {
    PROCESS_INFORMATION pi{};
    HANDLE rd = nullptr;
    bool ok = false;
    bool launch(const std::vector<std::string>& args) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        std::wstring cmd = buildCmdLine(args);
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &si, &pi) != 0;
        CloseHandle(wr);
        if (!ok) { CloseHandle(rd); rd = nullptr; }
        return ok;
    }
    // leitura não bloqueante
    void readAvail(std::string& out, size_t cap) {
        DWORD avail = 0;
        while (rd && PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char buf[4096];
            DWORD n = 0;
            if (!ReadFile(rd, buf, (DWORD)std::min<DWORD>(avail, sizeof buf), &n, nullptr) || n == 0) break;
            out.append(buf, n);
            if (out.size() > cap) out.erase(0, out.size() - cap);
        }
    }
    void close() {
        if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); pi = {}; }
        if (rd) { CloseHandle(rd); rd = nullptr; }
    }
};

ExecResult execCapture(const std::vector<std::string>& args, int timeoutMs) {
    ExecResult r;
    PipeProc p;
    if (!p.launch(args)) { r.out = "falha ao iniciar processo"; return r; }
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        p.readAvail(r.out, 1 << 20);
        DWORD w = WaitForSingleObject(p.pi.hProcess, 5);
        if (w == WAIT_OBJECT_0) break;
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) {
            TerminateProcess(p.pi.hProcess, 1);
            r.out += "\n[timeout]";
            break;
        }
    }
    p.readAvail(r.out, 1 << 20);
    DWORD code = 1;
    GetExitCodeProcess(p.pi.hProcess, &code);
    r.exitCode = (int)code;
    p.close();
    return r;
}

struct BackgroundProcess::Impl { PipeProc p; std::string out; };
BackgroundProcess::BackgroundProcess() = default;
BackgroundProcess::~BackgroundProcess() { kill(); }
bool BackgroundProcess::start(const std::vector<std::string>& args) {
    kill();
    d = std::make_unique<Impl>();
    return d->p.launch(args);
}
bool BackgroundProcess::running() {
    return d && d->p.pi.hProcess && WaitForSingleObject(d->p.pi.hProcess, 0) == WAIT_TIMEOUT;
}
void BackgroundProcess::kill() {
    if (!d) return;
    if (running()) { TerminateProcess(d->p.pi.hProcess, 0); WaitForSingleObject(d->p.pi.hProcess, 2000); }
    d->p.close();
    d.reset();
}
std::string BackgroundProcess::drainOutput() {
    if (!d) return {};
    d->p.readAvail(d->out, 64 * 1024);
    return std::move(d->out);
}

std::string exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    return narrow(s.substr(0, s.find_last_of(L"\\/")));
}
std::string appDataDir() {
    PWSTR p = nullptr;
    std::string r = ".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        r = narrow(p) + "\\Mobilador";
        CoTaskMemFree(p);
    }
    ensureDir(r);
    return r;
}
bool fileExists(const std::string& p) { return GetFileAttributesW(widen(p).c_str()) != INVALID_FILE_ATTRIBUTES; }
void ensureDir(const std::string& p) { CreateDirectoryW(widen(p).c_str(), nullptr); }

struct HostMonitor::Impl {
    ULONGLONG lastProc = 0, lastWall = 0;
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER c = nullptr;
    bool gpuOk = false;
};
HostMonitor::HostMonitor() : d(std::make_unique<Impl>()) {
    // Utilização da GPU pelo próprio processo via contadores "GPU Engine" (Win10 1709+).
    if (PdhOpenQueryW(nullptr, 0, &d->q) == ERROR_SUCCESS) {
        wchar_t path[128];
        swprintf(path, 128, L"\\GPU Engine(pid_%lu_*)\\Utilization Percentage", GetCurrentProcessId());
        d->gpuOk = PdhAddEnglishCounterW(d->q, path, 0, &d->c) == ERROR_SUCCESS;
        if (d->gpuOk) PdhCollectQueryData(d->q);
    }
}
HostMonitor::~HostMonitor() { if (d->q) PdhCloseQuery(d->q); }
HostUsage HostMonitor::sample() {
    HostUsage u;
    FILETIME c, e, k, us, now;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &us);
    GetSystemTimeAsFileTime(&now);
    auto toU = [](FILETIME f) { return (ULONGLONG(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    ULONGLONG proc = toU(k) + toU(us), wall = toU(now);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (d->lastWall && wall > d->lastWall)
        u.cpuPercent = 100.0 * double(proc - d->lastProc) / double(wall - d->lastWall) / si.dwNumberOfProcessors;
    d->lastProc = proc;
    d->lastWall = wall;
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) u.ramMB = pmc.WorkingSetSize / 1048576.0;
    if (d->gpuOk && PdhCollectQueryData(d->q) == ERROR_SUCCESS) {
        DWORD size = 0, count = 0;
        PdhGetFormattedCounterArrayW(d->c, PDH_FMT_DOUBLE, &size, &count, nullptr);
        if (size) {
            std::vector<unsigned char> buf(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
            if (PdhGetFormattedCounterArrayW(d->c, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                double mx = 0;  // engine mais ocupado (3D / VideoDecode)
                for (DWORD i = 0; i < count; ++i) mx = std::max(mx, items[i].FmtValue.doubleValue);
                u.gpuPercent = mx;
            }
        }
    }
    return u;
}

void raiseTimerResolution(bool enable) {
    if (enable) timeBeginPeriod(1); else timeEndPeriod(1);
}
void setThreadLatencyCritical(const char* name) {
    // MMCSS "Games": o agendador prioriza a thread sem precisar de REALTIME_PRIORITY_CLASS.
    DWORD idx = 0;
    HANDLE h = AvSetMmThreadCharacteristicsW(L"Games", &idx);
    if (h) AvSetMmThreadPriority(h, AVRT_PRIORITY_HIGH);
    else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    (void)name;
}

#else  // ---------------- POSIX (desenvolvimento/testes) ----------------

static pid_t spawn(const std::vector<std::string>& args, int& rdfd) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1); dup2(fds[1], 2);
        close(fds[0]); close(fds[1]);
        std::vector<char*> av;
        for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }
    close(fds[1]);
    rdfd = fds[0];
    fcntl(rdfd, F_SETFL, O_NONBLOCK);
    return pid;
}
static void readAvail(int fd, std::string& out, size_t cap) {
    char buf[4096];
    ssize_t n;
    while (fd >= 0 && (n = read(fd, buf, sizeof buf)) > 0) {
        out.append(buf, n);
        if (out.size() > cap) out.erase(0, out.size() - cap);
    }
}
ExecResult execCapture(const std::vector<std::string>& args, int timeoutMs) {
    ExecResult r;
    int fd = -1;
    pid_t pid = spawn(args, fd);
    if (pid < 0) return r;
    auto t0 = std::chrono::steady_clock::now();
    int st = 0;
    for (;;) {
        pollfd p{fd, POLLIN, 0};
        poll(&p, 1, 5);
        readAvail(fd, r.out, 1 << 20);
        if (waitpid(pid, &st, WNOHANG) == pid) break;
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(timeoutMs)) {
            ::kill(pid, SIGKILL); waitpid(pid, &st, 0); r.out += "\n[timeout]"; break;
        }
    }
    readAvail(fd, r.out, 1 << 20);
    close(fd);
    r.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}
struct BackgroundProcess::Impl { pid_t pid = -1; int fd = -1; std::string out; };
BackgroundProcess::BackgroundProcess() = default;
BackgroundProcess::~BackgroundProcess() { kill(); }
bool BackgroundProcess::start(const std::vector<std::string>& args) {
    kill();
    d = std::make_unique<Impl>();
    d->pid = spawn(args, d->fd);
    return d->pid > 0;
}
bool BackgroundProcess::running() {
    int st;
    return d && d->pid > 0 && waitpid(d->pid, &st, WNOHANG) == 0;
}
void BackgroundProcess::kill() {
    if (!d) return;
    if (running()) { ::kill(d->pid, SIGTERM); int st; waitpid(d->pid, &st, 0); }
    if (d->fd >= 0) close(d->fd);
    d.reset();
}
std::string BackgroundProcess::drainOutput() {
    if (!d) return {};
    readAvail(d->fd, d->out, 64 * 1024);
    return std::move(d->out);
}
std::string exeDir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf);
    std::string s(buf, n > 0 ? n : 0);
    return s.substr(0, s.find_last_of('/'));
}
std::string appDataDir() {
    const char* h = getenv("HOME");
    std::string r = std::string(h ? h : ".") + "/.config/mobilador";
    ensureDir(std::string(h ? h : ".") + "/.config");
    ensureDir(r);
    return r;
}
bool fileExists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0; }
void ensureDir(const std::string& p) { mkdir(p.c_str(), 0755); }
struct HostMonitor::Impl { double lastCpu = 0; std::chrono::steady_clock::time_point last; };
HostMonitor::HostMonitor() : d(std::make_unique<Impl>()) {}
HostMonitor::~HostMonitor() = default;
HostUsage HostMonitor::sample() {
    HostUsage u;
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    double cpu = ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - d->last).count();
    if (d->lastCpu > 0 && dt > 0)
        u.cpuPercent = 100.0 * (cpu - d->lastCpu) / dt / std::max(1u, std::thread::hardware_concurrency());
    d->lastCpu = cpu; d->last = now;
    std::ifstream f("/proc/self/statm");
    long pages = 0, rss = 0;
    if (f >> pages >> rss) u.ramMB = rss * (sysconf(_SC_PAGESIZE) / 1048576.0);
    return u;
}
void raiseTimerResolution(bool) {}
void setThreadLatencyCritical(const char*) {}
#endif
}
