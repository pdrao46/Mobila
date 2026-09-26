#include "App.h"
#include "Theme.h"
#include "../core/Clock.h"
#include "../core/Log.h"
#include "../stream/ControlMsg.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <algorithm>
#include <cmath>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace mob {
static constexpr uint64_t kMousePointer = 0;

App::App() {
    cfgPath_ = appDataDir() +
#ifdef _WIN32
               "\\settings.json";
#else
               "/settings.json";
#endif
}
App::~App() = default;

int App::run() {
    if (!init()) return 1;
    int64_t lastUi = 0;
    while (running_) {
        // Espera orientada a eventos: input e frames novos acordam o loop na hora (sem polling fixo).
        int64_t now = nowUs();
        bool play = view_ == View::Play && session_->state() == SessionState::Streaming;
        int64_t uiInterval = play ? 33000 : 16000;  // UI/overlay não precisa de mais que isso
        int timeoutMs = (int)std::clamp<int64_t>((lastUi + uiInterval - now) / 1000, 0, 50);
        SDL_Event e;
        bool newFrame = false;
        if (SDL_WaitEventTimeout(&e, timeoutMs)) {
            do {
                if (e.type == frameEvent_) { newFrame = true; framePending_ = false; }
                else handleEvent(e);
            } while (SDL_PollEvent(&e));
        }
        if (!newFrame && session_->frames().size() > 0) newFrame = true;
        mapper_->flush();  // movimento de mouse agregado do lote -> um único MOVE
        updateLogic();
        now = nowUs();
        if (newFrame || now - lastUi >= uiInterval || !running_) {
            renderVideo(newFrame);
            frame();
            lastUi = now;
        }
    }
    shutdown();
    return 0;
}

bool App::init() {
    Log::init(appDataDir() +
#ifdef _WIN32
              "\\mobilador.log");
#else
              "/mobilador.log");
#endif
    LOGI("MOBILADOR %s — %s", MOB_VERSION, MOB_APP_SUBTITLE);
    cfg_ = Settings::load(cfgPath_);
    showPerf_ = cfg_.ui.showPerfOverlay;
    Socket::globalInit();

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");  // flip-model DXGI + YUV->RGB em shader
#endif
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0");  // Raw Input: deltas diretos do mouse, sem aceleração do Windows
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        LOGE("SDL_Init: %s", SDL_GetError());
        return false;
    }
    float ddpi = 96;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0) ddpi = 96;
    dpiScale_ = std::clamp(ddpi / 96.0f, 1.0f, 3.0f);
    SDL_DisplayMode dm{};
    SDL_GetDesktopDisplayMode(0, &dm);
    int w = std::min((int)(1360 * dpiScale_), dm.w ? dm.w - 80 : 1360);
    int h = std::min((int)(840 * dpiScale_), dm.h ? dm.h - 120 : 840);
    win_ = SDL_CreateWindow(MOB_APP_NAME " — " MOB_APP_SUBTITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) { LOGE("janela: %s", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(win_, 960, 600);
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | (cfg_.video.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, 0);
    if (!ren_) { LOGE("renderer: %s", SDL_GetError()); return false; }
    SDL_RendererInfo ri{};
    SDL_GetRendererInfo(ren_, &ri);
    rendererName_ = ri.name ? ri.name : "?";
    vsyncApplied_ = cfg_.video.vsync;
    LOGI("renderer: %s (%s)", rendererName_.c_str(), (ri.flags & SDL_RENDERER_ACCELERATED) ? "GPU" : "software");
    frameEvent_ = SDL_RegisterEvents(1);
    raiseTimerResolution(true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    static std::string iniPath = appDataDir() + "/ui.ini";
    io.IniFilename = iniPath.c_str();
    ImGui_ImplSDL2_InitForSDLRenderer(win_, ren_);
    ImGui_ImplSDLRenderer2_Init(ren_);

    session_ = std::make_unique<Session>(adb_, metrics_);
    session_->onFrame = [this] {
        // No máximo um evento pendente: se a UI ainda não consumiu, não empilha (sem fila de eventos).
        if (framePending_.exchange(true)) return;
        SDL_Event ev{};
        ev.type = frameEvent_;
        SDL_PushEvent(&ev);
    };
    mapper_ = std::make_unique<InputMapper>(*session_, metrics_);
    profiles_ = std::make_unique<ProfileStore>(appDataDir() +
#ifdef _WIN32
                                               "\\profiles");
#else
                                               "/profiles");
#endif
    profiles_->loadAll();
    if (!profiles_->find(cfg_.activeProfile) && !profiles_->all().empty())
        cfg_.activeProfile = profiles_->all().front().name;
    mapper_->setProfile(activeProfile());

    bench_.restart = [this](const VideoSettings& vs) { restartWith(vs); };
    VideoSettings a, b, c;
    applyVideoPreset(a, VideoMode::UltraLowLatency);
    applyVideoPreset(b, VideoMode::Balanced);
    applyVideoPreset(c, VideoMode::Quality);
    bench_.configs = {{"A — Ultra Low Latency", a}, {"B — Balanced", b}, {"C — Quality", c}};

    splashUntil_ = cfg_.ui.showSplash ? 1.6 : 0;
    watcher_ = std::thread(&App::deviceWatcher, this);
    tickOffsetUs_ = nowUs() - (int64_t)SDL_GetTicks64() * 1000;
    return true;
}

void App::shutdown() {
    LOGI("encerrando");
    tester_.cancel();
    bench_.cancel();
    setMouseCapture(false);
    if (mapper_) mapper_->releaseAll();
    if (session_) session_->stop();
    watcherStop_ = true;
    if (watcher_.joinable()) watcher_.join();
    if (infoThread_.joinable()) infoThread_.join();
    saveSettings();
    raiseTimerResolution(false);
    if (tex_) SDL_DestroyTexture(tex_);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren_);
    SDL_DestroyWindow(win_);
    SDL_Quit();
    Log::shutdown();
}

void App::saveSettings() {
    cfg_.ui.showPerfOverlay = showPerf_;
    if (!cfg_.save(cfgPath_)) LOGW("falha ao salvar configurações");
}

Profile* App::activeProfile() { return profiles_ ? profiles_->find(cfg_.activeProfile) : nullptr; }

int64_t App::eventUs(uint32_t ts) const { return tickOffsetUs_ + (int64_t)ts * 1000; }

// ---------------------------------------------------------------- dispositivos
void App::deviceWatcher() {
    if (!adb_.locate(cfg_.adbPath)) {
        LOGE("ADB não encontrado. Coloque platform-tools ao lado do executável ou no PATH.");
        return;
    }
    adb_.startServer();
    adbVersion_ = adb_.version();
    adbReady_ = true;
    LOGI("ADB: %s (%s)", adb_.path().c_str(), adbVersion_.c_str());
    std::string lastSig;
    while (!watcherStop_) {
        auto list = adb_.devices();
        std::string sig;
        for (auto& d : list) sig += d.serial + d.state + ";";
        if (sig != lastSig) {
            LOGI("dispositivos: %s", sig.empty() ? "(nenhum)" : sig.c_str());
            lastSig = sig;
        }
        {
            std::lock_guard<std::mutex> lk(devMx_);
            devices_ = std::move(list);
        }
        for (int i = 0; i < 15 && !watcherStop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void App::refreshDeviceInfo(const std::string& serial) {
    if (infoBusy_) return;
    if (infoThread_.joinable()) infoThread_.join();
    infoBusy_ = true;
    infoThread_ = std::thread([this, serial] {
        DeviceInfo di = adb_.queryInfo(serial);
        di.videoEncoders = Session::listEncoders(adb_, serial);
        {
            std::lock_guard<std::mutex> lk(devMx_);
            info_ = di;
        }
        infoBusy_ = false;
    });
}

void App::connect() {
    std::vector<AdbDevice> list;
    {
        std::lock_guard<std::mutex> lk(devMx_);
        list = devices_;
    }
    const AdbDevice* pick = nullptr;
    for (auto& d : list)
        if (d.state == "device" && (!pick || (d.isUsb() && !pick->isUsb()))) pick = &d;
    if (!pick) { LOGW("nenhum dispositivo autorizado para conectar"); return; }
    targetSerial_ = pick->serial;
    wantConnected_ = true;
    reconnectTries_ = 0;
    reconnectAt_ = 0;
    metrics_.reset();
    LOGI("conectando a %s (%s)", pick->serial.c_str(), pick->model.c_str());
    session_->start(targetSerial_, cfg_.video, cfg_.stayAwake, cfg_.showTouchesForTest);
    mapper_->setProfile(activeProfile());
    refreshDeviceInfo(targetSerial_);
    view_ = View::Play;
}

void App::disconnect() {
    wantConnected_ = false;
    tester_.cancel();
    bench_.cancel();
    setMouseCapture(false);
    mapper_->releaseAll();
    session_->stop();
    if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; texW_ = texH_ = 0; }
    LOGI("desconectado");
}

void App::restartWith(const VideoSettings& vs) {
    if (targetSerial_.empty()) return;
    mapper_->releaseAll();
    metrics_.reset();
    session_->start(targetSerial_, vs, cfg_.stayAwake, cfg_.showTouchesForTest);
    wantConnected_ = true;
    applyVsync();
}

void App::applyVsync() {
    bool want = bench_.running() ? false : cfg_.video.vsync;
    if (want == vsyncApplied_) return;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_RenderSetVSync(ren_, want ? 1 : 0);
#endif
    vsyncApplied_ = want;
}

void App::updateLogic() {
    int64_t now = nowUs();
    if (now - lastTick_ >= 1000000) {
        lastTick_ = now;
        snap_ = metrics_.tick();
        hostUsage_ = host_.sample();
        bench_.onSecond(snap_, hostUsage_);
        tickOffsetUs_ = nowUs() - (int64_t)SDL_GetTicks64() * 1000;
    }
    if (session_->state() == SessionState::Streaming && now - lastTempUs_ > 20000000 && !infoBusy_) {
        lastTempUs_ = now;
        std::string s = targetSerial_;
        if (infoThread_.joinable()) infoThread_.join();
        infoBusy_ = true;
        infoThread_ = std::thread([this, s] {
            float t = adb_.batteryTemp(s);
            { std::lock_guard<std::mutex> lk(devMx_); info_.batteryTempC = t; }
            infoBusy_ = false;
        });
    }
    // info do aparelho assim que ele aparece (tela inicial mostra modelo/Android/resolução antes de conectar)
    if (!infoBusy_ && session_->state() != SessionState::Streaming) {
        std::string ready, have;
        {
            std::lock_guard<std::mutex> lk(devMx_);
            for (auto& d : devices_) if (d.state == "device") { ready = d.serial; break; }
            have = info_.serial;
        }
        if (!ready.empty() && ready != have && ready != infoTried_) { infoTried_ = ready; refreshDeviceInfo(ready); }
    }
    // reconexão automática com backoff
    if (wantConnected_ && cfg_.autoReconnect && session_->state() == SessionState::Error && !bench_.running()) {
        if (!reconnectAt_) {
            int64_t backoff = std::min<int64_t>(5000000, 700000LL << std::min(reconnectTries_, 3));
            reconnectAt_ = now + backoff;
            if (mouseCapturedSafe()) setMouseCapture(false);
        } else if (now >= reconnectAt_) {
            bool present = false;
            {
                std::lock_guard<std::mutex> lk(devMx_);
                for (auto& d : devices_) if (d.serial == targetSerial_ && d.state == "device") present = true;
            }
            reconnectAt_ = 0;
            if (present) {
                ++reconnectTries_;
                LOGI("reconexão automática (tentativa %d)", reconnectTries_);
                restartWith(cfg_.video);
            }
        }
    }
    if (session_->state() == SessionState::Streaming) reconnectTries_ = 0;
    mapper_->update(now);
    tester_.update(now);
    bench_.update(now);
    if (!tester_.running() && !tester_.results().empty()) lastE2E_ = tester_.avg();
    applyVsync();
}

bool App::mouseCapturedSafe() const { return mapper_ && mapper_->mouseCaptured(); }

// ---------------------------------------------------------------- input
void App::setMouseCapture(bool on) {
    if (!mapper_) return;
    if (on && (session_->state() != SessionState::Streaming)) on = false;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    mapper_->setMouseCaptured(on);
}

bool App::handleHotkey(const SDL_Keysym& ks, bool down) {
    if (!down) return false;
    std::string k = SDL_GetScancodeName(ks.scancode);
    auto& h = cfg_.hotkeys;
    if (k == h.showControls) { showControls_ = true; return true; }
    if (k == h.hideControls) { showControls_ = false; return true; }
    if (k == h.toggleMapping) { mappingEnabled_ = !mappingEnabled_; mapper_->setEnabled(mappingEnabled_); return true; }
    if (k == h.releaseMouse) { setMouseCapture(false); return true; }
    if (k == h.captureMouse) { setMouseCapture(!mapper_->mouseCaptured()); return true; }
    if (k == h.openSettings) { setMouseCapture(false); view_ = View::Settings; return true; }
    if (k == h.togglePerf) { showPerf_ = !showPerf_; return true; }
    if (k == h.fullscreen) {
        fullscreen_ = !fullscreen_;
        SDL_SetWindowFullscreen(win_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        return true;
    }
    return false;
}

bool App::screenToVideo(float sx, float sy, float& nx, float& ny) const {
    if (!videoVisible_ || videoRect_.w <= 0) return false;
    float u = (sx - videoRect_.x) / videoRect_.w, v = (sy - videoRect_.y) / videoRect_.h;
    if (u < 0 || u > 1 || v < 0 || v > 1) return false;
    switch (cfg_.video.rotation) {
    case 90: nx = v; ny = 1 - u; break;
    case 180: nx = 1 - u; ny = 1 - v; break;
    case 270: nx = 1 - v; ny = u; break;
    default: nx = u; ny = v;
    }
    return true;
}

void App::handleEvent(const SDL_Event& e) {
    bool captured = mapper_->mouseCaptured();
    if (!captured) ImGui_ImplSDL2_ProcessEvent(&e);
    ImGuiIO& io = ImGui::GetIO();
    bool streaming = session_->state() == SessionState::Streaming;
    bool play = view_ == View::Play && streaming;

    switch (e.type) {
    case SDL_QUIT: running_ = false; break;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            // evita teclas/toques "presos" quando o usuário troca de janela
            setMouseCapture(false);
            mapper_->releaseAll();
        } else if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
            float ddpi = 96;
            int idx = SDL_GetWindowDisplayIndex(win_);
            if (SDL_GetDisplayDPI(idx, &ddpi, nullptr, nullptr) == 0 && ddpi > 0) dpiScale_ = std::clamp(ddpi / 96.f, 1.f, 3.f);
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        bool down = e.type == SDL_KEYDOWN;
        if (capturing_) {
            if (down) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) capturing_ = nullptr;
                else if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) { capturing_->clear(); capturing_ = nullptr; }
                else { *capturing_ = SDL_GetScancodeName(e.key.keysym.scancode); capturing_ = nullptr; saveSettings(); }
            }
            break;
        }
        if (e.key.repeat == 0 && handleHotkey(e.key.keysym, down)) break;
        if (play && !io.WantTextInput) {
            uint16_t m = e.key.keysym.mod;
            mapper_->onKey(SDL_GetScancodeName(e.key.keysym.scancode), down, m & KMOD_CTRL, m & KMOD_SHIFT,
                           m & KMOD_ALT, eventUs(e.key.timestamp));
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = e.type == SDL_MOUSEBUTTONDOWN;
        if (capturing_ && captureAllowMouse_ && down && e.button.button != SDL_BUTTON_LEFT) {
            *capturing_ = "Mouse" + std::to_string(e.button.button);
            capturing_ = nullptr;
            break;
        }
        if (!play) break;
        if (captured) { mapper_->onMouseButton(e.button.button, down, eventUs(e.button.timestamp)); break; }
        if (e.button.button == SDL_BUTTON_LEFT) {
            float nx, ny;
            if (down && !io.WantCaptureMouse && screenToVideo((float)e.button.x, (float)e.button.y, nx, ny)) {
                session_->sendTouch(AMOTION_DOWN, kMousePointer, (int)(nx * session_->videoWidth()),
                                    (int)(ny * session_->videoHeight()));
                leftTouchDown_ = true;
            } else if (!down && leftTouchDown_) {
                screenToVideo((float)e.button.x, (float)e.button.y, nx, ny);
                session_->sendTouch(AMOTION_UP, kMousePointer, (int)(nx * session_->videoWidth()),
                                    (int)(ny * session_->videoHeight()));
                leftTouchDown_ = false;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT && down && !io.WantCaptureMouse) {
            // botão direito sem captura = voltar (atalho clássico de espelhamento)
            session_->sendKey(0, 4);
            session_->sendKey(1, 4);
        }
        break;
    }
    case SDL_MOUSEMOTION:
        if (!play) break;
        if (captured) mapper_->onMouseMotion((float)e.motion.xrel, (float)e.motion.yrel, eventUs(e.motion.timestamp));
        else if (leftTouchDown_) {
            float nx, ny;
            if (screenToVideo((float)e.motion.x, (float)e.motion.y, nx, ny))
                session_->sendTouch(AMOTION_MOVE, kMousePointer, (int)(nx * session_->videoWidth()),
                                    (int)(ny * session_->videoHeight()));
        }
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- render
void App::renderVideo(bool newFrame) {
    if (!newFrame) return;
    AVFrame* f = session_->frames().pop();
    metrics_.pending = session_->frames().size();
    if (!f) return;
    int64_t t0 = nowUs();
    metrics_.addStage(ST_QUEUE, t0 - f->best_effort_timestamp);
    int fmt = f->format == AV_PIX_FMT_NV12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV;
    if (!tex_ || texW_ != f->width || texH_ != f->height || texFmt_ != fmt) {
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(ren_, fmt, SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
        texW_ = f->width; texH_ = f->height; texFmt_ = fmt;
        LOGI("textura %dx%d %s", texW_, texH_, fmt == SDL_PIXELFORMAT_NV12 ? "NV12" : "YUV420P");
    }
    if (!tex_) return;
    // Upload direto dos planos YUV (sem conversão na CPU); a conversão para RGB acontece no shader.
    if (fmt == SDL_PIXELFORMAT_NV12)
        SDL_UpdateNVTexture(tex_, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1]);
    else
        SDL_UpdateYUVTexture(tex_, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2],
                             f->linesize[2]);
    int64_t t1 = nowUs();
    metrics_.addStage(ST_UPLOAD, t1 - t0);
    metrics_.framesRendered++;
    if (lastPresentUs_) metrics_.addFrameInterval(t1 - lastPresentUs_);
    lastPresentUs_ = t1;
}

// Ferramenta de desenvolvimento/QA: MOB_SHOT=arquivo.bmp [MOB_SHOT_VIEW=0..6] salva a janela e encerra.
void App::devScreenshot() {
    static const char* path = SDL_getenv("MOB_SHOT");
    if (!path) return;
    static bool viewSet = false;
    if (!viewSet) {
        viewSet = true;
        if (const char* v = SDL_getenv("MOB_SHOT_VIEW")) view_ = (View)SDL_atoi(v);
    }
    if (ImGui::GetTime() < splashUntil_ + 1.5) return;
    int w, h;
    SDL_GetRendererOutputSize(ren_, &w, &h);
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (sf && SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) SDL_SaveBMP(sf, path);
    if (sf) SDL_FreeSurface(sf);
    running_ = false;
}

void App::rebuildFontsIfNeeded() {
    float scale = dpiScale_ * cfg_.ui.uiScale;
    if (std::fabs(scale - fontScaleBuilt_) < 0.01f && !themeDirty_) return;
    if (std::fabs(scale - fontScaleBuilt_) >= 0.01f) {
        ImGui_ImplSDLRenderer2_DestroyFontsTexture();
        buildFonts(scale);
        fontScaleBuilt_ = scale;
    }
    applyTheme(cfg_.ui.darkTheme, scale);
    themeDirty_ = false;
}

void App::frame() {
    rebuildFontsIfNeeded();
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(fonts().body);

    const Palette& P = pal();
    SDL_SetRenderDrawColor(ren_, (Uint8)(P.bg.x * 255), (Uint8)(P.bg.y * 255), (Uint8)(P.bg.z * 255), 255);
    SDL_RenderClear(ren_);
    videoVisible_ = false;

    if (ImGui::GetTime() < splashUntil_) {
        drawSplash();
    } else {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        bool play = view_ == View::Play;
        float sideW = play ? 0 : 230 * fontScaleBuilt_;
        if (!play) drawSidebar();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + sideW, vp->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x - sideW, vp->Size.y));
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (play) fl |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, play ? ImVec2(0, 0) : ImVec2(28 * fontScaleBuilt_, 22 * fontScaleBuilt_));
        ImGui::Begin("##main", nullptr, fl);
        ImGui::PopStyleVar();
        switch (view_) {
        case View::Home: viewHome(); break;
        case View::Play: viewPlay(); break;
        case View::Editor: viewEditor(); break;
        case View::Performance: viewPerformance(); break;
        case View::Benchmark: viewBenchmark(); break;
        case View::Settings: viewSettings(); break;
        case View::Diagnostics: viewDiagnostics(); break;
        }
        ImGui::End();
        if (play) drawTopbar();
        if (play && showPerf_) drawPerfOverlay();
    }

    ImGui::PopFont();
    ImGui::Render();
    // Vídeo desenhado direto pelo renderer (quad texturizado na GPU), depois a UI por cima.
    if (videoVisible_ && tex_) {
        int rot = cfg_.video.rotation;
        SDL_Rect dst = videoRect_;
        if (rot == 90 || rot == 270) {
            dst = {videoRect_.x + (videoRect_.w - videoRect_.h) / 2, videoRect_.y + (videoRect_.h - videoRect_.w) / 2,
                   videoRect_.h, videoRect_.w};
        }
        SDL_RenderCopyEx(ren_, tex_, nullptr, &dst, rot, nullptr, SDL_FLIP_NONE);
    }
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren_);
    devScreenshot();
    int64_t t0 = nowUs();
    SDL_RenderPresent(ren_);
    if (videoVisible_) metrics_.addStage(ST_PRESENT, nowUs() - t0);
}
}
