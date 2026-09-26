#include "Benchmark.h"
#include "../core/Clock.h"
#include "../core/Log.h"
#include "../stream/Session.h"
#include <cmath>
#include <cstdio>
#include <fstream>

namespace mob {
void Benchmark::start(Session* s) {
    s_ = s;
    results_.clear();
    idx_ = -1;
    next();
}
void Benchmark::cancel() {
    tester_.cancel();
    idx_ = -1;
}
void Benchmark::next() {
    do { ++idx_; } while (idx_ < (int)configs.size() && !configs[idx_].enabled);
    if (idx_ >= (int)configs.size()) { idx_ = -1; LOGI("benchmark concluído"); return; }
    snaps_.clear();
    hosts_.clear();
    phase_ = Phase::Starting;
    phaseAt_ = nowUs();
    LOGI("benchmark: %s", configs[idx_].name.c_str());
    if (restart) restart(configs[idx_].video);
}
void Benchmark::update(int64_t now) {
    if (idx_ < 0) return;
    now_ = now;
    tester_.update(now);
    double el = (now - phaseAt_) / 1e6;
    switch (phase_) {
    case Phase::Starting:
        if (s_->state() == SessionState::Streaming) { phase_ = Phase::Warmup; phaseAt_ = now; }
        else if (s_->state() == SessionState::Error || el > 20) {
            BenchResult r;
            r.name = configs[idx_].name;
            r.error = s_->error().empty() ? "timeout ao iniciar" : s_->error();
            results_.push_back(r);
            next();
        }
        break;
    case Phase::Warmup:
        if (el >= warmupSec) { phase_ = Phase::Measure; phaseAt_ = now; snaps_.clear(); hosts_.clear(); }
        break;
    case Phase::Measure:
        if (el >= measureSec) {
            if (measureE2E && e2eSamples > 0) {
                phase_ = Phase::E2E;
                phaseAt_ = now;
                tester_.start(s_, e2eSamples);
            } else finish();
        }
        break;
    case Phase::E2E:
        if (!tester_.running() || el > 30) { tester_.cancel(); finish(); }
        break;
    }
}
void Benchmark::onSecond(const MetricsSnapshot& m, const HostUsage& h) {
    if (idx_ >= 0 && phase_ == Phase::Measure) { snaps_.push_back(m); hosts_.push_back(h); }
}
void Benchmark::finish() {
    BenchResult r;
    r.name = configs[idx_].name;
    auto& v = configs[idx_].video;
    char sum[160];
    std::snprintf(sum, sizeof sum, "%s | %s | %d Mbps | max %d px | %s fps | %s", videoModeName(v.mode),
                  codecName(v.codec), v.bitrateMbps, v.maxSize,
                  v.maxFps ? std::to_string(v.maxFps).c_str() : "livre", v.vsync ? "VSync" : "sem VSync");
    r.summary = sum;
    size_t n = snaps_.size();
    if (n) {
        r.valid = true;
        double fmin = 1e9, sq = 0;
        auto avgStage = [&](Stage s) { double t = 0; int c = 0; for (auto& x : snaps_) if (x.stage[s].n) { t += x.stage[s].avgMs; ++c; } return c ? t / c : 0.0; };
        for (auto& s : snaps_) {
            r.fpsRecv += s.fpsReceived; r.fpsRender += s.fpsRendered; fmin = std::min(fmin, s.fpsRendered);
            r.ftAvg += s.frameTimeAvgMs; r.ftStd += s.frameTimeStdMs; r.ftP99 = std::max(r.ftP99, s.frameTimeP99Ms);
            r.dropped += s.droppedPerSec; r.bitrate += s.bitrateMbps;
        }
        r.fpsRecv /= n; r.fpsRender /= n; r.ftAvg /= n; r.ftStd /= n; r.bitrate /= n;
        r.dropped = r.dropped;  // total no período
        for (auto& s : snaps_) sq += (s.fpsRendered - r.fpsRender) * (s.fpsRendered - r.fpsRender);
        r.fpsStd = std::sqrt(sq / n);
        r.fpsMin = fmin;
        r.usb = avgStage(ST_USB); r.decode = avgStage(ST_DECODE); r.upload = avgStage(ST_UPLOAD);
        r.present = avgStage(ST_PRESENT); r.jitter = avgStage(ST_JITTER); r.input = avgStage(ST_INPUT);
        int gpuN = 0;
        for (auto& h : hosts_) {
            r.cpu += h.cpuPercent; r.ram = std::max(r.ram, h.ramMB);
            if (h.gpuPercent >= 0) { r.gpu = (r.gpu < 0 ? 0 : r.gpu) + h.gpuPercent; ++gpuN; }
        }
        r.cpu /= hosts_.size();
        if (gpuN) r.gpu /= gpuN;
        r.hw = snaps_.back().hwDecode;
        r.width = snaps_.back().width; r.height = snaps_.back().height;
    } else {
        r.error = "nenhuma amostra (a tela do celular estava parada?)";
    }
    r.e2eSamples = (int)tester_.results().size();
    r.e2eAvg = tester_.avg(); r.e2eMin = tester_.minv(); r.e2eMax = tester_.maxv();
    results_.push_back(r);
    next();
}
std::string Benchmark::status() const {
    if (idx_ < 0) return "Parado";
    static const char* ph[] = {"iniciando stream", "aquecimento", "medindo", "latência ponta a ponta"};
    return configs[idx_].name + " — " + ph[(int)phase_];
}
float Benchmark::progress() const {
    if (idx_ < 0 || configs.empty()) return 0;
    return (float)results_.size() / (float)configs.size();
}
bool Benchmark::exportCsv(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "config,resumo,resolucao,decoder,fps_recebido,fps_render,fps_desvio,fps_min,frametime_ms,frametime_desvio,"
         "frametime_p99,frames_descartados,bitrate_mbps,usb_ms,decode_ms,upload_ms,present_ms,jitter_ms,input_host_ms,"
         "cpu_pct,gpu_pct,ram_mb,e2e_media_ms,e2e_min_ms,e2e_max_ms,e2e_amostras,erro\n";
    for (auto& r : results_)
        f << r.name << ",\"" << r.summary << "\"," << r.width << "x" << r.height << "," << (r.hw ? "GPU" : "CPU") << ","
          << r.fpsRecv << "," << r.fpsRender << "," << r.fpsStd << "," << r.fpsMin << "," << r.ftAvg << "," << r.ftStd
          << "," << r.ftP99 << "," << r.dropped << "," << r.bitrate << "," << r.usb << "," << r.decode << ","
          << r.upload << "," << r.present << "," << r.jitter << "," << r.input << "," << r.cpu << "," << r.gpu << ","
          << r.ram << "," << r.e2eAvg << "," << r.e2eMin << "," << r.e2eMax << "," << r.e2eSamples << ",\""
          << r.error << "\"\n";
    return true;
}
}
