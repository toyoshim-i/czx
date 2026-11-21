#pragma once

#include "czx/core.h"

#include <cstdint>

namespace czx::input {

struct RawKeyEvent {
    czx_key_t key = CZX_KEY_NONE;
    bool pressed = false;
    bool repeat = false;
    uint32_t codepoint = 0;  // 0 = compute from key + modifiers if printable
};

}  // namespace czx::input
