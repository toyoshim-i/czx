#pragma once

#include "czx/core.h"

#include <cstdint>

namespace czx::input {

struct RawKeyEvent {
    czx_key_t key = CZX_KEY_NONE;
    bool pressed = false;
    bool repeat = false;
    uint32_t codepoint = 0;  // 0 = compute from key + modifiers if printable
    uint32_t native_key = 0; // 0 = map from key/codepoint, non-zero = direct core physical keycode
};

}  // namespace czx::input
