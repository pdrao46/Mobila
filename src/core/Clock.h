#pragma once
#include <chrono>
#include <cstdint>

namespace mob {
using Clock = std::chrono::steady_clock;
inline int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}
inline double usToMs(int64_t us) { return us / 1000.0; }
}
