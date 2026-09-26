#pragma once
#include <SDL.h>
#include <imgui.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../bench/Benchmark.h"
#include "../bench/LatencyTester.h"
#include "../core/Config.h"
#include "../core/Platform.h"
#include "../device/Adb.h"
#include "../input/InputMapper.h"
#include "../input/Keymap.h"
#include "../stats/Metrics.h"
#include "../stream/Session.h"

namespace mob {

#define MOB_APP_NAME "MOBILADOR"
#define MOB_APP_SUBTITLE "Android Gaming Bridge"
#ifndef MOB_VERSION
#define MOB_VERSION "1.0.0"
#endif

enum class View { Home, Play, Editor, Performance, Benchmark, Settings, Diagnostics };

class App {
public:
    App();
    ~App();
    int run();

private:
    // ciclo
    bool init();
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void frame();
    void renderVideo(bool newFrame);
    void updateLogic();
    void rebuildFontsIfNeeded();
    void applyVsync();
    void devScreenshot();

    // conexão
    void connect();
    void disconnect();
    void deviceWatcher();
    void refreshDeviceInfo(const std::string& serial);
    void restartWith(const VideoSettings& vs);

    // input
    bool handleHotkey(const SDL_Keysym& ks, bool down);
    void setMouseCapture(bool on);
    bool screenToVideo(float sx, float sy, float& nx, float& ny) const;
    int64_t eventUs(uint32_t sdlTimestampMs) const;
    bool mouseCapturedSafe() const;

    // views
    void drawSidebar();
    void drawTopbar();
    void drawSplash();
    void viewHome();
    void viewPlay();
    void viewEditor();
    void viewPerformance();
    void viewBenchmark();
    void viewSettings();
    void viewDiagnostics();
    void drawOverlayControls(ImDrawList* dl, ImVec2 origin, ImVec2 size, bool editing);
    void drawPerfOverlay();
    void drawLatencyAnalyzer(bool compact);
    bool keyCaptureButton(const char* id, std::string& target, bool allowMouse = true);
    void saveSettings();
    Profile* activeProfile();

    // SDL
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    int texW_ = 0, texH_ = 0, texFmt_ = -1;
    uint32_t frameEvent_ = 0;
    std::atomic<bool> framePending_{false};
    std::string rendererName_;

    // estado
    bool running_ = true;
    View view_ = View::Home;
    Settings cfg_;
    std::string cfgPath_;
    float dpiScale_ = 1.0f, fontScaleBuilt_ = 0;
    bool themeDirty_ = true;
    bool fullscreen_ = false;
    bool showControls_ = true;
    bool showPerf_ = true;
    bool mappingEnabled_ = true;
    double splashUntil_ = 0;
    int64_t lastTick_ = 0, lastPresentUs_ = 0, lastTempUs_ = 0;
    int64_t tickOffsetUs_ = 0;
    SDL_Rect videoRect_{0, 0, 0, 0};  // área do vídeo na janela (pixels)
    bool videoVisible_ = false;
    bool leftTouchDown_ = false;
    bool vsyncApplied_ = false;

    // captura de tecla para editor/hotkeys
    std::string* capturing_ = nullptr;
    bool captureAllowMouse_ = true;

    // subsistemas
    Adb adb_;
    std::atomic<bool> adbReady_{false};
    std::string adbVersion_;
    Metrics metrics_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<InputMapper> mapper_;
    std::unique_ptr<ProfileStore> profiles_;
    HostMonitor host_;
    HostUsage hostUsage_;
    MetricsSnapshot snap_;
    LatencyTester tester_;
    Benchmark bench_;
    double lastE2E_ = 0;
    float probeX_ = 0.5f, probeY_ = 0.5f;

    // dispositivos (thread de monitoramento)
    std::thread watcher_, infoThread_;
    std::atomic<bool> watcherStop_{false};
    std::mutex devMx_;
    std::vector<AdbDevice> devices_;
    DeviceInfo info_;
    std::atomic<bool> infoBusy_{false};
    std::string targetSerial_, infoTried_;
    bool wantConnected_ = false;
    int64_t reconnectAt_ = 0;
    int reconnectTries_ = 0;

    // editor
    int selElem_ = -1;
    char newProfileName_[64] = "";
    int benchSel_ = 0;
};
}
