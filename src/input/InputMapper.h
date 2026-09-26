#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "Keymap.h"

namespace mob {
class Session;
class Metrics;

// Converte teclado/mouse em toques multi-touch. Processamento imediato na thread de eventos:
// cada tecla vira no máximo uma mensagem de 32 bytes enviada na hora. O movimento do mouse é
// o único evento agregado: todos os deltas de um lote de eventos viram UM "MOVE" (evita
// inundar o Android com eventos que ele iria fundir de qualquer forma e acumular fila).
class InputMapper {
public:
    InputMapper(Session& s, Metrics& m) : s_(s), m_(m) {}
    void setProfile(const Profile* p);
    void setEnabled(bool e);
    bool enabled() const { return enabled_; }
    void setMouseCaptured(bool c);
    bool mouseCaptured() const { return captured_; }

    // Retornam true se o evento foi consumido por um controle mapeado.
    bool onKey(const std::string& keyName, bool down, bool ctrl, bool shift, bool alt, int64_t evtUs);
    bool onMouseButton(int button, bool down, int64_t evtUs);
    void onMouseMotion(float dx, float dy, int64_t evtUs);
    void flush();               // fim do lote de eventos: envia movimento agregado
    void update(int64_t nowUs); // toques/swipes temporizados
    void releaseAll();

    // Estado para desenhar o overlay.
    bool isElementActive(size_t i) const;

private:
    struct ElemState { bool down = false; float cx = 0, cy = 0; int64_t tapUpAt = 0; int64_t swipeStart = 0; bool swiping = false; };
    bool match(const Binding& b, const std::string& key, bool ctrl, bool shift, bool alt) const;
    void touch(uint8_t action, uint64_t id, float nx, float ny, int64_t evtUs);
    void updateJoystick(size_t i, int64_t evtUs);
    uint64_t pid(size_t i) const { return 10 + i; }

    Session& s_;
    Metrics& m_;
    const Profile* p_ = nullptr;
    bool enabled_ = true, captured_ = false;
    std::unordered_map<std::string, bool> keys_;
    std::unordered_map<size_t, ElemState> st_;
    // câmera
    int camIdx_ = -1;
    bool camDown_ = false;
    float camX_ = 0, camY_ = 0, accX_ = 0, accY_ = 0;
    int64_t camEvtUs_ = 0, lastMotionUs_ = 0;
    int moveAreaIdx_ = -1;
};
}
