#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../core/Config.h"
#include "../core/Platform.h"
#include "../device/Adb.h"
#include "../net/Socket.h"
#include "../stats/Metrics.h"
#include "../video/FrameQueue.h"

namespace mob {

enum class SessionState { Idle, Connecting, Streaming, Error };

// Uma sessão de streaming + controle com um dispositivo Android.
//
// Threads:
//   "mob-video"   recv USB (via túnel ADB) -> decodifica -> FrameQueue. Sem fila de pacotes entre recv e
//                 decode: o pacote é decodificado assim que chega (um salto de thread a menos).
//   "mob-ctrlrx"  drena mensagens do dispositivo (clipboard/acks) para o socket nunca encher.
//   Input é enviado DIRETO da thread de UI/eventos (send() de 32 bytes com TCP_NODELAY em loopback
//   não bloqueia na prática) — evita fila e troca de contexto entre evento e envio.
class Session {
public:
    Session(Adb& adb, Metrics& m) : adb_(adb), metrics_(m) {}
    ~Session() { stop(); }

    bool start(const std::string& serial, const VideoSettings& vs, bool stayAwake, bool showTouches);
    void stop();
    SessionState state() const { return state_; }
    std::string error() { std::lock_guard<std::mutex> lk(mx_); return error_; }
    std::string deviceName() { std::lock_guard<std::mutex> lk(mx_); return devName_; }
    std::string decoderName() { std::lock_guard<std::mutex> lk(mx_); return decName_; }
    const std::string& serial() const { return serial_; }

    FrameQueue& frames() { return frames_; }
    std::function<void()> onFrame;  // chamado na thread de vídeo quando há frame novo

    int videoWidth() const { return vw_; }
    int videoHeight() const { return vh_; }

    // Input (thread de UI). x,y em pixels do frame de vídeo.
    bool sendTouch(uint8_t action, uint64_t pointerId, int x, int y);
    bool sendKey(uint8_t action, uint32_t keycode);
    bool sendRaw(const uint8_t* data, size_t len);

    // Sonda de latência ponta a ponta: mede luminância média de uma região do vídeo.
    void setProbeRegion(float nx, float ny, float nw, float nh);  // ativa rastreio contínuo de luminância
    void clearProbe() { probeActive_ = false; probeArmed_ = false; }
    void armProbe();  // linha base = último frame conhecido
    void disarmProbe() { probeArmed_ = false; }
    bool probeBaselineReady() const { return probeBaseline_ >= 0; }
    int64_t probeDetectedUs() const { return probeDetected_; }

    static std::string serverPath();
    static std::vector<std::string> listEncoders(Adb& adb, const std::string& serial);

private:
    void run(std::string serial, VideoSettings vs, bool stayAwake, bool showTouches);
    void videoLoop(uint32_t codecId, const VideoSettings& vs);
    void ctrlRxLoop();
    void fail(const std::string& e);
    void probeFrame(const struct AVFrame* f, int64_t t);

    Adb& adb_;
    Metrics& metrics_;
    FrameQueue frames_{1};
    std::thread worker_, ctrlRx_;
    std::atomic<bool> stopping_{false};
    std::atomic<SessionState> state_{SessionState::Idle};
    std::mutex mx_, sendMx_;
    std::string error_, devName_, decName_, serial_;
    Socket video_, control_;
    BackgroundProcess server_;
    uint16_t port_ = 27183;
    std::atomic<int> vw_{0}, vh_{0};

    std::atomic<bool> probeArmed_{false}, probeActive_{false};
    std::atomic<double> probeLast_{-1};
    std::atomic<float> pnx_{0}, pny_{0}, pnw_{0}, pnh_{0};
    std::atomic<double> probeBaseline_{-1};
    std::atomic<int64_t> probeDetected_{0};
};
}
