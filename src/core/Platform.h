#pragma once
#include <string>
#include <vector>
#include <memory>

namespace mob {

struct ExecResult {
    int exitCode = -1;
    std::string out;  // stdout+stderr
};

// Executa um processo e aguarda (sem janela de console no Windows).
ExecResult execCapture(const std::vector<std::string>& args, int timeoutMs = 15000);

// Processo em segundo plano (ex.: "adb shell app_process ..." do servidor no Android).
class BackgroundProcess {
public:
    BackgroundProcess();
    ~BackgroundProcess();
    bool start(const std::vector<std::string>& args);
    bool running();
    void kill();
    std::string drainOutput();  // saída acumulada (limitada)
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

std::string exeDir();
std::string appDataDir();  // %APPDATA%\Mobilador
bool fileExists(const std::string& p);
void ensureDir(const std::string& p);

// Métricas do processo host.
struct HostUsage {
    double cpuPercent = 0;   // do processo, normalizado por núcleos
    double ramMB = 0;        // working set
    double gpuPercent = -1;  // -1 = indisponível
};
class HostMonitor {
public:
    HostMonitor();
    ~HostMonitor();
    HostUsage sample();  // chamar ~1x/s
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Ajustes de SO para baixa latência (timer 1 ms, prioridade de thread, MMCSS).
void raiseTimerResolution(bool enable);
void setThreadLatencyCritical(const char* name);
}
