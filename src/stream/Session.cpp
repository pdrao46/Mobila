#include "Session.h"
#include "ControlMsg.h"
#include "../core/Clock.h"
#include "../core/Log.h"
#include "../video/Decoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <random>
#include <sstream>

#ifndef MOB_SCRCPY_SERVER_VERSION
#define MOB_SCRCPY_SERVER_VERSION "3.1"
#endif

namespace mob {
static const char* kRemoteJar = "/data/local/tmp/mobilador-server.jar";

std::string Session::serverPath() {
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    for (auto p : {exeDir() + sep + "scrcpy-server", exeDir() + sep + "server" + sep + "scrcpy-server"})
        if (fileExists(p)) return p;
    return {};
}

void Session::fail(const std::string& e) {
    {
        std::lock_guard<std::mutex> lk(mx_);
        error_ = e;
    }
    LOGE("sessão: %s", e.c_str());
    state_ = SessionState::Error;
}

bool Session::start(const std::string& serial, const VideoSettings& vs, bool stayAwake, bool showTouches) {
    stop();
    stopping_ = false;
    serial_ = serial;
    {
        std::lock_guard<std::mutex> lk(mx_);
        error_.clear();
    }
    frames_.setCapacity(vs.frameQueue);
    state_ = SessionState::Connecting;
    worker_ = std::thread(&Session::run, this, serial, vs, stayAwake, showTouches);
    return true;
}

void Session::stop() {
    stopping_ = true;
    video_.shutdown();
    control_.shutdown();
    if (worker_.joinable()) worker_.join();
    if (ctrlRx_.joinable()) ctrlRx_.join();
    video_.close();
    control_.close();
    if (server_.running()) server_.kill();
    if (!serial_.empty() && !adb_.path().empty())
        adb_.run(serial_, {"forward", "--remove", "tcp:" + std::to_string(port_)}, 3000);
    frames_.clear();
    vw_ = vh_ = 0;
    state_ = SessionState::Idle;
}

std::vector<std::string> Session::listEncoders(Adb& adb, const std::string& serial) {
    std::vector<std::string> out;
    std::string local = serverPath();
    if (local.empty()) return out;
    if (adb.run(serial, {"push", local, kRemoteJar}, 20000).exitCode != 0) return out;
    auto r = adb.shell(serial, std::string("CLASSPATH=") + kRemoteJar +
                                   " app_process / com.genymobile.scrcpy.Server " MOB_SCRCPY_SERVER_VERSION
                                   " list_encoders=true log_level=info", 15000);
    std::istringstream is(r.out);
    std::string l;
    while (std::getline(is, l)) {
        auto c = l.find("--video-codec=");
        if (c == std::string::npos) continue;
        std::string codec = l.substr(c + 14, l.find(' ', c) - (c + 14));
        auto e = l.find("--video-encoder=");
        std::string enc = e == std::string::npos ? "" : l.substr(e + 16, l.find_first_of(" \r", e + 16) - (e + 16));
        bool hw = l.find("(hw)") != std::string::npos;
        out.push_back(codec + "  " + enc + (hw ? "  [HW]" : "  [SW]"));
    }
    return out;
}

void Session::run(std::string serial, VideoSettings vs, bool stayAwake, bool showTouches) {
    std::string local = serverPath();
    if (local.empty()) return fail("scrcpy-server não encontrado ao lado do executável");

    // 1) enviar servidor (≈60 KB; custo único por conexão)
    auto r = adb_.run(serial, {"push", local, kRemoteJar}, 20000);
    if (r.exitCode != 0) return fail("falha no adb push: " + r.out);
    if (stopping_) return;

    // 2) túnel USB: porta local -> socket abstrato no Android (forward: sem corrida de conexão reversa)
    std::mt19937 rng((unsigned)nowUs());
    uint32_t scid = rng() & 0x7fffffff;
    char scidHex[16];
    std::snprintf(scidHex, sizeof scidHex, "%08x", scid);
    bool fwdOk = false;
    for (int i = 0; i < 20 && !fwdOk; ++i) {
        port_ = (uint16_t)(27183 + (rng() % 800));
        fwdOk = adb_.run(serial, {"forward", "tcp:" + std::to_string(port_),
                                  std::string("localabstract:scrcpy_") + scidHex}, 5000).exitCode == 0;
    }
    if (!fwdOk) return fail("falha ao criar túnel adb forward");

    // 3) iniciar servidor no Android
    std::vector<std::string> args = adb_.baseArgs(serial);
    args.insert(args.end(), {"shell", std::string("CLASSPATH=") + kRemoteJar, "app_process", "/",
                             "com.genymobile.scrcpy.Server", MOB_SCRCPY_SERVER_VERSION,
                             std::string("scid=") + scidHex, "log_level=info", "tunnel_forward=true",
                             "audio=false", "control=true", std::string("video_codec=") + codecName(vs.codec),
                             "video_bit_rate=" + std::to_string(vs.bitrateMbps * 1000000),
                             "max_size=" + std::to_string(vs.maxSize), "send_frame_meta=true",
                             "cleanup=true", "power_off_on_close=false"});
    if (vs.maxFps > 0) args.push_back("max_fps=" + std::to_string(vs.maxFps));
    if (!vs.encoderName.empty()) args.push_back("video_encoder=" + vs.encoderName);
    if (stayAwake) args.push_back("stay_awake=true");
    if (showTouches) args.push_back("show_touches=true");
    if (vs.mode == VideoMode::UltraLowLatency) {
        // MediaFormat KEY_PRIORITY=0 (tempo real) e KEY_LATENCY=0 (sem fila no encoder, Android 11+)
        args.push_back("video_codec_options=priority:int=0,latency:int=0");
    }
    if (!server_.start(args)) return fail("falha ao iniciar servidor no Android");

    // 4) conectar socket de vídeo; o byte "dummy" confirma que o servidor realmente escuta
    bool connected = false;
    for (int i = 0; i < 100 && !stopping_; ++i) {
        if (video_.connectLocal(port_, 500)) {
            uint8_t dummy;
            if (video_.recvAll(&dummy, 1)) { connected = true; break; }
            video_.close();
        }
        if (!server_.running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!connected) {
        std::string out = server_.drainOutput();
        return fail("servidor não respondeu" + (out.empty() ? std::string() : (": " + out.substr(0, 400))));
    }
    if (!control_.connectLocal(port_, 2000)) return fail("falha ao conectar socket de controle");
    video_.setRecvBuffer(1 << 20);

    uint8_t name[64];
    if (!video_.recvAll(name, 64)) return fail("falha ao ler metadados do dispositivo");
    name[63] = 0;
    uint8_t meta[12];
    if (!video_.recvAll(meta, 12)) return fail("falha ao ler metadados do codec");
    uint32_t codecId = rd32(meta);
    if (codecId == 0 || codecId == 1) return fail("o dispositivo recusou o codec/encoder selecionado");
    vw_ = (int)rd32(meta + 4);
    vh_ = (int)rd32(meta + 8);
    metrics_.width = vw_.load();
    metrics_.height = vh_.load();
    {
        std::lock_guard<std::mutex> lk(mx_);
        devName_ = (const char*)name;
    }
    LOGI("stream: %s %dx%d codec 0x%08x porta %u", (const char*)name, vw_.load(), vh_.load(), codecId, port_);
    ctrlRx_ = std::thread(&Session::ctrlRxLoop, this);
    videoLoop(codecId, vs);
}

void Session::ctrlRxLoop() {
    uint8_t buf[4096];
    while (!stopping_ && control_.recvSome(buf, sizeof buf) > 0) {
    }
}

void Session::videoLoop(uint32_t codecId, const VideoSettings& vs) {
    setThreadLatencyCritical("mob-video");
    Decoder dec;
    bool hw = vs.decoder != DecoderPref::Software;
    if (!dec.open(codecId, hw, hw)) return fail("falha ao abrir decodificador");
    metrics_.hwDecode = dec.isHw();
    {
        std::lock_guard<std::mutex> lk(mx_);
        decName_ = dec.name();
    }
    state_ = SessionState::Streaming;

    std::vector<uint8_t> buf, config, merged;
    buf.reserve(1 << 20);
    AVFrame* tmp = av_frame_alloc();
    int64_t baseline = INT64_MAX, baselineSetAt = 0;
    constexpr int kPad = AV_INPUT_BUFFER_PADDING_SIZE;

    while (!stopping_) {
        uint8_t hdr[12];
        if (!video_.recvAll(hdr, 12)) break;
        int64_t tHdr = nowUs();
        uint64_t ptsFlags = rd64(hdr);
        uint32_t size = rd32(hdr + 8);
        if (size == 0 || size > (64u << 20)) { fail("pacote de vídeo inválido"); break; }
        if (buf.size() < size + kPad) buf.resize(size + kPad);  // cresce só até o maior pacote; reutilizado
        if (!video_.recvAll(buf.data(), size)) break;
        int64_t tBody = nowUs();
        std::memset(buf.data() + size, 0, kPad);
        metrics_.bytesReceived += size + 12;

        bool isConfig = ptsFlags >> 63;
        bool key = (ptsFlags >> 62) & 1;
        int64_t pts = (int64_t)(ptsFlags & ((1ULL << 62) - 1));
        if (isConfig) {  // SPS/PPS: guardado e prefixado ao próximo pacote
            config.assign(buf.begin(), buf.begin() + size);
            continue;
        }
        metrics_.framesReceived++;
        metrics_.addStage(ST_USB, tBody - tHdr);

        // Atraso relativo do pipeline (captura+encode+USB) acima do melhor caso observado:
        // relógios diferentes impedem valor absoluto; a variação revela filas no dispositivo/USB.
        int64_t rel = tBody - pts;
        if (rel < baseline || tBody - baselineSetAt > 10000000) { baseline = rel; baselineSetAt = tBody; }
        metrics_.addStage(ST_JITTER, rel - baseline);

        const uint8_t* data = buf.data();
        int len = (int)size;
        if (!config.empty()) {
            merged.resize(config.size() + size + kPad);
            std::memcpy(merged.data(), config.data(), config.size());
            std::memcpy(merged.data() + config.size(), buf.data(), size);
            std::memset(merged.data() + config.size() + size, 0, kPad);
            data = merged.data();
            len = (int)(config.size() + size);
            config.clear();
        }
        AVFrame* f = dec.decode(data, len, pts, key);
        int64_t tDec = nowUs();
        metrics_.addStage(ST_DECODE, tDec - tBody);
        if (!f) continue;
        metrics_.framesDecoded++;
        if (f->width != vw_ || f->height != vh_) {  // rotação do aparelho muda as dimensões
            vw_ = f->width; vh_ = f->height;
            metrics_.width = vw_.load(); metrics_.height = vh_.load();
        }
        if (probeActive_) probeFrame(f, tDec);
        av_frame_unref(tmp);
        av_frame_move_ref(tmp, f);
        tmp->best_effort_timestamp = tDec;  // usado pela UI para medir tempo em fila
        if (frames_.push(tmp)) metrics_.framesDropped++;
        metrics_.pending = frames_.size();
        if (onFrame) onFrame();
    }
    av_frame_free(&tmp);
    if (!stopping_ && state_ != SessionState::Error) fail("conexão de vídeo encerrada (cabo/USB/ADB)");
}

bool Session::sendRaw(const uint8_t* data, size_t len) {
    if (state_ != SessionState::Streaming) return false;
    std::lock_guard<std::mutex> lk(sendMx_);
    return control_.sendAll(data, len);
}
bool Session::sendTouch(uint8_t action, uint64_t pointerId, int x, int y) {
    int w = vw_, h = vh_;
    if (!w || !h) return false;
    x = x < 0 ? 0 : (x >= w ? w - 1 : x);
    y = y < 0 ? 0 : (y >= h ? h - 1 : y);
    uint8_t b[kTouchMsgSize];
    buildTouch(b, action, pointerId, x, y, (uint16_t)w, (uint16_t)h, action != AMOTION_UP);
    return sendRaw(b, sizeof b);
}
bool Session::sendKey(uint8_t action, uint32_t keycode) {
    uint8_t b[kKeyMsgSize];
    buildKey(b, action, keycode, 0, 0);
    return sendRaw(b, sizeof b);
}

void Session::setProbeRegion(float nx, float ny, float nw, float nh) {
    pnx_ = nx; pny_ = ny; pnw_ = nw; pnh_ = nh;
    probeLast_ = -1;
    probeArmed_ = false;
    probeActive_ = true;
}
void Session::armProbe() {
    probeBaseline_ = probeLast_.load();
    probeDetected_ = 0;
    probeArmed_ = true;
}
void Session::probeFrame(const AVFrame* f, int64_t t) {
    // Plano Y é data[0] tanto em NV12 quanto YUV420P.
    int x0 = (int)(pnx_ * f->width), y0 = (int)(pny_ * f->height);
    int x1 = x0 + std::max(2, (int)(pnw_ * f->width)), y1 = y0 + std::max(2, (int)(pnh_ * f->height));
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(f->width, x1); y1 = std::min(f->height, y1);
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; y += 2)
        for (int x = x0; x < x1; x += 2) { sum += f->data[0][y * f->linesize[0] + x]; ++n; }
    if (!n) return;
    double mean = sum / n;
    probeLast_ = mean;
    if (!probeArmed_) return;
    if (probeBaseline_ < 0) { probeBaseline_ = mean; return; }
    if (!probeDetected_ && std::fabs(mean - probeBaseline_) > 6.0) probeDetected_ = t;
}
}
