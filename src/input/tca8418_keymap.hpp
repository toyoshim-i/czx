#pragma once

#include "czx/core.h"
#include "input/keycodes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace czx::input {

class Tca8418Keymap {
public:
    Tca8418Keymap();

    // Optionally loads overrides from /usr/share/keymaps/tca8418_keypad_m5stack_keymap.map.
    bool load_from_file(const std::string& path);

    // Converts a Linux evdev key code to RawKeyEvent.
    // Set `is_tca8418_device` true for Cardputer Zero's built-in I2C keypad so SYM-layer
    // custom scancodes (26..94) resolve to ASCII symbols.
    RawKeyEvent map_evdev(uint16_t evdev_code, int32_t evdev_value, bool is_tca8418_device) const;

    static czx_key_t evdev_to_czx_key(uint16_t evdev_code) noexcept;
    static uint32_t key_to_ascii(czx_key_t key, bool shift) noexcept;

private:
    struct SymEntry {
        czx_key_t key;
        uint32_t codepoint;
    };

    std::unordered_map<uint16_t, SymEntry> sym_map_;
    static uint32_t sym_name_to_codepoint(const char* name) noexcept;
    static czx_key_t codepoint_to_base_key(uint32_t cp) noexcept;
};

}  // namespace czx::input
