#include "InputMapper.h"
#include "../core/Clock.h"
#include "../stats/Metrics.h"
#include "../stream/ControlMsg.h"
#include "../stream/Session.h"
#include <cmath>

namespace mob {
static constexpr uint64_t kCamPointer = 5;

void InputMapper::setProfile(const Profile* p) {
    releaseAll();
    p_ = p;
    camIdx_ = moveAreaIdx_ = -1;
    if (!p) return;
    for (size_t i = 0; i < p->elements.size(); ++i)
        if (p->elements[i].type == ElemType::Camera && camIdx_ < 0) camIdx_ = (int)i;
}
void InputMapper::setEnabled(bool e) {
    if (!e) releaseAll();
    enabled_ = e;
}
void InputMapper::setMouseCaptured(bool c) {
    if (!c && camDown_) {
        touch(AMOTION_UP, kCamPointer, camX_, camY_, nowUs());
        camDown_ = false;
    }
    captured_ = c;
}

bool InputMapper::match(const Binding& b, const std::string& key, bool ctrl, bool shift, bool alt) const {
    if (b.key.empty() || b.key != key) return false;
    // modificadores exigidos precisam estar pressionados (combinações); os não exigidos são ignorados
    return (!b.ctrl || ctrl) && (!b.shift || shift) && (!b.alt || alt);
}

void InputMapper::touch(uint8_t action, uint64_t id, float nx, float ny, int64_t evtUs) {
    int w = s_.videoWidth(), h = s_.videoHeight();
    if (!w || !h) return;
    if (s_.sendTouch(action, id, (int)std::lround(nx * w), (int)std::lround(ny * h))) {
        m_.inputEvents++;
        if (evtUs) m_.addStage(ST_INPUT, nowUs() - evtUs);
    }
}

bool InputMapper::isElementActive(size_t i) const {
    auto it = st_.find(i);
    if (it != st_.end() && it->second.down) return true;
    return (int)i == camIdx_ && camDown_;
}

void InputMapper::updateJoystick(size_t i, int64_t evtUs) {
    const Element& e = p_->elements[i];
    auto k = [&](const Binding& b) { auto it = keys_.find(b.key); return it != keys_.end() && it->second; };
    float dx = (k(e.right) ? 1.f : 0.f) - (k(e.left) ? 1.f : 0.f);
    float dy = (k(e.down) ? 1.f : 0.f) - (k(e.up) ? 1.f : 0.f);
    auto& s = st_[i];
    int w = s_.videoWidth(), h = s_.videoHeight();
    if (!w || !h) return;
    if (dx == 0 && dy == 0) {
        if (s.down) { touch(AMOTION_UP, pid(i), s.cx, s.cy, evtUs); s.down = false; }
        return;
    }
    float len = std::sqrt(dx * dx + dy * dy);
    float rad = e.r * (k(e.modifierSlow) ? 0.45f : 1.0f);
    float tx = e.x + dx / len * rad;
    float ty = e.y + dy / len * rad * (float)w / (float)h;  // raio relativo à largura
    if (!s.down) {
        touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs);
        s.down = true;
    }
    s.cx = tx; s.cy = ty;
    touch(AMOTION_MOVE, pid(i), tx, ty, evtUs);
}

bool InputMapper::onKey(const std::string& key, bool down, bool ctrl, bool shift, bool alt, int64_t evtUs) {
    if (!enabled_ || !p_) return false;
    bool prev = keys_[key];
    keys_[key] = down;
    if (prev == down) return true;  // ignora auto-repeat do SO
    bool used = false;
    int64_t now = nowUs();
    for (size_t i = 0; i < p_->elements.size(); ++i) {
        const Element& e = p_->elements[i];
        auto& s = st_[i];
        switch (e.type) {
        case ElemType::Joystick:
            if (e.up.key == key || e.down.key == key || e.left.key == key || e.right.key == key ||
                e.modifierSlow.key == key) {
                updateJoystick(i, evtUs);
                used = true;
            }
            break;
        case ElemType::Button:
            if (match(e.key, key, ctrl, shift, alt) || (!down && e.key.key == key && s.down)) {
                if (down && !s.down) { touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs); s.down = true; }
                else if (!down && s.down) { touch(AMOTION_UP, pid(i), e.x, e.y, evtUs); s.down = false; }
                used = true;
            }
            break;
        case ElemType::Aim:
            if (match(e.key, key, ctrl, shift, alt) || (!down && e.key.key == key)) {
                if (e.holdMode || down) {
                    if (down && !s.down) { touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs); s.down = true; }
                    else if (!down && s.down && e.holdMode) { touch(AMOTION_UP, pid(i), e.x, e.y, evtUs); s.down = false; }
                    if (!e.holdMode && down) s.tapUpAt = now + 40000;  // modo alternar: um toque
                }
                used = true;
            }
            break;
        case ElemType::Tap:
            if (down && match(e.key, key, ctrl, shift, alt) && !s.down) {
                touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs);
                s.down = true;
                s.tapUpAt = now + 40000;
                used = true;
            }
            break;
        case ElemType::Swipe:
            if (down && match(e.key, key, ctrl, shift, alt) && !s.swiping) {
                touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs);
                s.down = s.swiping = true;
                s.swipeStart = now;
                used = true;
            }
            break;
        case ElemType::MoveArea:
            if (match(e.key, key, ctrl, shift, alt) || (!down && e.key.key == key)) {
                if (down && !s.down) {
                    touch(AMOTION_DOWN, pid(i), e.x, e.y, evtUs);
                    s.down = true; s.cx = e.x; s.cy = e.y;
                    moveAreaIdx_ = (int)i;
                } else if (!down && s.down) {
                    touch(AMOTION_UP, pid(i), s.cx, s.cy, evtUs);
                    s.down = false;
                    moveAreaIdx_ = -1;
                }
                used = true;
            }
            break;
        case ElemType::Camera: break;
        }
    }
    return used;
}

bool InputMapper::onMouseButton(int button, bool down, int64_t evtUs) {
    if (!captured_) return false;  // sem captura, o mouse funciona como toque direto (tratado na UI)
    return onKey("Mouse" + std::to_string(button), down, false, false, false, evtUs);
}

void InputMapper::onMouseMotion(float dx, float dy, int64_t evtUs) {
    if (!enabled_ || !captured_ || !p_) return;
    accX_ += dx;
    accY_ += dy;
    if (!camEvtUs_) camEvtUs_ = evtUs;  // latência medida a partir do evento mais antigo do lote
}

void InputMapper::flush() {
    if (!p_ || (accX_ == 0 && accY_ == 0)) return;
    float dx = accX_, dy = accY_;
    accX_ = accY_ = 0;
    int64_t evt = camEvtUs_;
    camEvtUs_ = 0;
    int w = s_.videoWidth(), h = s_.videoHeight();
    if (!w || !h) return;

    if (moveAreaIdx_ >= 0) {
        auto& e = p_->elements[moveAreaIdx_];
        auto& s = st_[moveAreaIdx_];
        s.cx = std::fmin(std::fmax(s.cx + dx / w, e.x - e.w / 2), e.x + e.w / 2);
        s.cy = std::fmin(std::fmax(s.cy + dy / h, e.y - e.h / 2), e.y + e.h / 2);
        touch(AMOTION_MOVE, pid(moveAreaIdx_), s.cx, s.cy, evt);
        return;
    }
    if (camIdx_ < 0) return;
    const Element& c = p_->elements[camIdx_];
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag <= c.deadzone) return;
    // aceleração opcional (0 = linear, recomendado para FPS)
    float gain = c.sensitivity * p_->globalSensitivity * (1.0f + c.accel * std::fmin(mag, 50.f) / 10.f);
    float mx = dx * gain * c.multX * (c.invertX ? -1.f : 1.f);
    float my = dy * gain * c.multY * (c.invertY ? -1.f : 1.f);
    // o dedo virtual se move em pixels do vídeo, convertido para normalizado
    float nx = mx / w, ny = my / h;
    float left = c.x - c.w / 2, right = c.x + c.w / 2, top = c.y - c.h / 2, bottom = c.y + c.h / 2;
    if (!camDown_) {
        camX_ = c.x; camY_ = c.y;
        touch(AMOTION_DOWN, kCamPointer, camX_, camY_, evt);
        camDown_ = true;
    }
    float tx = camX_ + nx, ty = camY_ + ny;
    if (tx < left || tx > right || ty < top || ty > bottom) {
        // borda da área: levanta e recoloca no centro — rotação "infinita", sem limite do cursor
        touch(AMOTION_UP, kCamPointer, camX_, camY_, evt);
        camX_ = c.x; camY_ = c.y;
        touch(AMOTION_DOWN, kCamPointer, camX_, camY_, evt);
        tx = camX_ + nx; ty = camY_ + ny;
    }
    camX_ = tx; camY_ = ty;
    touch(AMOTION_MOVE, kCamPointer, camX_, camY_, evt);
}

void InputMapper::update(int64_t now) {
    if (!p_) return;
    for (auto& [i, s] : st_) {
        if (i >= p_->elements.size()) continue;
        const Element& e = p_->elements[i];
        if (s.tapUpAt && now >= s.tapUpAt) {
            s.tapUpAt = 0;
            if (e.type == ElemType::Aim && !e.holdMode && !s.down) continue;
            touch(AMOTION_UP, pid(i), e.x, e.y, 0);
            s.down = false;
        }
        if (s.swiping) {
            float t = std::fmin(1.f, (now - s.swipeStart) / (e.swipeMs * 1000.f));
            float x = e.x + (e.x2 - e.x) * t, y = e.y + (e.y2 - e.y) * t;
            touch(AMOTION_MOVE, pid(i), x, y, 0);
            if (t >= 1.f) { touch(AMOTION_UP, pid(i), x, y, 0); s.swiping = s.down = false; }
        }
    }
}

void InputMapper::releaseAll() {
    if (p_) {
        for (auto& [i, s] : st_)
            if (s.down && i < p_->elements.size()) {
                const Element& e = p_->elements[i];
                float x = e.type == ElemType::Joystick || e.type == ElemType::MoveArea ? s.cx : e.x;
                float y = e.type == ElemType::Joystick || e.type == ElemType::MoveArea ? s.cy : e.y;
                touch(AMOTION_UP, pid(i), x, y, 0);
            }
    }
    if (camDown_) touch(AMOTION_UP, kCamPointer, camX_, camY_, 0);
    camDown_ = false;
    st_.clear();
    keys_.clear();
    moveAreaIdx_ = -1;
    accX_ = accY_ = 0;
}
}
