#include "czx/libretro.h"

#include "paths.h"
#include "emu.h"
#include "vm/mz700/joystick.h"
#include "vm/mz700/mz700.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifndef RETRO_EXPORT
#define RETRO_EXPORT extern "C" __attribute__((visibility("default")))
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

EMU* g_emu = nullptr;
VM* g_vm = nullptr;

uint8_t g_key_buf[256]{};
uint8_t g_vk_count[256]{};
bool g_retrok_down[RETROK_LAST]{};
uint32_t g_joy_buf[4]{};
scrntype_t g_screen_640x400[640 * 400]{};
uint16_t g_fb_320x200[320 * 200]{};

std::string to_lower(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

void init_default_config() {
    std::memset(&config, 0, sizeof(config));
    config.dipswitch = 0;
    config.joystick_type = DEVICE_JOYSTICK_JOY700;
    config.scan_line = false;
    config.sound_frequency = 6;  // 48000 Hz
    config.sound_strict_rendering = true;
    config.sound_noise_fdd = false;
    config.sound_noise_qd = false;
    config.sound_noise_cmt = false;
    config.sound_tape_signal = false;
    config.sound_tape_voice = false;
    config.wave_shaper[0] = false;
    config.cpu_power = 0;
    for (int i = 0; i < USE_SOUND_VOLUME; ++i) {
        config.sound_volume_l[i] = 0;
        config.sound_volume_r[i] = 0;
    }
}

void apply_core_options() {
    if (!g_env_cb) return;

    retro_variable var{};
    var.key = "emuz700_joystick";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (std::strcmp(var.value, "AM7J") == 0) {
            config.joystick_type = DEVICE_JOYSTICK_AM7J;
        } else if (std::strcmp(var.value, "MZ-1X03") == 0) {
            config.joystick_type = DEVICE_JOYSTICK_1X03;
        } else {
            config.joystick_type = DEVICE_JOYSTICK_JOY700;
        }
    }

    var.key = "emuz700_tape_sound";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        config.sound_tape_signal = (std::strcmp(var.value, "enabled") == 0);
    }

    if (g_vm) {
        g_vm->update_config();
    }
}

int retrok_to_vk(unsigned keycode) {
    if (keycode >= RETROK_a && keycode <= RETROK_z) {
        return 0x41 + static_cast<int>(keycode - RETROK_a);
    }
    if (keycode >= RETROK_0 && keycode <= RETROK_9) {
        return 0x30 + static_cast<int>(keycode - RETROK_0);
    }
    if (keycode >= RETROK_KP0 && keycode <= RETROK_KP9) {
        return 0x30 + static_cast<int>(keycode - RETROK_KP0);
    }
    switch (keycode) {
        case RETROK_RETURN:
        case RETROK_KP_ENTER: return 0x0D;
        case RETROK_SPACE: return 0x20;
        case RETROK_TAB: return 0x09;
        case RETROK_LSHIFT:
        case RETROK_RSHIFT: return 0x10;
        case RETROK_LCTRL:
        case RETROK_RCTRL: return 0x11;
        case RETROK_ESCAPE: return 0x08;  // BREAK
        case RETROK_BACKSPACE:
        case RETROK_DELETE: return 0x2E;  // DEL
        case RETROK_INSERT: return 0x2D;  // INST
        case RETROK_LEFT: return 0x25;
        case RETROK_UP: return 0x26;
        case RETROK_RIGHT: return 0x27;
        case RETROK_DOWN: return 0x28;
        case RETROK_F1: return 0x70;
        case RETROK_F2: return 0x71;
        case RETROK_F3: return 0x72;
        case RETROK_F4: return 0x73;
        case RETROK_F5: return 0x74;
        case RETROK_F6:
        case RETROK_PAGEUP: return 0x21;  // KANA
        case RETROK_F7:
        case RETROK_PAGEDOWN: return 0x22;  // EISUU
        case RETROK_F8:
        case RETROK_LALT: return 0x78;  // GRAPH
        case RETROK_COLON:
        case RETROK_QUOTE: return 0xBA;  // ':'
        case RETROK_SEMICOLON: return 0xBB;  // ';'
        case RETROK_COMMA: return 0xBC;  // ','
        case RETROK_MINUS: return 0xBD;  // '-'
        case RETROK_PERIOD: return 0xBE;  // '.'
        case RETROK_SLASH: return 0xBF;  // '/'
        case RETROK_AT:
        case RETROK_BACKQUOTE: return 0xC0;  // '@'
        case RETROK_LEFTBRACKET: return 0xDB;  // '['
        case RETROK_BACKSLASH: return 0xDC;  // '\'
        case RETROK_RIGHTBRACKET: return 0xDD;  // ']'
        case RETROK_CARET:
        case RETROK_EQUALS: return 0xDE;  // '^'
        case RETROK_UNDERSCORE: return 0xE2;  // '?' / '_'
        default: return 0;
    }
}

void RETRO_CALLCONV on_retro_keyboard(bool down, unsigned keycode, uint32_t, uint16_t) {
    if (keycode >= RETROK_LAST || g_retrok_down[keycode] == down) {
        return;
    }
    g_retrok_down[keycode] = down;

    if (down && keycode == RETROK_KP_PLUS && g_vm && g_vm->is_tape_inserted(0)) {
        if (g_vm->is_tape_playing(0)) {
            g_vm->push_stop(0);
        } else {
            g_vm->push_play(0);
        }
    }
    const int vk = retrok_to_vk(keycode);
    if (vk <= 0 || vk >= 256) return;

    if (down) {
        if (g_vk_count[vk] < 255u) {
            ++g_vk_count[vk];
        }
    } else {
        if (g_vk_count[vk] > 0u) {
            --g_vk_count[vk];
        }
    }
    g_key_buf[vk] = (g_vk_count[vk] > 0u) ? 0x80u : 0x00u;
}

void destroy_vm() {
    if (g_vm) {
        delete g_vm;
        g_vm = nullptr;
    }
    if (g_emu) {
        delete g_emu;
        g_emu = nullptr;
    }
    std::memset(g_key_buf, 0, sizeof(g_key_buf));
    std::memset(g_vk_count, 0, sizeof(g_vk_count));
    std::memset(g_retrok_down, 0, sizeof(g_retrok_down));
    std::memset(g_joy_buf, 0, sizeof(g_joy_buf));
    std::memset(g_screen_640x400, 0, sizeof(g_screen_640x400));
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

// Minimal EMU implementation satisfying CSCP VM devices.
EMU::EMU()
    : vm(nullptr),
      osd(new OSD()),
      sound_frequency(6),
      sound_latency(0),
      sound_rate(kSampleRate),
      sound_samples(kSoundSamples),
      dipswitch(0),
      now_suspended(false),
      now_debugging(false),
      debugger_thread_id(0),
      now_waiting_in_debugger(false),
      message_count(0) {
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

scrntype_t* EMU::get_screen_buffer(int y) {
    const int cy = std::clamp(y, 0, 399);
    return &g_screen_640x400[static_cast<size_t>(cy) * 640u];
}

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
    info->library_name = "EmuZ-700 (SHARP MZ-700)";
    info->library_version = "2025-09-23";
    info->valid_extensions = "mzt|mzf|m12|wav|cas|tap|t77|q20|d88|2d";
    info->need_fullpath = true;
    info->block_extract = false;
}

RETRO_EXPORT void retro_get_system_av_info(struct retro_system_av_info* info) {
    if (!info) return;
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width = 320;
    info->geometry.base_height = 200;
    info->geometry.max_width = 320;
    info->geometry.max_height = 200;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = FRAMES_PER_SEC;
    info->timing.sample_rate = static_cast<double>(kSampleRate);
}

RETRO_EXPORT void retro_set_environment(retro_environment_t cb) {
    g_env_cb = cb;
    if (!cb) return;

    bool no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    static const retro_variable vars[] = {
        {"emuz700_joystick", "Joystick Adapter; JOY-700|AM7J|MZ-1X03"},
        {"emuz700_tape_sound", "Tape Sound; disabled|enabled"},
        {nullptr, nullptr},
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(vars));
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
    std::memset(g_key_buf, 0, sizeof(g_key_buf));
    std::memset(g_vk_count, 0, sizeof(g_vk_count));
    std::memset(g_retrok_down, 0, sizeof(g_retrok_down));
    if (g_vm) {
        g_vm->reset();
    }
}

RETRO_EXPORT bool retro_load_game(const struct retro_game_info* game) {
    destroy_vm();
    init_default_config();

    const char* sys_dir = nullptr;
    const char* save_dir = nullptr;
    if (g_env_cb) {
        g_env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir);
        g_env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir);

        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        if (!g_env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
            return false;
        }

        retro_keyboard_callback kb{on_retro_keyboard};
        g_env_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
    }

    emuz700_set_paths(sys_dir, save_dir);
    apply_core_options();

    g_emu = new EMU();
    g_vm = new VM(g_emu);
    g_vm->initialize_sound(kSampleRate, kSoundSamples);
    for (int ch = 0; ch < USE_SOUND_VOLUME; ++ch) {
        g_vm->set_sound_device_volume(ch, config.sound_volume_l[ch], config.sound_volume_r[ch]);
    }
    g_vm->reset();

    if (game && game->path && game->path[0] != '\0') {
        const std::string ext = to_lower(std::filesystem::path(game->path).extension().string());
        if (ext == ".q20") {
            g_vm->open_quick_disk(0, game->path);
        } else if (ext == ".d88" || ext == ".2d") {
            g_vm->open_floppy_disk(0, game->path, 0);
        } else if (ext == ".mzt" || ext == ".mzf" || ext == ".m12" ||
                   ext == ".wav" || ext == ".cas" || ext == ".tap" || ext == ".t77") {
            g_vm->play_tape(0, game->path);
        } else {
            destroy_vm();
            return false;
        }
    }

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

    int extra_frames = 0;
    uint16_t* pcm = g_vm->create_sound(&extra_frames);
    if (extra_frames == 0) {
        g_vm->run();
    }
    if (pcm && g_audio_batch_cb) {
        g_audio_batch_cb(reinterpret_cast<const int16_t*>(pcm), static_cast<size_t>(kSoundSamples));
    }

    g_vm->draw_screen();
    for (int y = 0; y < 200; ++y) {
        const uint16_t* src = &g_screen_640x400[static_cast<size_t>(y * 2) * 640u];
        uint16_t* dst = &g_fb_320x200[static_cast<size_t>(y) * 320u];
        for (int x = 0; x < 320; ++x) {
            dst[x] = src[x * 2];
        }
    }
    if (g_video_cb) {
        g_video_cb(g_fb_320x200, 320u, 200u, 320u * sizeof(uint16_t));
    }
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
