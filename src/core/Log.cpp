#include "Log.h"
#include <cstdarg>
#include <ctime>
#include <deque>

namespace mob {
namespace {
std::mutex g_mx;
FILE* g_file = nullptr;
std::deque<std::string> g_ring;
size_t g_written = 0;
std::string g_path;
constexpr size_t kMaxFile = 4u * 1024u * 1024u;
}

void Log::init(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_path = path;
    std::remove((path + ".old").c_str());
    std::rename(path.c_str(), (path + ".old").c_str());
    g_file = std::fopen(path.c_str(), "w");
}

void Log::write(LogLevel lvl, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    static const char* names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char line[1200];
    std::snprintf(line, sizeof line, "%02d:%02d:%02d %s %s", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                  names[(int)lvl], msg);
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_file) {
        g_written += std::fprintf(g_file, "%s\n", line);
        std::fflush(g_file);
        if (g_written > kMaxFile) {  // rotação: impede crescimento em sessões longas
            std::fclose(g_file);
            std::remove((g_path + ".old").c_str());
            std::rename(g_path.c_str(), (g_path + ".old").c_str());
            g_file = std::fopen(g_path.c_str(), "w");
            g_written = 0;
        }
    }
    g_ring.emplace_back(line);
    if (g_ring.size() > kRing) g_ring.pop_front();
}

std::vector<std::string> Log::recent() {
    std::lock_guard<std::mutex> lk(g_mx);
    return {g_ring.begin(), g_ring.end()};
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}
}
