#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../core/Config.h"
#include "../core/Platform.h"
#include "../stats/Metrics.h"
#include "LatencyTester.h"

namespace mob {
class Session;

struct BenchConfig {
    std::string name;
    VideoSettings video;
    bool enabled = true;
};

struct BenchResult {
    std::string name, summary;
    bool valid = false;
    std::string error;
    double fpsRecv = 0, fpsRender = 0, fpsStd = 0, fpsMin = 0;
    double ftAvg = 0, ftStd = 0, ftP99 = 0;
    double dropped = 0, bitrate = 0;
    double usb = 0, decode = 0, upload = 0, present = 0, jitter = 0, input = 0;
    double cpu = 0, gpu = -1, ram = 0;
    double e2eAvg = 0, e2eMin = 0, e2eMax = 0;
    int e2eSamples = 0;
    bool hw = false;
    int width = 0, height = 0;
};

// Executa configurações em sequência com a MESMA metodologia e apresenta todos os números.
// Nenhuma configuração é escolhida automaticamente.
class Benchmark {
public:
    std::vector<BenchConfig> configs;
    int warmupSec = 3, measureSec = 10, e2eSamples = 6;
    bool measureE2E = true;
    std::function<void(const VideoSettings&)> restart;  // fornecido pela App

    void start(Session* s);
    void cancel();
    void update(int64_t nowUs);
    void onSecond(const MetricsSnapshot& m, const HostUsage& h);  // 1x/s
    bool running() const { return idx_ >= 0; }
    std::string status() const;
    float progress() const;
    const std::vector<BenchResult>& results() const { return results_; }
    LatencyTester& tester() { return tester_; }
    bool exportCsv(const std::string& path) const;
private:
    enum class Phase { Starting, Warmup, Measure, E2E };
    void next();
    void finish();
    Session* s_ = nullptr;
    int idx_ = -1;
    Phase phase_ = Phase::Starting;
    int64_t phaseAt_ = 0, now_ = 0;
    std::vector<MetricsSnapshot> snaps_;
    std::vector<HostUsage> hosts_;
    std::vector<BenchResult> results_;
    LatencyTester tester_;
};
}
