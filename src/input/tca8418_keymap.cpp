#include "input/tca8418_keymap.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef __linux__
#include <linux/input-event-codes.h>
#endif

namespace czx::input {

namespace {

// Cardputer Zero driver emits KEY_CD (383) for SYM and KEY_DVD (389) for Fn.
constexpr uint16_t kEvdevSymKeyCd = 383;
constexpr uint16_t kEvdevFnKeyDvd = 389;
constexpr uint16_t kEvdevKeyFn = 0x1d0;

}  // namespace

Tca8418Keymap::Tca8418Keymap() {
    // Built-in defaults matching m5stack/m5stack-linux-dtoverlays tca8418_keypad_m5stack_keymap.map.
    struct DefaultSym {
        uint16_t code;
        czx_key_t key;
        uint32_t cp;
    };
    static constexpr DefaultSym kDefaults[] = {
        {26, CZX_KEY_1, '!'},          {27, CZX_KEY_2, '@'},          {39, CZX_KEY_3, '#'},
        {40, CZX_KEY_4, '$'},          {41, CZX_KEY_5, '%'},          {43, CZX_KEY_6, '^'},
        {51, CZX_KEY_7, '&'},          {52, CZX_KEY_8, '*'},          {53, CZX_KEY_9, '('},
        {94, CZX_KEY_0, ')'},          {55, CZX_KEY_GRAVE, '~'},      {69, CZX_KEY_GRAVE, '`'},
        {70, CZX_KEY_MINUS, '_'},      {71, CZX_KEY_MINUS, '-'},      {72, CZX_KEY_EQUAL, '+'},
        {73, CZX_KEY_EQUAL, '='},      {74, CZX_KEY_LEFTBRACE, '['},  {75, CZX_KEY_RIGHTBRACE, ']'},
        {76, CZX_KEY_LEFTBRACE, '{'},  {77, CZX_KEY_RIGHTBRACE, '}'}, {79, CZX_KEY_SEMICOLON, ';'},
        {80, CZX_KEY_SEMICOLON, ':'},  {81, CZX_KEY_APOSTROPHE, '\''},{82, CZX_KEY_APOSTROPHE, '"'},
        {83, CZX_KEY_COMMA, '<'},      {85, CZX_KEY_DOT, '>'},        {86, CZX_KEY_BACKSLASH, '\\'},
        {89, CZX_KEY_BACKSLASH, '|'},  {90, CZX_KEY_COMMA, ','},      {91, CZX_KEY_DOT, '.'},
        {92, CZX_KEY_SLASH, '/'},      {93, CZX_KEY_SLASH, '?'},
    };
    for (const auto& e : kDefaults) {
        sym_map_[e.code] = SymEntry{e.key, e.cp};
    }
}

uint32_t Tca8418Keymap::sym_name_to_codepoint(const char* name) noexcept {
    struct Entry {
        const char* name;
        uint32_t cp;
    };
    static constexpr Entry kTable[] = {
        {"exclam", '!'},       {"at", '@'},          {"numbersign", '#'}, {"dollar", '$'},
        {"percent", '%'},      {"asciicircum", '^'}, {"ampersand", '&'},  {"asterisk", '*'},
        {"parenleft", '('},    {"parenright", ')'},  {"asciitilde", '~'}, {"grave", '`'},
        {"underscore", '_'},   {"minus", '-'},       {"plus", '+'},       {"equal", '='},
        {"bracketleft", '['},  {"bracketright", ']'},{"braceleft", '{'},  {"braceright", '}'},
        {"semicolon", ';'},    {"colon", ':'},       {"apostrophe", '\''},{"quotedbl", '"'},
        {"less", '<'},         {"greater", '>'},     {"backslash", '\\'}, {"bar", '|'},
        {"comma", ','},        {"period", '.'},      {"slash", '/'},      {"question", '?'},
    };
    for (const auto& e : kTable) {
        if (std::strcmp(name, e.name) == 0) return e.cp;
    }
    return 0;
}

czx_key_t Tca8418Keymap::codepoint_to_base_key(uint32_t cp) noexcept {
    switch (cp) {
        case '!': return CZX_KEY_1;
        case '@': return CZX_KEY_2;
        case '#': return CZX_KEY_3;
        case '$': return CZX_KEY_4;
        case '%': return CZX_KEY_5;
        case '^': return CZX_KEY_6;
        case '&': return CZX_KEY_7;
        case '*': return CZX_KEY_8;
        case '(': return CZX_KEY_9;
        case ')': return CZX_KEY_0;
        case '~':
        case '`': return CZX_KEY_GRAVE;
        case '_':
        case '-': return CZX_KEY_MINUS;
        case '+':
        case '=': return CZX_KEY_EQUAL;
        case '[':
        case '{': return CZX_KEY_LEFTBRACE;
        case ']':
        case '}': return CZX_KEY_RIGHTBRACE;
        case ';':
        case ':': return CZX_KEY_SEMICOLON;
        case '\'':
        case '"': return CZX_KEY_APOSTROPHE;
        case '<':
        case ',': return CZX_KEY_COMMA;
        case '>':
        case '.': return CZX_KEY_DOT;
        case '\\':
        case '|': return CZX_KEY_BACKSLASH;
        case '/':
        case '?': return CZX_KEY_SLASH;
        default: return CZX_KEY_NONE;
    }
}

bool Tca8418Keymap::load_from_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    std::unordered_map<uint16_t, SymEntry> loaded;
    std::string line;
    while (std::getline(ifs, line)) {
        const auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line.resize(hash_pos);
        }
        unsigned int code = 0;
        char kw[32] = {};
        char eq[8] = {};
        char sym[64] = {};
        if (std::sscanf(line.c_str(), " %31s %u %7s %63s", kw, &code, eq, sym) == 4 &&
            std::strcmp(kw, "keycode") == 0 && std::strcmp(eq, "=") == 0) {
            const uint32_t cp = sym_name_to_codepoint(sym);
            if (cp != 0 && code <= 0xFFFFu) {
                loaded[static_cast<uint16_t>(code)] = SymEntry{codepoint_to_base_key(cp), cp};
            }
        }
    }

    if (loaded.empty()) {
        return false;
    }
    for (const auto& [k, v] : loaded) {
        sym_map_[k] = v;
    }
    return true;
}

czx_key_t Tca8418Keymap::evdev_to_czx_key(uint16_t code) noexcept {
#ifdef __linux__
    switch (code) {
        case KEY_ESC: return CZX_KEY_ESC;
        case KEY_1: return CZX_KEY_1;
        case KEY_2: return CZX_KEY_2;
        case KEY_3: return CZX_KEY_3;
        case KEY_4: return CZX_KEY_4;
        case KEY_5: return CZX_KEY_5;
        case KEY_6: return CZX_KEY_6;
        case KEY_7: return CZX_KEY_7;
        case KEY_8: return CZX_KEY_8;
        case KEY_9: return CZX_KEY_9;
        case KEY_0: return CZX_KEY_0;
        case KEY_MINUS: return CZX_KEY_MINUS;
        case KEY_EQUAL: return CZX_KEY_EQUAL;
        case KEY_BACKSPACE: return CZX_KEY_BACKSPACE;
        case KEY_TAB: return CZX_KEY_TAB;
        case KEY_Q: return CZX_KEY_Q;
        case KEY_W: return CZX_KEY_W;
        case KEY_E: return CZX_KEY_E;
        case KEY_R: return CZX_KEY_R;
        case KEY_T: return CZX_KEY_T;
        case KEY_Y: return CZX_KEY_Y;
        case KEY_U: return CZX_KEY_U;
        case KEY_I: return CZX_KEY_I;
        case KEY_O: return CZX_KEY_O;
        case KEY_P: return CZX_KEY_P;
        case KEY_LEFTBRACE: return CZX_KEY_LEFTBRACE;
        case KEY_RIGHTBRACE: return CZX_KEY_RIGHTBRACE;
        case KEY_ENTER:
        case KEY_KPENTER: return CZX_KEY_ENTER;
        case KEY_LEFTCTRL: return CZX_KEY_LCTRL;
        case KEY_RIGHTCTRL: return CZX_KEY_RCTRL;
        case KEY_A: return CZX_KEY_A;
        case KEY_S: return CZX_KEY_S;
        case KEY_D: return CZX_KEY_D;
        case KEY_F: return CZX_KEY_F;
        case KEY_G: return CZX_KEY_G;
        case KEY_H: return CZX_KEY_H;
        case KEY_J: return CZX_KEY_J;
        case KEY_K: return CZX_KEY_K;
        case KEY_L: return CZX_KEY_L;
        case KEY_SEMICOLON: return CZX_KEY_SEMICOLON;
        case KEY_APOSTROPHE: return CZX_KEY_APOSTROPHE;
        case KEY_GRAVE: return CZX_KEY_GRAVE;
        case KEY_LEFTSHIFT: return CZX_KEY_LSHIFT;
        case KEY_RIGHTSHIFT: return CZX_KEY_RSHIFT;
        case KEY_BACKSLASH: return CZX_KEY_BACKSLASH;
        case KEY_Z: return CZX_KEY_Z;
        case KEY_X: return CZX_KEY_X;
        case KEY_C: return CZX_KEY_C;
        case KEY_V: return CZX_KEY_V;
        case KEY_B: return CZX_KEY_B;
        case KEY_N: return CZX_KEY_N;
        case KEY_M: return CZX_KEY_M;
        case KEY_COMMA: return CZX_KEY_COMMA;
        case KEY_DOT: return CZX_KEY_DOT;
        case KEY_SLASH: return CZX_KEY_SLASH;
        case KEY_LEFTALT: return CZX_KEY_LALT;
        case KEY_RIGHTALT: return CZX_KEY_RALT;
        case KEY_SPACE: return CZX_KEY_SPACE;
        case KEY_F1: return CZX_KEY_F1;
        case KEY_F2: return CZX_KEY_F2;
        case KEY_F3: return CZX_KEY_F3;
        case KEY_F4: return CZX_KEY_F4;
        case KEY_F5: return CZX_KEY_F5;
        case KEY_F6: return CZX_KEY_F6;
        case KEY_F7: return CZX_KEY_F7;
        case KEY_F8: return CZX_KEY_F8;
        case KEY_F9: return CZX_KEY_F9;
        case KEY_F10: return CZX_KEY_F10;
        case KEY_F11: return CZX_KEY_F11;
        case KEY_F12: return CZX_KEY_F12;
        case KEY_UP: return CZX_KEY_UP;
        case KEY_DOWN: return CZX_KEY_DOWN;
        case KEY_LEFT: return CZX_KEY_LEFT;
        case KEY_RIGHT: return CZX_KEY_RIGHT;
        case KEY_HOME: return CZX_KEY_HOME;
        case KEY_END: return CZX_KEY_END;
        case KEY_PAGEUP: return CZX_KEY_PAGEUP;
        case KEY_PAGEDOWN: return CZX_KEY_PAGEDOWN;
        case KEY_INSERT: return CZX_KEY_INSERT;
        case KEY_DELETE: return CZX_KEY_DELETE;
        case KEY_LEFTMETA: return CZX_KEY_LMETA;
        case KEY_RIGHTMETA: return CZX_KEY_RMETA;
        case KEY_SYSRQ: return CZX_KEY_PRINT;
        case KEY_HELP: return CZX_KEY_HELP;
        case KEY_MUTE: return CZX_KEY_MUTE;
        case KEY_VOLUMEDOWN: return CZX_KEY_VOLUMEDOWN;
        case KEY_VOLUMEUP: return CZX_KEY_VOLUMEUP;
        case kEvdevSymKeyCd:
        case KEY_COMPOSE: return CZX_KEY_SYM;
        case kEvdevFnKeyDvd:
        case kEvdevKeyFn: return CZX_KEY_FN;
        default: return CZX_KEY_NONE;
    }
#else
    (void)code;
    return CZX_KEY_NONE;
#endif
}

RawKeyEvent Tca8418Keymap::map_evdev(uint16_t evdev_code, int32_t evdev_value, bool is_tca8418_device) const {
    RawKeyEvent ev{};
    ev.pressed = (evdev_value != 0);
    ev.repeat = (evdev_value == 2);

    if (is_tca8418_device) {
        const auto it = sym_map_.find(evdev_code);
        if (it != sym_map_.end()) {
            ev.key = it->second.key;
            ev.codepoint = it->second.codepoint;
            return ev;
        }
    }

    ev.key = evdev_to_czx_key(evdev_code);
    return ev;
}

uint32_t Tca8418Keymap::key_to_ascii(czx_key_t key, bool shift) noexcept {
    if (key >= CZX_KEY_A && key <= CZX_KEY_Z) {
        const uint32_t offset = static_cast<uint32_t>(key - CZX_KEY_A);
        return (shift ? 'A' : 'a') + offset;
    }
    if (key >= CZX_KEY_0 && key <= CZX_KEY_9) {
        static constexpr char kShiftDigits[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        const size_t idx = static_cast<size_t>(key - CZX_KEY_0);
        return shift ? static_cast<uint32_t>(kShiftDigits[idx])
                     : static_cast<uint32_t>('0' + idx);
    }
    switch (key) {
        case CZX_KEY_SPACE: return ' ';
        case CZX_KEY_ENTER: return '\r';
        case CZX_KEY_BACKSPACE: return '\b';
        case CZX_KEY_TAB: return '\t';
        case CZX_KEY_ESC: return 0x1B;
        case CZX_KEY_MINUS: return shift ? '_' : '-';
        case CZX_KEY_EQUAL: return shift ? '+' : '=';
        case CZX_KEY_LEFTBRACE: return shift ? '{' : '[';
        case CZX_KEY_RIGHTBRACE: return shift ? '}' : ']';
        case CZX_KEY_BACKSLASH: return shift ? '|' : '\\';
        case CZX_KEY_SEMICOLON: return shift ? ':' : ';';
        case CZX_KEY_APOSTROPHE: return shift ? '"' : '\'';
        case CZX_KEY_GRAVE: return shift ? '~' : '`';
        case CZX_KEY_COMMA: return shift ? '<' : ',';
        case CZX_KEY_DOT: return shift ? '>' : '.';
        case CZX_KEY_SLASH: return shift ? '?' : '/';
        default: return 0;
    }
}

}  // namespace czx::input
