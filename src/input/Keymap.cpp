#include "Keymap.h"
#include "../core/Log.h"
#include "../core/Platform.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>

using nlohmann::json;
namespace fs = std::filesystem;
namespace mob {

const char* elemTypeName(ElemType t) {
    switch (t) {
    case ElemType::Button: return "Botão";
    case ElemType::Joystick: return "Joystick";
    case ElemType::Tap: return "Toque";
    case ElemType::Camera: return "Câmera";
    case ElemType::Aim: return "Mira";
    case ElemType::Swipe: return "Swipe";
    case ElemType::MoveArea: return "Área de movimento";
    }
    return "?";
}

std::string Binding::toString() const {
    if (key.empty()) return "";
    std::string s;
    if (ctrl) s += "Ctrl+";
    if (shift) s += "Shift+";
    if (alt) s += "Alt+";
    return s + key;
}
Binding Binding::parse(const std::string& in) {
    Binding b;
    std::string s = in;
    for (;;) {
        if (s.rfind("Ctrl+", 0) == 0) { b.ctrl = true; s = s.substr(5); }
        else if (s.rfind("Shift+", 0) == 0) { b.shift = true; s = s.substr(6); }
        else if (s.rfind("Alt+", 0) == 0) { b.alt = true; s = s.substr(4); }
        else break;
    }
    b.key = s;
    return b;
}

static json toJson(const Element& e) {
    return {{"type", (int)e.type}, {"label", e.label}, {"x", e.x}, {"y", e.y}, {"r", e.r}, {"w", e.w},
            {"h", e.h}, {"key", e.key.toString()}, {"up", e.up.toString()}, {"down", e.down.toString()},
            {"left", e.left.toString()}, {"right", e.right.toString()}, {"slow", e.modifierSlow.toString()},
            {"x2", e.x2}, {"y2", e.y2}, {"swipeMs", e.swipeMs}, {"holdMode", e.holdMode},
            {"sensitivity", e.sensitivity}, {"multX", e.multX}, {"multY", e.multY}, {"deadzone", e.deadzone},
            {"accel", e.accel}, {"invertX", e.invertX}, {"invertY", e.invertY}};
}
static Element fromJson(const json& j) {
    Element e;
    e.type = (ElemType)j.value("type", 0);
    e.label = j.value("label", "");
    e.x = j.value("x", e.x); e.y = j.value("y", e.y); e.r = j.value("r", e.r);
    e.w = j.value("w", e.w); e.h = j.value("h", e.h);
    e.key = Binding::parse(j.value("key", ""));
    e.up = Binding::parse(j.value("up", "")); e.down = Binding::parse(j.value("down", ""));
    e.left = Binding::parse(j.value("left", "")); e.right = Binding::parse(j.value("right", ""));
    e.modifierSlow = Binding::parse(j.value("slow", ""));
    e.x2 = j.value("x2", e.x2); e.y2 = j.value("y2", e.y2); e.swipeMs = j.value("swipeMs", e.swipeMs);
    e.holdMode = j.value("holdMode", e.holdMode);
    e.sensitivity = j.value("sensitivity", e.sensitivity);
    e.multX = j.value("multX", e.multX); e.multY = j.value("multY", e.multY);
    e.deadzone = j.value("deadzone", e.deadzone); e.accel = j.value("accel", e.accel);
    e.invertX = j.value("invertX", e.invertX); e.invertY = j.value("invertY", e.invertY);
    return e;
}

std::string ProfileStore::fileFor(const std::string& name) const {
    std::string safe;
    for (char c : name) safe += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return (fs::path(dir_) / (safe + ".mobmap.json")).string();
}

bool ProfileStore::exportTo(const Profile& p, const std::string& path) {
    json j{{"name", p.name}, {"game", p.game}, {"globalSensitivity", p.globalSensitivity},
           {"format", "mobilador-keymap-1"}};
    j["elements"] = json::array();
    for (auto& e : p.elements) j["elements"].push_back(toJson(e));
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return true;
}
bool ProfileStore::save(const Profile& p) { return exportTo(p, fileFor(p.name)); }

void ProfileStore::loadAll() {
    ensureDir(dir_);
    profiles_.clear();
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(dir_, ec)) {
        auto fn = ent.path().string();
        if (fn.size() < 12 || fn.substr(fn.size() - 12) != ".mobmap.json") continue;
        try {
            std::ifstream f(ent.path());
            json j = json::parse(f);
            Profile p;
            p.name = j.value("name", ent.path().stem().string());
            p.game = j.value("game", "");
            p.globalSensitivity = j.value("globalSensitivity", 1.0f);
            for (auto& e : j.value("elements", json::array())) p.elements.push_back(fromJson(e));
            profiles_.push_back(std::move(p));
        } catch (const std::exception& e) {
            LOGW("perfil inválido %s: %s", fn.c_str(), e.what());
        }
    }
    if (profiles_.empty()) {
        profiles_.push_back(makeDefaultFreeFire("Free Fire"));
        profiles_.push_back(makeDefaultFreeFire("Free Fire BR"));
        profiles_.push_back(makeTrainingProfile());
        Profile c;
        c.name = "Personalizado";
        profiles_.push_back(c);
        for (auto& p : profiles_) save(p);
    }
    std::sort(profiles_.begin(), profiles_.end(), [](auto& a, auto& b) { return a.name < b.name; });
}
Profile* ProfileStore::find(const std::string& name) {
    for (auto& p : profiles_) if (p.name == name) return &p;
    return nullptr;
}
bool ProfileStore::remove(const std::string& name) {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](auto& p) { return p.name == name; });
    if (it == profiles_.end()) return false;
    std::error_code ec;
    fs::remove(fileFor(name), ec);
    profiles_.erase(it);
    return true;
}
Profile& ProfileStore::duplicate(const std::string& name, const std::string& newName) {
    Profile copy = find(name) ? *find(name) : Profile{};
    copy.name = newName;
    profiles_.push_back(copy);
    save(copy);
    return profiles_.back();
}
Profile& ProfileStore::create(const std::string& name) {
    Profile p;
    p.name = name;
    profiles_.push_back(p);
    save(p);
    return profiles_.back();
}

static Element btn(const char* label, const char* key, float x, float y, float r = 0.035f) {
    Element e;
    e.type = ElemType::Button;
    e.label = label;
    e.key = Binding::parse(key);
    e.x = x; e.y = y; e.r = r;
    return e;
}

// Layout de referência para jogos Battle Royale em paisagem. Ajuste as posições no editor
// sobre a tela real do jogo — cada HUD pode ser personalizado dentro do próprio jogo.
Profile makeDefaultFreeFire(const std::string& name) {
    Profile p;
    p.name = name;
    p.game = "Free Fire";
    Element joy;
    joy.type = ElemType::Joystick; joy.label = "Movimento";
    joy.x = 0.16f; joy.y = 0.70f; joy.r = 0.075f;
    joy.up = Binding::parse("W"); joy.down = Binding::parse("S");
    joy.left = Binding::parse("A"); joy.right = Binding::parse("D");
    joy.modifierSlow = Binding::parse("Left Shift");
    p.elements.push_back(joy);
    Element cam;
    cam.type = ElemType::Camera; cam.label = "Câmera";
    cam.x = 0.62f; cam.y = 0.45f; cam.w = 0.45f; cam.h = 0.55f; cam.sensitivity = 1.0f;
    p.elements.push_back(cam);
    p.elements.push_back(btn("Disparo", "Mouse1", 0.86f, 0.62f, 0.045f));
    Element aim = btn("Mira", "Mouse3", 0.93f, 0.45f);
    aim.type = ElemType::Aim; aim.holdMode = false;
    p.elements.push_back(aim);
    p.elements.push_back(btn("Pular", "Space", 0.92f, 0.78f));
    p.elements.push_back(btn("Agachar", "C", 0.82f, 0.88f));
    p.elements.push_back(btn("Deitar", "Z", 0.74f, 0.90f));
    p.elements.push_back(btn("Recarregar", "R", 0.73f, 0.72f, 0.03f));
    p.elements.push_back(btn("Interagir", "F", 0.60f, 0.58f, 0.03f));
    p.elements.push_back(btn("Arma 1", "1", 0.44f, 0.90f, 0.03f));
    p.elements.push_back(btn("Arma 2", "2", 0.52f, 0.90f, 0.03f));
    p.elements.push_back(btn("Gel/Cura", "G", 0.66f, 0.90f, 0.03f));
    p.elements.push_back(btn("Mapa", "M", 0.93f, 0.08f, 0.03f));
    p.elements.push_back(btn("Mochila", "Tab", 0.70f, 0.08f, 0.03f));
    return p;
}
Profile makeTrainingProfile() {
    Profile p = makeDefaultFreeFire("Treino");
    p.game = "Treino";
    for (auto& e : p.elements)
        if (e.type == ElemType::Camera) e.sensitivity = 0.8f;
    return p;
}
}
