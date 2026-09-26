#pragma once
#include <atomic>
#include <mutex>
#include <array>
#include <vector>
#include <cstdint>

namespace mob {

enum Stage { ST_USB = 0, ST_DECODE, ST_UPLOAD, ST_PRESENT, ST_INPUT, ST_QUEUE, ST_JITTER, ST_COUNT };

struct StageStat { double avgMs = 0, maxMs = 0; int n = 0; };

struct MetricsSnapshot {
    double fpsReceived = 0, fpsDecoded = 0, fpsRendered = 0;
    uint64_t droppedTotal = 0;
    double droppedPerSec = 0;
    double bitrateMbps = 0;
    double inputRate = 0;           // eventos/s enviados ao Android
    StageStat stage[ST_COUNT];
    double frameTimeAvgMs = 0, frameTimeStdMs = 0, frameTimeP99Ms = 0;
    int pendingFrames = 0;
    int width = 0, height = 0;
    bool hwDecode = false;
};

// Coletor de métricas: incrementos atômicos nos caminhos quentes; agregação 1x/s na thread de UI.
class Metrics {
public:
    std::atomic<uint64_t> framesReceived{0}, framesDecoded{0}, framesRendered{0}, framesDropped{0};
    std::atomic<uint64_t> bytesReceived{0}, inputEvents{0};
    std::atomic<int> pending{0}, width{0}, height{0};
    std::atomic<bool> hwDecode{false};

    void addStage(Stage s, int64_t us);
    void addFrameInterval(int64_t us);
    MetricsSnapshot tick();  // fecha a janela atual (chamar ~1x/s)
    MetricsSnapshot last() { std::lock_guard<std::mutex> lk(mx_); return last_; }
    std::vector<float> frameTimes();  // histórico p/ gráfico (ms)
    void reset();

private:
    struct Acc { double sum = 0, mx = 0; int n = 0; };
    std::mutex mx_;
    Acc acc_[ST_COUNT];
    std::array<float, 300> ft_{};
    size_t ftPos_ = 0, ftCount_ = 0;
    std::vector<float> winFt_;
    uint64_t lastRecv_ = 0, lastDec_ = 0, lastRend_ = 0, lastDrop_ = 0, lastBytes_ = 0, lastInput_ = 0;
    int64_t lastTick_ = 0;
    MetricsSnapshot last_;
};
}
