#include "LatencyTester.h"
#include "../stream/ControlMsg.h"
#include "../stream/Session.h"
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace mob {
static constexpr uint64_t kProbePointer = 99;

void LatencyTester::start(Session* s, int samples, float nx, float ny) {
    s_ = s;
    target_ = samples;
    nx_ = nx; ny_ = ny;
    res_.clear();
    timeouts_ = 0;
    s_->setProbeRegion(nx_ - 0.02f, ny_ - 0.02f, 0.04f, 0.04f);
    // toque inicial: garante frames novos (o servidor só envia quando a tela muda),
    // assim o rastreador conhece a luminância da região antes da primeira amostra
    int x = (int)(nx_ * s_->videoWidth()), y = (int)(ny_ * s_->videoHeight());
    s_->sendTouch(AMOTION_DOWN, kProbePointer, x, y);
    s_->sendTouch(AMOTION_UP, kProbePointer, x, y);
    phase_ = Phase::Cooldown;
    phaseAt_ = 0;
    primed_ = false;
}
void LatencyTester::cancel() {
    if (s_ && phase_ == Phase::WaitDetect)
        s_->sendTouch(AMOTION_UP, kProbePointer, (int)(nx_ * s_->videoWidth()), (int)(ny_ * s_->videoHeight()));
    if (s_) s_->clearProbe();
    phase_ = Phase::Idle;
}
void LatencyTester::update(int64_t now) {
    if (phase_ == Phase::Idle || !s_) return;
    if (s_->state() != SessionState::Streaming) { cancel(); return; }
    if (!phaseAt_) phaseAt_ = now;
    int x = (int)(nx_ * s_->videoWidth()), y = (int)(ny_ * s_->videoHeight());
    switch (phase_) {
    case Phase::Cooldown:
        if (now - phaseAt_ < 450000) return;  // deixa o indicador de toque desaparecer
        if ((int)res_.size() + timeouts_ >= target_) { cancel(); return; }
        phase_ = Phase::Baseline;
        phaseAt_ = now;
        break;
    case Phase::Baseline:
        s_->armProbe();
        if (!s_->probeBaselineReady()) {
            if (now - phaseAt_ > 2000000) { ++timeouts_; phase_ = Phase::Cooldown; phaseAt_ = now; }
            return;
        }
        t0_ = now;
        s_->sendTouch(AMOTION_DOWN, kProbePointer, x, y);
        phase_ = Phase::WaitDetect;
        break;
    case Phase::WaitDetect: {
        int64_t d = s_->probeDetectedUs();
        if (d) res_.push_back((d - t0_) / 1000.0);
        else if (now - t0_ > 1000000) ++timeouts_;
        else return;
        s_->sendTouch(AMOTION_UP, kProbePointer, x, y);
        s_->disarmProbe();
        phase_ = Phase::Cooldown;
        phaseAt_ = now;
        break;
    }
    default: break;
    }
}
double LatencyTester::avg() const {
    return res_.empty() ? 0 : std::accumulate(res_.begin(), res_.end(), 0.0) / res_.size();
}
double LatencyTester::minv() const { return res_.empty() ? 0 : *std::min_element(res_.begin(), res_.end()); }
double LatencyTester::maxv() const { return res_.empty() ? 0 : *std::max_element(res_.begin(), res_.end()); }
std::string LatencyTester::status() const {
    char b[128];
    std::snprintf(b, sizeof b, "%d/%d amostras (%d sem resposta)", (int)res_.size(), target_, timeouts_);
    return b;
}
}
