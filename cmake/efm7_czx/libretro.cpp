#include "czx/libretro.h"

#include "paths.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "emu.h"
#include "vm/device.h"
#include "vm/fm7/fm7.h"
#include "vm/fm7/fm7_common.h"
#pragma GCC diagnostic pop

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef RETRO_EXPORT
#define RETRO_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifndef VK_OEM_CSP_KPRET
#define VK_OEM_CSP_KPRET 0x9F
#endif

config_t config;

namespace {

constexpr int kSampleRate = 48000;
constexpr int kSoundSamples = static_cast<int>(static_cast<double>(kSampleRate) / FRAMES_PER_SEC + 0.5);

retro_environment_t g_env_cb = nullptr;
retro_video_refresh_t g_video_cb = nullptr;
retro_audio_sample_t g_audio_sample_cb = nullptr;
retro_audio_sample_batch_t g_audio_batch_cb = nullptr;
retro_input_poll_t g_input_poll_cb = nullptr;
retro_input_state_t g_input_state_cb = nullptr;
retro_log_printf_t g_log_cb = nullptr;
retro_set_led_state_t g_led_cb = nullptr;

EMU* g_emu = nullptr;
VM* g_vm = nullptr;

bool g_tape_turbo = true;
bool g_host_supports_ff = false;
bool g_ff_active = false;
int g_boot_mode_pref = 0;  // 0=auto, 1=basic, 2=dos
int g_swap_target = 0;     // 0=FDD0, 1=FDD1, 2=CMT

std::array<std::string, 2> g_fdd_path;
std::array<bool, 2> g_fdd_ejected{{false, false}};
std::string g_tape_path;
bool g_tape_ejected = false;
bool g_resume_tape_on_insert = false;
std::string g_initial_arg_path;

int g_last_led_fdd = -1;
int g_last_led_tape = -1;

struct ActiveKeyState {
    bool down = false;
    int vk = 0;
    int8_t shift_override = 0;  // 0 = physical, +1 = force shift, -1 = force unshifted
};

std::array<ActiveKeyState, RETROK_LAST> g_active_keys{};
bool g_host_lshift = false;
bool g_host_rshift = false;
bool g_vm_lshift = false;
bool g_vm_rshift = false;
uint8_t g_key_buf[256]{};
uint32_t g_joy_buf[4]{};
int32_t g_mouse_buf[3]{};

scrntype_t g_screen_640x400[640 * 400]{};
uint16_t g_fb_640x200[640 * 200]{};
uint16_t g_fb_320x200[320 * 200]{};

std::string to_lower(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trim_ws(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

__attribute__((format(printf, 2, 3)))
void log_msg(enum retro_log_level level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log_cb) {
        g_log_cb(level, "efm7: %s\n", buf);
    } else if (level >= RETRO_LOG_WARN) {
        std::fprintf(stderr, "[efm7] %s\n", buf);
    }
}

bool is_floppy_ext(const std::string& ext) {
    return ext == ".d77" || ext == ".77d" || ext == ".d88" || ext == ".2d";
}

bool is_tape_ext(const std::string& ext) {
    return ext == ".t77" || ext == ".wav";
}

void clear_leds() {
    if (g_led_cb) {
        g_led_cb(1, 0);
        g_led_cb(2, 0);
    }
    g_last_led_fdd = 0;
    g_last_led_tape = 0;
}

void update_leds() {
    if (!g_led_cb || !g_vm) return;

    const int fdd = static_cast<int>(g_vm->is_floppy_disk_accessed() & 0x0Fu);
    int tape = 0;
    if (g_vm->is_tape_playing(0)) {
        tape = 100 + std::clamp(g_vm->get_tape_position(0), 0, 100);
    } else if (g_vm->is_tape_recording(0)) {
        tape = 300 + std::clamp(g_vm->get_tape_position(0), 0, 100);
    }

    if (fdd != g_last_led_fdd) {
        g_led_cb(1, fdd);
        g_last_led_fdd = fdd;
    }
    if (tape != g_last_led_tape) {
        g_led_cb(2, tape);
        g_last_led_tape = tape;
    }
}

void clear_fastforward() {
    if (g_host_supports_ff && g_env_cb && g_ff_active) {
        retro_fastforwarding_override ov{0.0f, false, false, false};
        g_env_cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, &ov);
    }
    g_ff_active = false;
}

void update_fastforward() {
    const bool want_ff = g_vm && g_tape_turbo && !config.sound_tape_signal &&
                         (g_vm->is_tape_playing(0) || g_vm->is_tape_recording(0));
    if (g_host_supports_ff && g_env_cb && want_ff != g_ff_active) {
        retro_fastforwarding_override ov{0.0f, want_ff, false, false};
        g_env_cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, &ov);
        g_ff_active = want_ff;
    }
}

void init_default_config() {
    std::memset(&config, 0, sizeof(config));
#if defined(_FM7)
    config.dipswitch = static_cast<uint32_t>(DIPSWITCH_DEFAULT) |
                       FM7_DIPSW_CONNECT_320KFDC | FM7_DIPSW_CONNECT_KANJIROM;
#else
    config.dipswitch = static_cast<uint32_t>(DIPSWITCH_DEFAULT);
#endif
    config.boot_mode = FM7_BOOTMODE_BASIC;
    config.cpu_type = CPU_TYPE_DEFAULT;
    config.sound_type = 1;  // FM-7: PSG+OPN, FM77AV/AV40: OPN
#ifdef USE_MONITOR_TYPE
    config.monitor_type = FM7_MONITOR_STANDARD;
#endif
    config.mouse_type = 0;
    config.scan_line = false;
    for (int i = 0; i < USE_FLOPPY_DISK; ++i) {
        config.correct_disk_timing[i] = true;
        config.ignore_disk_crc[i] = IGNORE_DISK_CRC_DEFAULT;
    }
    config.wave_shaper[0] = true;
    config.direct_load_mzt[0] = false;
    config.baud_high[0] = false;
    config.drive_vm_in_opecode = true;
    config.sound_frequency = 6;  // 48000 Hz
    config.sound_strict_rendering = false;
    config.sound_noise_fdd = false;
    config.sound_noise_cmt = false;
    config.sound_tape_signal = false;
    config.sound_tape_voice = false;
    config.cpu_power = 0;
    for (int i = 0; i < USE_SOUND_VOLUME; ++i) {
        config.sound_volume_l[i] = 0;
        config.sound_volume_r[i] = 0;
    }
    g_tape_turbo = true;
    g_boot_mode_pref = 0;
    g_swap_target = 0;
}

void sync_swap_target_option() {
    if (!g_env_cb) return;
    retro_variable var{};
    var.key = "efm7_drive";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (std::strcmp(var.value, "FDD1") == 0) {
            g_swap_target = 1;
        } else if (std::strcmp(var.value, "CMT") == 0) {
            g_swap_target = 2;
        } else {
            g_swap_target = 0;
        }
    }
}

void apply_core_options() {
    if (!g_env_cb) return;

    retro_variable var{};
    var.key = "efm7_boot_mode";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (std::strcmp(var.value, "basic") == 0) {
            g_boot_mode_pref = 1;
        } else if (std::strcmp(var.value, "dos") == 0) {
            g_boot_mode_pref = 2;
        } else {
            g_boot_mode_pref = 0;
        }
    }

    sync_swap_target_option();

    var.key = "efm7_sound_board";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
#if defined(_FM7)
        config.sound_type = (std::strcmp(var.value, "PSG") == 0) ? 0 : 1;
#else
        config.sound_type = (std::strcmp(var.value, "OPN+WHG") == 0) ? 3 : 1;
#endif
    }

    var.key = "efm7_tape_sound";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        config.sound_tape_signal = (std::strcmp(var.value, "enabled") == 0);
    }

    var.key = "efm7_tape_turbo";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        g_tape_turbo = (std::strcmp(var.value, "disabled") != 0);
    }

    if (g_vm) {
        g_vm->update_config();
    }
}

bool parse_initial_media_spec(const char* path) {
    g_fdd_path[0].clear();
    g_fdd_path[1].clear();
    g_tape_path.clear();
    g_fdd_ejected[0] = false;
    g_fdd_ejected[1] = false;
    g_tape_ejected = false;
    g_resume_tape_on_insert = false;
    g_initial_arg_path.clear();

    if (!path || path[0] == '\0') {
        return true;
    }

    g_initial_arg_path = path;
    const std::filesystem::path p(path);
    const std::string ext = to_lower(p.extension().string());

    if (ext == ".m3u") {
        std::ifstream ifs(p);
        if (!ifs.is_open()) {
            log_msg(RETRO_LOG_ERROR, "failed to open m3u playlist: %s", path);
            return false;
        }
        int next_fdd = 0;
        std::string line;
        while (std::getline(ifs, line)) {
            line = trim_ws(line);
            if (line.empty() || line[0] == '#') continue;
            std::filesystem::path item(line);
            if (item.is_relative()) {
                item = p.parent_path() / item;
            }
            const std::string item_ext = to_lower(item.extension().string());
            if (is_floppy_ext(item_ext)) {
                if (next_fdd < 2) {
                    g_fdd_path[next_fdd++] = item.string();
                }
            } else if (is_tape_ext(item_ext)) {
                if (g_tape_path.empty()) {
                    g_tape_path = item.string();
                }
            }
        }
        return !g_fdd_path[0].empty() || !g_fdd_path[1].empty() || !g_tape_path.empty();
    }

    if (is_floppy_ext(ext)) {
        g_fdd_path[0] = path;
        return true;
    }
    if (is_tape_ext(ext)) {
        g_tape_path = path;
        return true;
    }
    log_msg(RETRO_LOG_ERROR, "unsupported media extension '%s': %s", ext.c_str(), path);
    return false;
}

bool mount_configured_media() {
    if (!g_vm) return false;
    bool ok = true;
    for (int drv = 0; drv < 2; ++drv) {
        if (!g_fdd_ejected[drv] && !g_fdd_path[drv].empty()) {
            g_vm->open_floppy_disk(drv, g_fdd_path[drv].c_str(), 0);
            const bool inserted = g_vm->is_floppy_disk_inserted(drv);
            log_msg(inserted ? RETRO_LOG_INFO : RETRO_LOG_ERROR,
                    "mount FDD%d (%s): %s", drv, inserted ? "ok" : "failed", g_fdd_path[drv].c_str());
            if (!inserted) ok = false;
        }
    }
    if (!g_tape_ejected && !g_tape_path.empty()) {
        g_vm->play_tape(0, g_tape_path.c_str());
        const bool inserted = g_vm->is_tape_inserted(0);
        if (inserted && !g_resume_tape_on_insert) {
            g_vm->push_stop(0);
        }
        log_msg(inserted ? RETRO_LOG_INFO : RETRO_LOG_ERROR,
                "mount CMT (%s): %s", inserted ? "ok" : "failed", g_tape_path.c_str());
        if (!inserted) ok = false;
    }
    return ok;
}

bool RETRO_CALLCONV disk_set_eject_state(bool ejected) {
    if (!g_vm) return false;
    sync_swap_target_option();
    if (g_swap_target == 2) {
        if (ejected == g_tape_ejected) return true;
        if (ejected) {
            g_resume_tape_on_insert = g_vm->is_tape_playing(0);
            g_vm->close_tape(0);
            g_tape_ejected = true;
            update_leds();
            update_fastforward();
            return true;
        }
        g_tape_ejected = false;
        bool ok = true;
        if (!g_tape_path.empty()) {
            g_vm->play_tape(0, g_tape_path.c_str());
            ok = g_vm->is_tape_inserted(0);
            if (ok) {
                if (g_resume_tape_on_insert) {
                    g_vm->push_play(0);
                } else {
                    g_vm->push_stop(0);
                }
            }
        }
        g_resume_tape_on_insert = false;
        update_leds();
        update_fastforward();
        return ok;
    }

    const int drv = (g_swap_target == 1) ? 1 : 0;
    if (ejected == g_fdd_ejected[drv]) return true;
    if (ejected) {
        g_vm->close_floppy_disk(drv);
        g_fdd_ejected[drv] = true;
        update_leds();
        return true;
    }
    g_fdd_ejected[drv] = false;
    bool ok = true;
    if (!g_fdd_path[drv].empty()) {
        g_vm->open_floppy_disk(drv, g_fdd_path[drv].c_str(), 0);
        ok = g_vm->is_floppy_disk_inserted(drv);
    }
    update_leds();
    return ok;
}

bool RETRO_CALLCONV disk_get_eject_state(void) {
    sync_swap_target_option();
    if (g_swap_target == 2) return g_tape_ejected;
    return g_fdd_ejected[(g_swap_target == 1) ? 1 : 0];
}

unsigned RETRO_CALLCONV disk_get_image_index(void) {
    return 0u;
}

bool RETRO_CALLCONV disk_set_image_index(unsigned index) {
    return index == 0u;
}

unsigned RETRO_CALLCONV disk_get_num_images(void) {
    sync_swap_target_option();
    if (g_swap_target == 2) {
        return g_tape_path.empty() ? 0u : 1u;
    }
    return g_fdd_path[(g_swap_target == 1) ? 1 : 0].empty() ? 0u : 1u;
}

bool RETRO_CALLCONV disk_replace_image_index(unsigned index, const struct retro_game_info* info) {
    sync_swap_target_option();
    if (index != 0u || !disk_get_eject_state()) return false;
    std::string* slot_path = (g_swap_target == 2)
                                 ? &g_tape_path
                                 : &g_fdd_path[(g_swap_target == 1) ? 1 : 0];
    if (!info || !info->path || info->path[0] == '\0') {
        slot_path->clear();
        return true;
    }
    std::error_code ec;
    const std::string ext = to_lower(std::filesystem::path(info->path).extension().string());
    const bool valid = (g_swap_target == 2) ? is_tape_ext(ext) : is_floppy_ext(ext);
    if (!valid || !std::filesystem::is_regular_file(info->path, ec)) {
        log_msg(RETRO_LOG_ERROR, "cannot replace target %d media with '%s'", g_swap_target, info->path);
        return false;
    }
    *slot_path = info->path;
    return true;
}

bool RETRO_CALLCONV disk_add_image_index(void) {
    return true;
}

bool map_jis_char(uint32_t ch, int& out_vk, bool& out_need_shift) {
    switch (ch) {
        case '!':  out_vk = '1';            out_need_shift = true;  return true;
        case '"':  out_vk = '2';            out_need_shift = true;  return true;
        case '#':  out_vk = '3';            out_need_shift = true;  return true;
        case '$':  out_vk = '4';            out_need_shift = true;  return true;
        case '%':  out_vk = '5';            out_need_shift = true;  return true;
        case '&':  out_vk = '6';            out_need_shift = true;  return true;
        case '\'': out_vk = '7';            out_need_shift = true;  return true;
        case '(':  out_vk = '8';            out_need_shift = true;  return true;
        case ')':  out_vk = '9';            out_need_shift = true;  return true;
        case '-':  out_vk = VK_OEM_MINUS;   out_need_shift = false; return true;
        case '=':  out_vk = VK_OEM_MINUS;   out_need_shift = true;  return true;
        case '^':  out_vk = VK_OEM_7;       out_need_shift = false; return true;
        case '~':  out_vk = VK_OEM_7;       out_need_shift = true;  return true;
        case '\\': out_vk = VK_OEM_5;       out_need_shift = false; return true;
        case '|':  out_vk = VK_OEM_5;       out_need_shift = true;  return true;
        case '@':  out_vk = VK_OEM_3;       out_need_shift = false; return true;
        case '`':  out_vk = VK_OEM_3;       out_need_shift = true;  return true;
        case '[':  out_vk = VK_OEM_4;       out_need_shift = false; return true;
        case '{':  out_vk = VK_OEM_4;       out_need_shift = true;  return true;
        case ';':  out_vk = VK_OEM_PLUS;    out_need_shift = false; return true;
        case '+':  out_vk = VK_OEM_PLUS;    out_need_shift = true;  return true;
        case ':':  out_vk = VK_OEM_1;       out_need_shift = false; return true;
        case '*':  out_vk = VK_OEM_1;       out_need_shift = true;  return true;
        case ']':  out_vk = VK_OEM_6;       out_need_shift = false; return true;
        case '}':  out_vk = VK_OEM_6;       out_need_shift = true;  return true;
        case ',':  out_vk = VK_OEM_COMMA;   out_need_shift = false; return true;
        case '<':  out_vk = VK_OEM_COMMA;   out_need_shift = true;  return true;
        case '.':  out_vk = VK_OEM_PERIOD;  out_need_shift = false; return true;
        case '>':  out_vk = VK_OEM_PERIOD;  out_need_shift = true;  return true;
        case '/':  out_vk = VK_OEM_2;       out_need_shift = false; return true;
        case '?':  out_vk = VK_OEM_2;       out_need_shift = true;  return true;
        case '_':  out_vk = VK_OEM_102;     out_need_shift = true;  return true;
        default:   return false;
    }
}

int retrok_to_vk(unsigned keycode) {
    if (keycode >= RETROK_a && keycode <= RETROK_z) {
        return 'A' + static_cast<int>(keycode - RETROK_a);
    }
    if (keycode >= RETROK_0 && keycode <= RETROK_9) {
        return '0' + static_cast<int>(keycode - RETROK_0);
    }
    if (keycode >= RETROK_KP0 && keycode <= RETROK_KP9) {
        return VK_NUMPAD0 + static_cast<int>(keycode - RETROK_KP0);
    }
    if (keycode >= RETROK_F1 && keycode <= RETROK_F10) {
        return VK_F1 + static_cast<int>(keycode - RETROK_F1);
    }
    switch (keycode) {
        case RETROK_RETURN: return VK_RETURN;
        case RETROK_KP_ENTER: return VK_OEM_CSP_KPRET;
        case RETROK_SPACE: return VK_SPACE;
        case RETROK_TAB: return VK_TAB;
        case RETROK_LSHIFT: return VK_LSHIFT;
        case RETROK_RSHIFT: return VK_RSHIFT;
        case RETROK_LCTRL:
        case RETROK_RCTRL: return VK_CONTROL;
        case RETROK_ESCAPE: return VK_KANJI;   // FM-7 ESC
        case RETROK_PAUSE:
        case RETROK_HELP:
        case RETROK_F11: return VK_ESCAPE;     // FM-7 BREAK
        case RETROK_LALT:
        case RETROK_LMETA:
        case RETROK_RALT:
        case RETROK_RMETA: return VK_LWIN;     // FM-7 GRAPH
        case RETROK_CAPSLOCK: return VK_CAPITAL;
        case RETROK_BACKSPACE: return VK_BACK;
        case RETROK_DELETE: return VK_DELETE;
        case RETROK_INSERT: return VK_INSERT;
        case RETROK_HOME: return VK_HOME;
        case RETROK_END: return VK_END;        // FM-7 DUP
        case RETROK_PAGEUP: return VK_PRIOR;   // FM-7 EL
        case RETROK_PAGEDOWN: return VK_NEXT;  // FM-7 CLS
        case RETROK_LEFT: return VK_LEFT;
        case RETROK_UP: return VK_UP;
        case RETROK_RIGHT: return VK_RIGHT;
        case RETROK_DOWN: return VK_DOWN;
        case RETROK_F12: return 0x15;          // FM-7 KANA
        case RETROK_MINUS: return VK_OEM_MINUS;
        case RETROK_EQUALS: return VK_OEM_7;
        case RETROK_BACKSLASH: return VK_OEM_5;
        case RETROK_BACKQUOTE:
        case RETROK_AT: return VK_OEM_3;
        case RETROK_LEFTBRACKET: return VK_OEM_4;
        case RETROK_SEMICOLON: return VK_OEM_PLUS;
        case RETROK_QUOTE: return VK_OEM_1;
        case RETROK_RIGHTBRACKET: return VK_OEM_6;
        case RETROK_COMMA: return VK_OEM_COMMA;
        case RETROK_PERIOD: return VK_OEM_PERIOD;
        case RETROK_SLASH: return VK_OEM_2;
        case RETROK_UNDERSCORE: return VK_OEM_102;
        case RETROK_KP_MULTIPLY: return VK_MULTIPLY;
        case RETROK_KP_DIVIDE: return VK_DIVIDE;
        case RETROK_KP_MINUS: return VK_SUBTRACT;
        case RETROK_KP_EQUALS: return VK_RWIN;
        case RETROK_KP_PERIOD: return VK_DECIMAL;
        default: return 0;
    }
}

void sync_vm_shift() {
    if (!g_vm) return;

    int8_t override_mode = 0;
    for (const ActiveKeyState& st : g_active_keys) {
        if (st.down && st.shift_override != 0) {
            override_mode = st.shift_override;
        }
    }

    bool want_lshift = g_host_lshift;
    bool want_rshift = g_host_rshift;
    if (override_mode > 0) {
        if (!want_lshift && !want_rshift) {
            want_lshift = true;
        }
    } else if (override_mode < 0) {
        want_lshift = false;
        want_rshift = false;
    }

    if (want_lshift != g_vm_lshift) {
        g_vm_lshift = want_lshift;
        g_key_buf[VK_LSHIFT] = want_lshift ? 0x80u : 0x00u;
        if (want_lshift) {
            g_vm->key_down(VK_LSHIFT, false);
        } else {
            g_vm->key_up(VK_LSHIFT);
        }
    }
    if (want_rshift != g_vm_rshift) {
        g_vm_rshift = want_rshift;
        g_key_buf[VK_RSHIFT] = want_rshift ? 0x80u : 0x00u;
        if (want_rshift) {
            g_vm->key_down(VK_RSHIFT, false);
        } else {
            g_vm->key_up(VK_RSHIFT);
        }
    }
}

void clear_key_states() {
    g_active_keys.fill(ActiveKeyState{});
    g_host_lshift = false;
    g_host_rshift = false;
    g_vm_lshift = false;
    g_vm_rshift = false;
    std::memset(g_key_buf, 0, sizeof(g_key_buf));
}

void RETRO_CALLCONV on_retro_keyboard(bool down, unsigned keycode, uint32_t character, uint16_t) {
    if (keycode >= RETROK_LAST) return;

    if (down && keycode == RETROK_KP_PLUS && g_vm) {
        if (g_vm->is_tape_playing(0)) {
            g_vm->push_stop(0);
            log_msg(RETRO_LOG_INFO, "tape: manual stop");
        } else {
            if ((!g_vm->is_tape_inserted(0) || g_vm->get_tape_position(0) >= 99) && !g_tape_path.empty()) {
                g_vm->play_tape(0, g_tape_path.c_str());
            }
            if (g_vm->is_tape_inserted(0)) {
                g_vm->push_play(0);
                log_msg(RETRO_LOG_INFO, "tape: manual play");
            }
        }
        return;
    }

    if (keycode == RETROK_LSHIFT) {
        if (g_host_lshift != down) {
            g_host_lshift = down;
            sync_vm_shift();
        }
        return;
    }
    if (keycode == RETROK_RSHIFT) {
        if (g_host_rshift != down) {
            g_host_rshift = down;
            sync_vm_shift();
        }
        return;
    }

    ActiveKeyState& ak = g_active_keys[keycode];
    if (down) {
        if (ak.down) return;
        int vk = 0;
        int8_t shift_override = 0;
        if (character >= 0x21u && character <= 0x7Eu &&
            !std::isalnum(static_cast<unsigned char>(character))) {
            bool need_shift = false;
            if (map_jis_char(character, vk, need_shift)) {
                shift_override = need_shift ? 1 : -1;
            }
        }
        if (vk == 0) {
            vk = retrok_to_vk(keycode);
        }
        if (vk <= 0 || vk >= 256) return;

        ak.down = true;
        ak.vk = vk;
        ak.shift_override = shift_override;
        sync_vm_shift();
        g_key_buf[vk] = 0x80u;
        if (g_vm) {
            g_vm->key_down(vk, false);
        }
    } else {
        if (!ak.down) return;
        const int vk = ak.vk;
        ak = ActiveKeyState{};
        g_key_buf[vk] = 0x00u;
        if (g_vm) {
            g_vm->key_up(vk);
        }
        sync_vm_shift();
    }
}

void destroy_vm() {
    clear_fastforward();
    clear_leds();
    g_fdd_path[0].clear();
    g_fdd_path[1].clear();
    g_fdd_ejected[0] = false;
    g_fdd_ejected[1] = false;
    g_tape_path.clear();
    g_tape_ejected = false;
    g_resume_tape_on_insert = false;
    g_initial_arg_path.clear();
    g_last_led_fdd = -1;
    g_last_led_tape = -1;
    if (g_vm) {
        delete g_vm;
        g_vm = nullptr;
    }
    if (g_emu) {
        delete g_emu;
        g_emu = nullptr;
    }
    clear_key_states();
    std::memset(g_joy_buf, 0, sizeof(g_joy_buf));
    std::memset(g_mouse_buf, 0, sizeof(g_mouse_buf));
    std::memset(g_screen_640x400, 0, sizeof(g_screen_640x400));
    std::memset(g_fb_640x200, 0, sizeof(g_fb_640x200));
    std::memset(g_fb_320x200, 0, sizeof(g_fb_320x200));
}

void poll_joypads() {
    if (!g_input_state_cb) {
        std::memset(g_joy_buf, 0, sizeof(g_joy_buf));
        return;
    }
    for (unsigned port = 0; port < 2; ++port) {
        uint32_t bits = 0;
        const int16_t mask = g_input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
        auto held = [&](unsigned id) -> bool {
            return (mask & (1 << id)) != 0 ||
                   g_input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id) != 0;
        };
        if (held(RETRO_DEVICE_ID_JOYPAD_UP))     bits |= 0x01u;
        if (held(RETRO_DEVICE_ID_JOYPAD_DOWN))   bits |= 0x02u;
        if (held(RETRO_DEVICE_ID_JOYPAD_LEFT))   bits |= 0x04u;
        if (held(RETRO_DEVICE_ID_JOYPAD_RIGHT))  bits |= 0x08u;
        if (held(RETRO_DEVICE_ID_JOYPAD_A))      bits |= 0x10u;
        if (held(RETRO_DEVICE_ID_JOYPAD_B))      bits |= 0x20u;
        if (held(RETRO_DEVICE_ID_JOYPAD_START))  bits |= 0x40u;
        if (held(RETRO_DEVICE_ID_JOYPAD_SELECT)) bits |= 0x80u;
        g_joy_buf[port] = bits;
    }
}

}  // namespace

// Minimal EMU implementation satisfying CSCP VM/FM-7 devices.
EMU::EMU()
    : vm(nullptr),
      osd(new OSD()),
      sound_frequency(6),
      sound_latency(0),
      sound_rate(kSampleRate),
      sound_samples(kSoundSamples),
      cpu_type(CPU_TYPE_DEFAULT),
      dipswitch(0),
      sound_type(1),
      printer_type(0),
      now_suspended(false),
      auto_key_buffer(nullptr),
      auto_key_phase(0),
      auto_key_shift(0),
      shift_pressed(false),
      now_debugging(false),
      debugger_thread_id(0),
      now_waiting_in_debugger(false),
      message_count(0) {
    std::memset(joy_status, 0, sizeof(joy_status));
    std::memset(floppy_disk_status, 0, sizeof(floppy_disk_status));
    std::memset(tape_status, 0, sizeof(tape_status));
    std::memset(message, 0, sizeof(message));
}

EMU::~EMU() {
    delete osd;
    osd = nullptr;
}

const uint8_t* EMU::get_key_buffer() {
    return g_key_buf;
}

const uint32_t* EMU::get_joy_buffer() {
    return g_joy_buf;
}

const int32_t* EMU::get_mouse_buffer() {
    return g_mouse_buf;
}

scrntype_t* EMU::get_screen_buffer(int y) {
    const int cy = std::clamp(y, 0, 399);
    return &g_screen_640x400[static_cast<size_t>(cy) * 640u];
}

void EMU::set_vm_screen_size(int, int, int, int, int, int) {}
void EMU::set_vm_screen_lines(int) {}
void EMU::screen_skip_line(bool) {}
void EMU::lock_vm() {}
void EMU::unlock_vm() {}
void EMU::out_debug_log(const _TCHAR*, ...) {}
void EMU::force_out_debug_log(const _TCHAR*, ...) {}
void EMU::start_waiting_in_debugger() {}
void EMU::process_waiting_in_debugger() {}
void EMU::finish_waiting_in_debugger() {}

RETRO_EXPORT unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

RETRO_EXPORT void retro_get_system_info(struct retro_system_info* info) {
    if (!info) return;
    std::memset(info, 0, sizeof(*info));
#if defined(_FM7)
    info->library_name = "eFM-7 (FUJITSU FM-7)";
#elif defined(_FM77AV)
    info->library_name = "eFM77AV (FUJITSU FM77AV)";
#elif defined(_FM77AV40)
    info->library_name = "eFM77AV40 (FUJITSU FM77AV40)";
#else
    info->library_name = "eFM-7";
#endif
    info->library_version = "2025-09-23";
    info->valid_extensions = "d77|77d|d88|2d|t77|wav|m3u";
    info->need_fullpath = true;
    info->block_extract = false;
}

RETRO_EXPORT void retro_get_system_av_info(struct retro_system_av_info* info) {
    if (!info) return;
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width = 640;
    info->geometry.base_height = 200;
    info->geometry.max_width = 640;
#if defined(_FM77AV40)
    info->geometry.max_height = 400;
#else
    info->geometry.max_height = 200;
#endif
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = FRAMES_PER_SEC;
    info->timing.sample_rate = static_cast<double>(kSampleRate);
}

RETRO_EXPORT void retro_set_environment(retro_environment_t cb) {
    g_env_cb = cb;
    if (!cb) return;

    retro_log_callback log_cb{};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb)) {
        g_log_cb = log_cb.log;
    }
    retro_led_interface led_cb{};
    if (cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led_cb)) {
        g_led_cb = led_cb.set_led_state;
    }
    g_host_supports_ff = cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, nullptr);

    bool no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    static const retro_variable vars[] = {
        {"efm7_boot_mode", "Boot Mode; auto|basic|dos"},
        {"efm7_drive", "Active Swap Drive; FDD0|FDD1|CMT"},
#if defined(_FM7)
        {"efm7_sound_board", "Sound; PSG+OPN|PSG"},
#else
        {"efm7_sound_board", "Sound; OPN|OPN+WHG"},
#endif
        {"efm7_tape_sound", "Tape Sound; disabled|enabled"},
        {"efm7_tape_turbo", "Tape Fast Load; enabled|disabled"},
        {nullptr, nullptr},
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(vars));

    static const retro_disk_control_callback disk_cb{
        disk_set_eject_state,
        disk_get_eject_state,
        disk_get_image_index,
        disk_set_image_index,
        disk_get_num_images,
        disk_replace_image_index,
        disk_add_image_index,
    };
    cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, const_cast<retro_disk_control_callback*>(&disk_cb));
}

RETRO_EXPORT void retro_set_video_refresh(retro_video_refresh_t cb) {
    g_video_cb = cb;
}

RETRO_EXPORT void retro_set_audio_sample(retro_audio_sample_t cb) {
    g_audio_sample_cb = cb;
}

RETRO_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    g_audio_batch_cb = cb;
}

RETRO_EXPORT void retro_set_input_poll(retro_input_poll_t cb) {
    g_input_poll_cb = cb;
}

RETRO_EXPORT void retro_set_input_state(retro_input_state_t cb) {
    g_input_state_cb = cb;
}

RETRO_EXPORT void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_EXPORT void retro_init(void) {
    init_default_config();
}

RETRO_EXPORT void retro_deinit(void) {
    destroy_vm();
}

RETRO_EXPORT void retro_reset(void) {
    clear_key_states();
    const bool has_floppy = !g_fdd_path[0].empty() || !g_fdd_path[1].empty();
    if (g_boot_mode_pref == 1) {
        config.boot_mode = FM7_BOOTMODE_BASIC;
    } else if (g_boot_mode_pref == 2) {
        config.boot_mode = FM7_BOOTMODE_DOS;
    } else {
        config.boot_mode = has_floppy ? FM7_BOOTMODE_DOS : FM7_BOOTMODE_BASIC;
    }
    g_fdd_ejected[0] = false;
    g_fdd_ejected[1] = false;
    g_tape_ejected = false;
    g_resume_tape_on_insert = false;
    if (g_vm) {
        g_vm->close_tape(0);
        g_vm->close_floppy_disk(0);
        g_vm->close_floppy_disk(1);
        g_vm->reset();
        (void)mount_configured_media();
    }
    clear_leds();
    clear_fastforward();
}

RETRO_EXPORT bool retro_load_game(const struct retro_game_info* game) {
    destroy_vm();
    init_default_config();

    const char* sys_dir = nullptr;
    const char* save_dir = nullptr;
    if (g_env_cb) {
        retro_log_callback log_cb{};
        if (g_env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb)) {
            g_log_cb = log_cb.log;
        }
        retro_led_interface led_cb{};
        if (g_env_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led_cb)) {
            g_led_cb = led_cb.set_led_state;
        }
        g_host_supports_ff = g_env_cb(RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE, nullptr);

        g_env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir);
        g_env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);

        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        if (!g_env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
            return false;
        }

        retro_keyboard_callback kb{on_retro_keyboard};
        g_env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
    }

    efm7_set_paths(sys_dir, save_dir);
    apply_core_options();

    const char* media_arg = (game && game->path && game->path[0] != '\0') ? game->path : nullptr;
    if (!parse_initial_media_spec(media_arg)) {
        destroy_vm();
        return false;
    }

    const bool has_floppy = !g_fdd_path[0].empty() || !g_fdd_path[1].empty();
    if (g_boot_mode_pref == 1) {
        config.boot_mode = FM7_BOOTMODE_BASIC;
    } else if (g_boot_mode_pref == 2) {
        config.boot_mode = FM7_BOOTMODE_DOS;
    } else {
        config.boot_mode = has_floppy ? FM7_BOOTMODE_DOS : FM7_BOOTMODE_BASIC;
    }

    g_emu = new EMU();
    g_vm = new VM(g_emu);
    g_vm->initialize_sound(kSampleRate, kSoundSamples);
    for (int ch = 0; ch < USE_SOUND_VOLUME; ++ch) {
        g_vm->set_sound_device_volume(ch, config.sound_volume_l[ch], config.sound_volume_r[ch]);
    }
    g_vm->reset();
    g_vm->close_tape(0);

    if (media_arg && !mount_configured_media()) {
        destroy_vm();
        return false;
    }

    g_last_led_fdd = -1;
    g_last_led_tape = -1;
    update_leds();
    update_fastforward();
    return true;
}

RETRO_EXPORT bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t) {
    return false;
}

RETRO_EXPORT void retro_unload_game(void) {
    destroy_vm();
}

RETRO_EXPORT void retro_run(void) {
    if (!g_vm) return;

    if (g_input_poll_cb) {
        g_input_poll_cb();
    }
    poll_joypads();

    if (g_env_cb) {
        bool updated = false;
        if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
            apply_core_options();
        }
    }

    int av_enable = 3;
    if (g_env_cb) {
        g_env_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable);
    }
    const bool want_video = (av_enable & 1) != 0;
    const bool want_audio = (av_enable & 2) != 0;

    if (!want_audio) {
        g_vm->run();
        (void)g_vm->is_frame_skippable();
    } else {
        int extra_frames = 0;
        uint16_t* pcm = g_vm->create_sound(&extra_frames);
        if (extra_frames == 0) {
            g_vm->run();
        }
        const bool skippable = g_vm->is_frame_skippable();
        if (!g_host_supports_ff && skippable && g_tape_turbo && !config.sound_tape_signal) {
            constexpr int kMaxTurboExtraFrames = 32;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
            for (int i = 0; i < kMaxTurboExtraFrames; ++i) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
                g_vm->run();
                if (!g_vm->is_frame_skippable()) {
                    break;
                }
            }
        }
        if (pcm && g_audio_batch_cb) {
            g_audio_batch_cb(reinterpret_cast<const int16_t*>(pcm), static_cast<size_t>(kSoundSamples));
        }
    }

    if (want_video) {
        g_vm->draw_screen();
        int rw = 640;
        int rh = 200;
        g_vm->get_screen_resolution(&rw, &rh);
        if (g_video_cb) {
            if (rw == 640 && rh == 400) {
                g_video_cb(g_screen_640x400, 640u, 400u, 640u * sizeof(uint16_t));
            } else if (rw == 320 && rh == 200) {
                for (int y = 0; y < 200; ++y) {
                    const uint16_t* src = &g_screen_640x400[static_cast<size_t>(y * 2) * 640u];
                    uint16_t* dst = &g_fb_320x200[static_cast<size_t>(y) * 320u];
                    for (int x = 0; x < 320; ++x) {
                        dst[x] = src[x * 2];
                    }
                }
                g_video_cb(g_fb_320x200, 320u, 200u, 320u * sizeof(uint16_t));
            } else {
                for (int y = 0; y < 200; ++y) {
                    const uint16_t* src = &g_screen_640x400[static_cast<size_t>(y * 2) * 640u];
                    uint16_t* dst = &g_fb_640x200[static_cast<size_t>(y) * 640u];
                    std::memcpy(dst, src, 640u * sizeof(uint16_t));
                }
                g_video_cb(g_fb_640x200, 640u, 200u, 640u * sizeof(uint16_t));
            }
        }
    }
    update_leds();
    update_fastforward();
}

RETRO_EXPORT unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

RETRO_EXPORT size_t retro_serialize_size(void) {
    return 0;
}

RETRO_EXPORT bool retro_serialize(void*, size_t) {
    return false;
}

RETRO_EXPORT bool retro_unserialize(const void*, size_t) {
    return false;
}

RETRO_EXPORT void retro_cheat_reset(void) {}
RETRO_EXPORT void retro_cheat_set(unsigned, bool, const char*) {}

RETRO_EXPORT void* retro_get_memory_data(unsigned) {
    return nullptr;
}

RETRO_EXPORT size_t retro_get_memory_size(unsigned) {
    return 0;
}
