#pragma once
#include <cstdint>
#include <cstddef>

namespace mob {
// Serialização binária do protocolo de controle do scrcpy-server 3.x (big-endian).
enum : uint8_t { CM_INJECT_KEYCODE = 0, CM_INJECT_TOUCH = 2, CM_BACK_OR_SCREEN_ON = 4, CM_SET_DISPLAY_POWER = 10,
                 CM_RESET_VIDEO = 17 };
enum : uint8_t { AMOTION_DOWN = 0, AMOTION_UP = 1, AMOTION_MOVE = 2 };

inline void be16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
inline void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
inline void be64(uint8_t* p, uint64_t v) { be32(p, (uint32_t)(v >> 32)); be32(p + 4, (uint32_t)v); }
inline uint32_t rd32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline uint64_t rd64(const uint8_t* p) { return (uint64_t(rd32(p)) << 32) | rd32(p + 4); }

constexpr size_t kTouchMsgSize = 32;
inline void buildTouch(uint8_t* b, uint8_t action, uint64_t pointerId, int32_t x, int32_t y, uint16_t w,
                       uint16_t h, bool pressed) {
    b[0] = CM_INJECT_TOUCH;
    b[1] = action;
    be64(b + 2, pointerId);
    be32(b + 10, (uint32_t)x);
    be32(b + 14, (uint32_t)y);
    be16(b + 18, w);
    be16(b + 20, h);
    be16(b + 22, pressed ? 0xffff : 0);  // pressão u16 ponto fixo
    be32(b + 24, 0);                     // action button
    be32(b + 28, 0);                     // buttons
}
constexpr size_t kKeyMsgSize = 14;
inline void buildKey(uint8_t* b, uint8_t action, uint32_t keycode, uint32_t repeat, uint32_t meta) {
    b[0] = CM_INJECT_KEYCODE;
    b[1] = action;
    be32(b + 2, keycode);
    be32(b + 6, repeat);
    be32(b + 10, meta);
}
}
