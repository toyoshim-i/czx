#include "czx/libretro.h"

#include "paths.h"
#include "emu.h"
#include "vm/device.h"
#include "vm/mz700/joystick.h"
#include "vm/mz700/mz700.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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
retro_log_printf_t g_log_cb = nullptr;
retro_set_led_state_t g_led_cb = nullptr;

EMU* g_emu = nullptr;
VM* g_vm = nullptr;

bool g_tape_turbo = true;
bool g_host_supports_ff = false;
bool g_ff_active = false;
bool g_ejected = false;
bool g_resume_tape_on_insert = false;
std::string g_media_path;
int g_last_led_fdd = -1;
int g_last_led_tape = -1;
int g_last_led_qd = -1;

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

__attribute__((format(printf, 2, 3)))
void log_msg(enum retro_log_level level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log_cb) {
        g_log_cb(level, "emuz700: %s\n", buf);
    } else if (level >= RETRO_LOG_WARN) {
        std::fprintf(stderr, "[emuz700] %s\n", buf);
    }
}

void clear_leds() {
    if (g_led_cb) {
        g_led_cb(1, 0);
        g_led_cb(2, 0);
        g_led_cb(3, 0);
    }
    g_last_led_fdd = 0;
    g_last_led_tape = 0;
    g_last_led_qd = 0;
}

void update_leds() {
    if (!g_led_cb || !g_vm) return;

    const int fdd = static_cast<int>(g_vm->is_floppy_disk_accessed() & 0x0Fu);
    const int qd = static_cast<int>(g_vm->is_quick_disk_accessed() & 0x0Fu);
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
    if (qd != g_last_led_qd) {
        g_led_cb(3, qd);
        g_last_led_qd = qd;
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

void prime_cmt_idle_state() {
    if (!g_vm) return;
    for (int id = 0; id < 64; ++id) {
        DEVICE* dev = g_vm->get_device(id);
        if (!dev) break;
        const char* name = dev->get_device_name();
        if (name && std::strcmp(name, "8255 PIO") == 0) {
            // Real MZ-700 idles PC3 (M-ON) HIGH and PC2 (INTMASK) HIGH while
            // the CMT motor starts OFF. Priming Port C = 0x0C sets
            // DATAREC::trigger = true before mounting media so 1Z-009's
            // initial PC3=1 write at cold boot does not fire a spurious
            // L->H edge that starts the tape at the '*' monitor prompt.
            dev->write_io8(2, 0x0C);
            break;
        }
    }
    g_vm->push_stop(0);
}

void init_default_config() {
    std::memset(&config, 0, sizeof(config));
    config.dipswitch = 0;
    config.joystick_type = DEVICE_JOYSTICK_JOY700;
    config.scan_line = false;
    config.correct_disk_timing[0] = true;
    config.correct_disk_timing[1] = true;
    config.wave_shaper[0] = true;
    config.direct_load_mzt[0] = true;
    config.baud_high[0] = true;
    config.drive_vm_in_opecode = true;
    config.sound_frequency = 6;  // 48000 Hz
    config.sound_strict_rendering = false;
    config.sound_noise_fdd = false;
    config.sound_noise_qd = false;
    config.sound_noise_cmt = false;
    config.sound_tape_signal = false;
    config.sound_tape_voice = false;
    config.cpu_power = 0;
    for (int i = 0; i < USE_SOUND_VOLUME; ++i) {
        config.sound_volume_l[i] = 0;
        config.sound_volume_r[i] = 0;
    }
    g_tape_turbo = true;
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

    var.key = "emuz700_tape_turbo";
    var.value = nullptr;
    if (g_env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        g_tape_turbo = (std::strcmp(var.value, "disabled") != 0);
    }

    if (g_vm) {
        g_vm->update_config();
    }
}

bool is_supported_media_ext(const char* path) {
    if (!path || path[0] == '\0') return false;
    const std::string ext = to_lower(std::filesystem::path(path).extension().string());
    return ext == ".q20" || ext == ".d88" || ext == ".2d" ||
           ext == ".mzt" || ext == ".mzf" || ext == ".m12" ||
           ext == ".wav" || ext == ".cas" || ext == ".tap" || ext == ".t77";
}

bool mount_media(const char* path) {
    if (!g_vm || !path || path[0] == '\0') return false;
    const std::string ext = to_lower(std::filesystem::path(path).extension().string());
    if (ext == ".q20") {
        g_vm->open_quick_disk(0, path);
        const bool ok = g_vm->is_quick_disk_inserted(0);
        log_msg(ok ? RETRO_LOG_INFO : RETRO_LOG_ERROR, "mount QD (%s): %s", ok ? "ok" : "failed", path);
        return ok;
    }
    if (ext == ".d88" || ext == ".2d") {
        g_vm->open_floppy_disk(0, path, 0);
        const bool ok = g_vm->is_floppy_disk_inserted(0);
        log_msg(ok ? RETRO_LOG_INFO : RETRO_LOG_ERROR, "mount FDD (%s): %s", ok ? "ok" : "failed", path);
        return ok;
    }
    if (ext == ".mzt" || ext == ".mzf" || ext == ".m12" ||
        ext == ".wav" || ext == ".cas" || ext == ".tap" || ext == ".t77") {
        g_vm->play_tape(0, path);
        const bool ok = g_vm->is_tape_inserted(0);
        log_msg(ok ? RETRO_LOG_INFO : RETRO_LOG_ERROR, "mount CMT (%s): %s", ok ? "ok" : "failed", path);
        return ok;
    }
    log_msg(RETRO_LOG_ERROR, "unsupported media extension '%s': %s", ext.c_str(), path);
    return false;
}

bool RETRO_CALLCONV disk_set_eject_state(bool ejected) {
    if (!g_vm) return false;
    if (ejected == g_ejected) return true;
    if (ejected) {
        g_resume_tape_on_insert = g_vm->is_tape_playing(0);
        g_vm->close_tape(0);
        g_vm->close_quick_disk(0);
        g_vm->close_floppy_disk(0);
        g_ejected = true;
        update_leds();
        update_fastforward();
        return true;
    }
    g_ejected = false;
    bool ok = true;
    if (!g_media_path.empty()) {
        ok = mount_media(g_media_path.c_str());
        if (ok && g_resume_tape_on_insert && g_vm->is_tape_inserted(0)) {
            g_vm->push_play(0);
        }
    }
    g_resume_tape_on_insert = false;
    update_leds();
    update_fastforward();
    return ok;
}

bool RETRO_CALLCONV disk_get_eject_state(void) {
    return g_ejected;
}

unsigned RETRO_CALLCONV disk_get_image_index(void) {
    return 0u;
}

bool RETRO_CALLCONV disk_set_image_index(unsigned index) {
    return index == 0u;
}

unsigned RETRO_CALLCONV disk_get_num_images(void) {
    return g_media_path.empty() ? 0u : 1u;
}

bool RETRO_CALLCONV disk_replace_image_index(unsigned index, const struct retro_game_info* info) {
    if (index != 0u || !g_ejected) return false;
    if (!info || !info->path || info->path[0] == '\0') {
        g_media_path.clear();
        return true;
    }
    std::error_code ec;
    if (!is_supported_media_ext(info->path) || !std::filesystem::is_regular_file(info->path, ec)) {
        log_msg(RETRO_LOG_ERROR, "cannot replace media with '%s'", info->path);
        return false;
    }
    g_media_path = info->path;
    return true;
}

bool RETRO_CALLCONV disk_add_image_index(void) {
    return true;
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

    if (down && keycode == RETROK_KP_PLUS && g_vm) {
        if (g_vm->is_tape_playing(0)) {
            g_vm->push_stop(0);
            log_msg(RETRO_LOG_INFO, "tape: manual stop");
        } else {
            if ((!g_vm->is_tape_inserted(0) || g_vm->get_tape_position(0) >= 99) && !g_media_path.empty()) {
                (void)mount_media(g_media_path.c_str());
            }
            if (g_vm->is_tape_inserted(0)) {
                g_vm->push_play(0);
                log_msg(RETRO_LOG_INFO, "tape: manual play");
            }
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
    clear_fastforward();
    clear_leds();
    g_ejected = false;
    g_resume_tape_on_insert = false;
    g_media_path.clear();
    g_last_led_fdd = -1;
    g_last_led_tape = -1;
    g_last_led_qd = -1;
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
        {"emuz700_joystick", "Joystick Adapter; JOY-700|AM7J|MZ-1X03"},
        {"emuz700_tape_sound", "Tape Sound; disabled|enabled"},
        {"emuz700_tape_turbo", "Tape Fast Load; enabled|disabled"},
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
    std::memset(g_key_buf, 0, sizeof(g_key_buf));
    std::memset(g_vk_count, 0, sizeof(g_vk_count));
    std::memset(g_retrok_down, 0, sizeof(g_retrok_down));
    g_ejected = false;
    g_resume_tape_on_insert = false;
    if (g_vm) {
        g_vm->reset();
        g_vm->close_tape(0);
        prime_cmt_idle_state();
        if (!g_media_path.empty()) {
            (void)mount_media(g_media_path.c_str());
        }
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

    emuz700_set_paths(sys_dir, save_dir);
    apply_core_options();

    g_emu = new EMU();
    g_vm = new VM(g_emu);
    g_vm->initialize_sound(kSampleRate, kSoundSamples);
    for (int ch = 0; ch < USE_SOUND_VOLUME; ++ch) {
        g_vm->set_sound_device_volume(ch, config.sound_volume_l[ch], config.sound_volume_r[ch]);
    }
    g_vm->reset();
    g_vm->close_tape(0);
    prime_cmt_idle_state();

    if (game && game->path && game->path[0] != '\0') {
        if (!mount_media(game->path)) {
            destroy_vm();
            return false;
        }
        g_media_path = game->path;
    }

    g_last_led_fdd = -1;
    g_last_led_tape = -1;
    g_last_led_qd = -1;
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
