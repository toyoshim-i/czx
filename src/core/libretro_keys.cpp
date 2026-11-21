#include "core/libretro_keys.hpp"

#include "czx/libretro.h"

namespace czx::core {

namespace {

uint32_t base_retrok_for_czx_key(czx_key_t key) noexcept {
    if (key >= CZX_KEY_A && key <= CZX_KEY_Z) {
        return RETROK_a + static_cast<uint32_t>(key - CZX_KEY_A);
    }
    if (key >= CZX_KEY_0 && key <= CZX_KEY_9) {
        return RETROK_0 + static_cast<uint32_t>(key - CZX_KEY_0);
    }
    if (key >= CZX_KEY_F1 && key <= CZX_KEY_F12) {
        return RETROK_F1 + static_cast<uint32_t>(key - CZX_KEY_F1);
    }
    switch (key) {
        case CZX_KEY_ENTER: return RETROK_RETURN;
        case CZX_KEY_ESC: return RETROK_ESCAPE;
        case CZX_KEY_BACKSPACE: return RETROK_BACKSPACE;
        case CZX_KEY_TAB: return RETROK_TAB;
        case CZX_KEY_SPACE: return RETROK_SPACE;
        case CZX_KEY_MINUS: return RETROK_MINUS;
        case CZX_KEY_EQUAL: return RETROK_EQUALS;
        case CZX_KEY_LEFTBRACE: return RETROK_LEFTBRACKET;
        case CZX_KEY_RIGHTBRACE: return RETROK_RIGHTBRACKET;
        case CZX_KEY_BACKSLASH: return RETROK_BACKSLASH;
        case CZX_KEY_SEMICOLON: return RETROK_SEMICOLON;
        case CZX_KEY_APOSTROPHE: return RETROK_QUOTE;
        case CZX_KEY_GRAVE: return RETROK_BACKQUOTE;
        case CZX_KEY_COMMA: return RETROK_COMMA;
        case CZX_KEY_DOT: return RETROK_PERIOD;
        case CZX_KEY_SLASH: return RETROK_SLASH;
        case CZX_KEY_UP: return RETROK_UP;
        case CZX_KEY_DOWN: return RETROK_DOWN;
        case CZX_KEY_LEFT: return RETROK_LEFT;
        case CZX_KEY_RIGHT: return RETROK_RIGHT;
        case CZX_KEY_HOME: return RETROK_HOME;
        case CZX_KEY_END: return RETROK_END;
        case CZX_KEY_PAGEUP: return RETROK_PAGEUP;
        case CZX_KEY_PAGEDOWN: return RETROK_PAGEDOWN;
        case CZX_KEY_INSERT: return RETROK_INSERT;
        case CZX_KEY_DELETE: return RETROK_DELETE;
        case CZX_KEY_LSHIFT: return RETROK_LSHIFT;
        case CZX_KEY_RSHIFT: return RETROK_RSHIFT;
        case CZX_KEY_LCTRL: return RETROK_LCTRL;
        case CZX_KEY_RCTRL: return RETROK_RCTRL;
        case CZX_KEY_LALT: return RETROK_LALT;
        case CZX_KEY_RALT: return RETROK_RALT;
        case CZX_KEY_LMETA: return RETROK_LMETA;
        case CZX_KEY_RMETA: return RETROK_RMETA;
        case CZX_KEY_PRINT: return RETROK_PRINT;
        case CZX_KEY_HELP: return RETROK_HELP;
        default: return RETROK_UNKNOWN;
    }
}

bool map_symbol_codepoint(uint32_t cp, uint32_t& out_base_retrok, bool& out_shifted) noexcept {
    switch (cp) {
        // Shifted US symbols (used when Cardputer Zero SYM layer emits symbols without Shift)
        case '!': out_base_retrok = RETROK_1; out_shifted = true; return true;
        case '@': out_base_retrok = RETROK_2; out_shifted = true; return true;
        case '#': out_base_retrok = RETROK_3; out_shifted = true; return true;
        case '$': out_base_retrok = RETROK_4; out_shifted = true; return true;
        case '%': out_base_retrok = RETROK_5; out_shifted = true; return true;
        case '^': out_base_retrok = RETROK_6; out_shifted = true; return true;
        case '&': out_base_retrok = RETROK_7; out_shifted = true; return true;
        case '*': out_base_retrok = RETROK_8; out_shifted = true; return true;
        case '(': out_base_retrok = RETROK_9; out_shifted = true; return true;
        case ')': out_base_retrok = RETROK_0; out_shifted = true; return true;
        case '_': out_base_retrok = RETROK_MINUS; out_shifted = true; return true;
        case '+': out_base_retrok = RETROK_EQUALS; out_shifted = true; return true;
        case '{': out_base_retrok = RETROK_LEFTBRACKET; out_shifted = true; return true;
        case '}': out_base_retrok = RETROK_RIGHTBRACKET; out_shifted = true; return true;
        case '|': out_base_retrok = RETROK_BACKSLASH; out_shifted = true; return true;
        case ':': out_base_retrok = RETROK_SEMICOLON; out_shifted = true; return true;
        case '"': out_base_retrok = RETROK_QUOTE; out_shifted = true; return true;
        case '<': out_base_retrok = RETROK_COMMA; out_shifted = true; return true;
        case '>': out_base_retrok = RETROK_PERIOD; out_shifted = true; return true;
        case '?': out_base_retrok = RETROK_SLASH; out_shifted = true; return true;
        case '~': out_base_retrok = RETROK_BACKQUOTE; out_shifted = true; return true;

        // Unshifted US symbols (also emitted by SYM layer on Cardputer Zero)
        case '`': out_base_retrok = RETROK_BACKQUOTE; out_shifted = false; return true;
        case '-': out_base_retrok = RETROK_MINUS; out_shifted = false; return true;
        case '=': out_base_retrok = RETROK_EQUALS; out_shifted = false; return true;
        case '[': out_base_retrok = RETROK_LEFTBRACKET; out_shifted = false; return true;
        case ']': out_base_retrok = RETROK_RIGHTBRACKET; out_shifted = false; return true;
        case '\\': out_base_retrok = RETROK_BACKSLASH; out_shifted = false; return true;
        case ';': out_base_retrok = RETROK_SEMICOLON; out_shifted = false; return true;
        case '\'': out_base_retrok = RETROK_QUOTE; out_shifted = false; return true;
        case ',': out_base_retrok = RETROK_COMMA; out_shifted = false; return true;
        case '.': out_base_retrok = RETROK_PERIOD; out_shifted = false; return true;
        case '/': out_base_retrok = RETROK_SLASH; out_shifted = false; return true;
        default: return false;
    }
}

}  // namespace

uint16_t map_mods_to_libretro(uint32_t mods) noexcept {
    uint16_t out = RETROKMOD_NONE;
    if (mods & CZX_MOD_SHIFT) out |= RETROKMOD_SHIFT;
    if (mods & CZX_MOD_CTRL) out |= RETROKMOD_CTRL;
    if (mods & CZX_MOD_ALT) out |= RETROKMOD_ALT;
    return out;
}

LibretroKeyMapping map_key_to_libretro(const czx_key_event_t& ev) noexcept {
    LibretroKeyMapping m{};
    m.retro_mods = map_mods_to_libretro(ev.mods);

    // SYM key itself is a Cardputer Zero layer modifier; do not forward as a guest key.
    if (ev.key == CZX_KEY_SYM || ev.key == CZX_KEY_FN) {
        return m;
    }

    uint32_t sym_retrok = RETROK_UNKNOWN;
    bool sym_shifted = false;
    if (ev.codepoint != 0 && map_symbol_codepoint(ev.codepoint, sym_retrok, sym_shifted)) {
        m.retrok = sym_retrok;
        m.character = ev.codepoint;
        if (sym_shifted && (ev.mods & CZX_MOD_SHIFT) == 0) {
            m.needs_synthetic_shift = true;
            m.retro_mods |= RETROKMOD_SHIFT;
        }
        return m;
    }

    m.retrok = base_retrok_for_czx_key(ev.key);
    if (ev.codepoint >= 0x20 && ev.codepoint < 0x7F) {
        m.character = ev.codepoint;
    } else if (ev.key == CZX_KEY_ENTER) {
        m.character = '\r';
    } else if (ev.key == CZX_KEY_BACKSPACE) {
        m.character = '\b';
    } else if (ev.key == CZX_KEY_TAB) {
        m.character = '\t';
    } else if (ev.key == CZX_KEY_SPACE) {
        m.character = ' ';
    } else if (ev.key == CZX_KEY_ESC) {
        m.character = 0x1B;
    }
    return m;
}

}  // namespace czx::core
