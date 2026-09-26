#include "Metrics.h"
#include "../core/Clock.h"
#include <algorithm>
#include <cmath>

namespace mob {
void Metrics::addStage(Stage s, int64_t us) {
    std::lock_guard<std::mutex> lk(mx_);
    auto& a = acc_[s];
    if (us < 0) us = 0;
    double ms = us / 1000.0;
    a.sum += ms; a.n++; a.mx = std::max(a.mx, ms);
}
void Metrics::addFrameInterval(int64_t us) {
    std::lock_guard<std::mutex> lk(mx_);
    float ms = us / 1000.0f;
    ft_[ftPos_] = ms;
    ftPos_ = (ftPos_ + 1) % ft_.size();
    ftCount_ = std::min(ftCount_ + 1, ft_.size());
    if (winFt_.size() < 2000) winFt_.push_back(ms);  // limite fixo: sem crescimento
}
MetricsSnapshot Metrics::tick() {
    int64_t now = nowUs();
    std::lock_guard<std::mutex> lk(mx_);
    double dt = lastTick_ ? (now - lastTick_) / 1e6 : 1.0;
    lastTick_ = now;
    if (dt <= 0) dt = 1;
    MetricsSnapshot s;
    uint64_t r = framesReceived, d = framesDecoded, rd = framesRendered, dr = framesDropped, b = bytesReceived,
             in = inputEvents;
    s.fpsReceived = (r - lastRecv_) / dt;
    s.fpsDecoded = (d - lastDec_) / dt;
    s.fpsRendered = (rd - lastRend_) / dt;
    s.droppedPerSec = (dr - lastDrop_) / dt;
    s.droppedTotal = dr;
    s.bitrateMbps = (b - lastBytes_) * 8.0 / dt / 1e6;
    s.inputRate = (in - lastInput_) / dt;
    lastRecv_ = r; lastDec_ = d; lastRend_ = rd; lastDrop_ = dr; lastBytes_ = b; lastInput_ = in;
    for (int i = 0; i < ST_COUNT; ++i) {
        auto& a = acc_[i];
        if (a.n) s.stage[i] = {a.sum / a.n, a.mx, a.n};
        else s.stage[i] = last_.stage[i].n ? StageStat{last_.stage[i].avgMs, last_.stage[i].maxMs, 0} : StageStat{};
        a = {};
    }
    if (!winFt_.empty()) {
        double sum = 0;
        for (float f : winFt_) sum += f;
        double avg = sum / winFt_.size(), var = 0;
        for (float f : winFt_) var += (f - avg) * (f - avg);
        std::sort(winFt_.begin(), winFt_.end());
        s.frameTimeAvgMs = avg;
        s.frameTimeStdMs = std::sqrt(var / winFt_.size());
        s.frameTimeP99Ms = winFt_[std::min(winFt_.size() - 1, (size_t)(winFt_.size() * 0.99))];
        winFt_.clear();
    }
    s.pendingFrames = pending;
    s.width = width; s.height = height;
    s.hwDecode = hwDecode;
    last_ = s;
    return s;
}
std::vector<float> Metrics::frameTimes() {
    std::lock_guard<std::mutex> lk(mx_);
    std::vector<float> v;
    v.reserve(ftCount_);
    size_t start = (ftPos_ + ft_.size() - ftCount_) % ft_.size();
    for (size_t i = 0; i < ftCount_; ++i) v.push_back(ft_[(start + i) % ft_.size()]);
    return v;
}
void Metrics::reset() {
    std::lock_guard<std::mutex> lk(mx_);
    framesReceived = framesDecoded = framesRendered = framesDropped = bytesReceived = inputEvents = 0;
    lastRecv_ = lastDec_ = lastRend_ = lastDrop_ = lastBytes_ = lastInput_ = 0;
    for (auto& a : acc_) a = {};
    ftPos_ = ftCount_ = 0;
    winFt_.clear();
    last_ = {};
    lastTick_ = 0;
}
}
