#include "Config.h"
#include "Log.h"
#include <nlohmann/json.hpp>
#include <fstream>

using nlohmann::json;
namespace mob {

void applyVideoPreset(VideoSettings& v, VideoMode m) {
    v.mode = m;
    switch (m) {
    case VideoMode::UltraLowLatency:
        // Resolução menor = encode/decode/transferência mais rápidos por frame.
        v.maxSize = 1280; v.bitrateMbps = 8; v.maxFps = 0; v.vsync = false; v.frameQueue = 1;
        v.codec = Codec::H264;  // H.264: encoder mais rápido/estável na maioria dos SoCs
        break;
    case VideoMode::Balanced:
        v.maxSize = 1600; v.bitrateMbps = 12; v.maxFps = 0; v.vsync = false; v.frameQueue = 1;
        v.codec = Codec::H264;
        break;
    case VideoMode::Quality:
        v.maxSize = 0; v.bitrateMbps = 20; v.maxFps = 0; v.vsync = true; v.frameQueue = 2;
        break;
    case VideoMode::Custom: break;
    }
}
const char* videoModeName(VideoMode m) {
    switch (m) {
    case VideoMode::UltraLowLatency: return "ULTRA LOW LATENCY";
    case VideoMode::Balanced: return "BALANCED";
    case VideoMode::Quality: return "QUALITY";
    default: return "CUSTOM";
    }
}
const char* codecName(Codec c) {
    switch (c) { case Codec::H265: return "h265"; case Codec::AV1: return "av1"; default: return "h264"; }
}

Settings Settings::load(const std::string& path) {
    Settings s;
    std::ifstream f(path);
    if (!f) return s;
    try {
        json j = json::parse(f);
        auto& v = s.video;
        auto jv = j.value("video", json::object());
        v.mode = (VideoMode)jv.value("mode", (int)v.mode);
        v.maxSize = jv.value("maxSize", v.maxSize);
        v.maxFps = jv.value("maxFps", v.maxFps);
        v.bitrateMbps = jv.value("bitrateMbps", v.bitrateMbps);
        v.codec = (Codec)jv.value("codec", (int)v.codec);
        v.decoder = (DecoderPref)jv.value("decoder", (int)v.decoder);
        v.vsync = jv.value("vsync", v.vsync);
        v.rotation = jv.value("rotation", v.rotation);
        v.renderScale = jv.value("renderScale", v.renderScale);
        v.integerScaling = jv.value("integerScaling", v.integerScaling);
        v.frameQueue = jv.value("frameQueue", v.frameQueue);
        v.encoderName = jv.value("encoderName", v.encoderName);
        auto jh = j.value("hotkeys", json::object());
        auto& h = s.hotkeys;
        h.showControls = jh.value("showControls", h.showControls);
        h.hideControls = jh.value("hideControls", h.hideControls);
        h.toggleMapping = jh.value("toggleMapping", h.toggleMapping);
        h.releaseMouse = jh.value("releaseMouse", h.releaseMouse);
        h.openSettings = jh.value("openSettings", h.openSettings);
        h.togglePerf = jh.value("togglePerf", h.togglePerf);
        h.fullscreen = jh.value("fullscreen", h.fullscreen);
        h.captureMouse = jh.value("captureMouse", h.captureMouse);
        auto ju = j.value("ui", json::object());
        s.ui.darkTheme = ju.value("darkTheme", s.ui.darkTheme);
        s.ui.uiScale = ju.value("uiScale", s.ui.uiScale);
        s.ui.showPerfOverlay = ju.value("showPerfOverlay", s.ui.showPerfOverlay);
        s.ui.showSplash = ju.value("showSplash", s.ui.showSplash);
        s.activeProfile = j.value("activeProfile", s.activeProfile);
        s.autoReconnect = j.value("autoReconnect", s.autoReconnect);
        s.stayAwake = j.value("stayAwake", s.stayAwake);
        s.showTouchesForTest = j.value("showTouchesForTest", s.showTouchesForTest);
        s.adbPath = j.value("adbPath", s.adbPath);
    } catch (const std::exception& e) {
        LOGW("config inválida (%s), usando padrões", e.what());
    }
    return s;
}

bool Settings::save(const std::string& path) const {
    json j;
    auto& v = video;
    j["configVersion"] = configVersion;
    j["video"] = {{"mode", (int)v.mode}, {"maxSize", v.maxSize}, {"maxFps", v.maxFps},
                  {"bitrateMbps", v.bitrateMbps}, {"codec", (int)v.codec}, {"decoder", (int)v.decoder},
                  {"vsync", v.vsync}, {"rotation", v.rotation}, {"renderScale", v.renderScale},
                  {"integerScaling", v.integerScaling}, {"frameQueue", v.frameQueue},
                  {"encoderName", v.encoderName}};
    auto& h = hotkeys;
    j["hotkeys"] = {{"showControls", h.showControls}, {"hideControls", h.hideControls},
                    {"toggleMapping", h.toggleMapping}, {"releaseMouse", h.releaseMouse},
                    {"openSettings", h.openSettings}, {"togglePerf", h.togglePerf},
                    {"fullscreen", h.fullscreen}, {"captureMouse", h.captureMouse}};
    j["ui"] = {{"darkTheme", ui.darkTheme}, {"uiScale", ui.uiScale},
               {"showPerfOverlay", ui.showPerfOverlay}, {"showSplash", ui.showSplash}};
    j["activeProfile"] = activeProfile;
    j["autoReconnect"] = autoReconnect;
    j["stayAwake"] = stayAwake;
    j["showTouchesForTest"] = showTouchesForTest;
    j["adbPath"] = adbPath;
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return false;
        f << j.dump(2);
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;  // escrita atômica
}
}
