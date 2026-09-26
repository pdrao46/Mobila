#pragma once
#include <string>
#include <vector>

namespace mob {

// Tipos de controle posicionados sobre a tela do celular (coordenadas normalizadas 0..1).
enum class ElemType { Button = 0, Joystick, Tap, Camera, Aim, Swipe, MoveArea };
const char* elemTypeName(ElemType t);

// Um "binding" é uma tecla SDL (nome) ou botão do mouse ("Mouse1".."Mouse5"),
// com modificadores opcionais para combinações (ex.: "Shift+E").
struct Binding {
    std::string key;  // nome SDL, ex. "W", "Space", "Mouse1"
    bool ctrl = false, shift = false, alt = false;
    std::string toString() const;
    static Binding parse(const std::string& s);
    bool empty() const { return key.empty(); }
};

struct Element {
    ElemType type = ElemType::Button;
    std::string label;
    float x = 0.5f, y = 0.5f;   // centro
    float r = 0.04f;            // raio (fração da largura)
    float w = 0.3f, h = 0.4f;   // áreas (Camera/MoveArea)
    Binding key;                // Button/Tap/Aim/Swipe
    Binding up, down, left, right;  // Joystick
    Binding modifierSlow;       // Joystick: caminhar (raio reduzido)
    float x2 = 0.6f, y2 = 0.5f; // Swipe: destino
    int swipeMs = 120;
    bool holdMode = true;       // Aim: segurar vs alternar
    // Camera
    float sensitivity = 1.0f, multX = 1.0f, multY = 1.0f, deadzone = 0.0f, accel = 0.0f;
    bool invertX = false, invertY = false;
};

struct Profile {
    std::string name;
    std::string game;
    std::vector<Element> elements;
    float globalSensitivity = 1.0f;
};

class ProfileStore {
public:
    explicit ProfileStore(std::string dir) : dir_(std::move(dir)) {}
    void loadAll();           // cria perfis padrão na primeira execução
    bool save(const Profile& p);
    bool remove(const std::string& name);
    Profile* find(const std::string& name);
    Profile& duplicate(const std::string& name, const std::string& newName);
    Profile& create(const std::string& name);
    std::vector<Profile>& all() { return profiles_; }
    const std::string& dir() const { return dir_; }
    bool exportTo(const Profile& p, const std::string& path);
private:
    std::string fileFor(const std::string& name) const;
    std::string dir_;
    std::vector<Profile> profiles_;
};

Profile makeDefaultFreeFire(const std::string& name);
Profile makeTrainingProfile();
}
