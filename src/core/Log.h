#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>

namespace mob {
enum class LogLevel { Debug, Info, Warn, Error };

// Logger thread-safe: arquivo rotativo simples + ring buffer em memória (tamanho fixo,
// sem crescimento ilimitado de RAM) para o painel de diagnóstico.
class Log {
public:
    static void init(const std::string& path);
    static void write(LogLevel lvl, const char* fmt, ...);
    static std::vector<std::string> recent();
    static void shutdown();
private:
    static constexpr size_t kRing = 500;
};
}
#define LOGD(...) ::mob::Log::write(::mob::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::mob::Log::write(::mob::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::mob::Log::write(::mob::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::mob::Log::write(::mob::LogLevel::Error, __VA_ARGS__)
