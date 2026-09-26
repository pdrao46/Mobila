#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace mob {
class Session;

// Medição REAL ponta a ponta (sem estimativas):
//   injeta um toque -> Android desenha o indicador de toque ("mostrar toques")
//   -> captura -> encode -> USB -> decode no PC -> detecção de mudança de luminância na região.
// Resultado = input + captura + encode + USB + decode. Somando upload+present medidos no PC
// obtém-se a latência "input-to-photon" (exceto scan-out do monitor).
class LatencyTester {
public:
    void start(Session* s, int samples, float nx = 0.5f, float ny = 0.5f);
    void cancel();
    void update(int64_t nowUs);  // chamar a cada iteração do loop principal
    bool running() const { return phase_ != Phase::Idle; }
    const std::vector<double>& results() const { return res_; }
    int timeouts() const { return timeouts_; }
    int target() const { return target_; }
    std::string status() const;
    double avg() const, minv() const, maxv() const;
private:
    enum class Phase { Idle, Baseline, WaitDetect, Cooldown };
    Session* s_ = nullptr;
    Phase phase_ = Phase::Idle;
    int target_ = 0, timeouts_ = 0;
    float nx_ = 0.5f, ny_ = 0.5f;
    int64_t t0_ = 0, phaseAt_ = 0;
    bool primed_ = false;
    std::vector<double> res_;
};
}
