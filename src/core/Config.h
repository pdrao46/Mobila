#pragma once
#include <string>
#include <map>

namespace mob {

enum class VideoMode { UltraLowLatency = 0, Balanced = 1, Quality = 2, Custom = 3 };
enum class Codec { H264 = 0, H265 = 1, AV1 = 2 };
enum class DecoderPref { Auto = 0, Hardware = 1, Software = 2 };

struct VideoSettings {
    VideoMode mode = VideoMode::UltraLowLatency;
    int maxSize = 1280;         // maior dimensão enviada pelo celular (0 = nativa)
    int maxFps = 0;             // 0 = sem limite (o encoder entrega o que o celular consegue)
    int bitrateMbps = 8;
    Codec codec = Codec::H264;
    DecoderPref decoder = DecoderPref::Auto;
    bool vsync = false;         // ULL: sem VSync (menor latência; tearing possível)
    int rotation = 0;           // 0/90/180/270 aplicado na GPU no PC
    float renderScale = 1.0f;   // escala da janela de vídeo
    bool integerScaling = false;
    int frameQueue = 1;         // frames pendentes permitidos antes do descarte (1 = só o mais recente)
    std::string encoderName;    // vazio = padrão do dispositivo
};

struct HotkeySettings {
    // Nomes SDL (SDL_GetKeyName)
    std::string showControls = "F1";
    std::string hideControls = "F2";
    std::string toggleMapping = "F3";
    std::string releaseMouse = "F4";
    std::string openSettings = "F5";
    std::string togglePerf = "F6";
    std::string fullscreen = "F11";
    std::string captureMouse = "`";
};

struct UiSettings {
    bool darkTheme = true;
    float uiScale = 1.0f;
    bool showPerfOverlay = true;
    bool showSplash = true;
};

struct Settings {
    VideoSettings video;
    HotkeySettings hotkeys;
    UiSettings ui;
    std::string activeProfile = "Free Fire";
    bool autoReconnect = true;
    bool stayAwake = true;
    bool showTouchesForTest = false;
    std::string adbPath;  // vazio = auto
    int configVersion = 1;

    static Settings load(const std::string& path);
    bool save(const std::string& path) const;
};

// Presets dos modos de vídeo. Nenhum preset força FPS acima do que o aparelho entrega.
void applyVideoPreset(VideoSettings& v, VideoMode m);
const char* videoModeName(VideoMode m);
const char* codecName(Codec c);
}
