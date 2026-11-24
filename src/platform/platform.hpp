#pragma once

#include "czx/core.h"
#include "input/keycodes.hpp"
#include "video/scaler.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace czx::platform {

struct PlatformOptions {
    std::string drm_device;
    std::string fb_device;
    std::string input_device;
    bool prefer_fbdev = false;
    bool headless = false;
    bool desktop_native = true;
    bool vsync = true;
    bool prefer_hdmi = false;
    bool force_lcd = false;
    bool fullscreen = false;
    int max_fullscreen_height = 0;  // 0 = native desktop resolution; >0 caps fullscreen EGL buffer height for low-CMA GPUs
    bool prefer_rgb565 = false;     // Request 16-bit RGB565 window surface when supported
};

struct NativeFrameView {
    const uint32_t* xrgb = nullptr;
    int width = 0;
    int height = 0;
    int par_num = 1;
    int par_den = 1;
    video::AspectPolicy aspect_policy = video::AspectPolicy::ParAware;
    double display_dar = 0.0;  // 0.0 = square pixels (out_w/out_h); >0 = minimum physical display DAR (e.g. 16/9)
    uint32_t margin_xrgb = 0;
    const uint32_t* overlay_argb = nullptr;  // 320x170 straight ARGB8888, or nullptr
    bool dim_outside_overlay = false;
};

struct NativeHwFrameView {
    int width = 0;
    int height = 0;
    int par_num = 1;
    int par_den = 1;
    video::AspectPolicy aspect_policy = video::AspectPolicy::ParAware;
    double display_dar = 0.0;  // 0.0 = square pixels (out_w/out_h); >0 = minimum physical display DAR (e.g. 16/9)
    bool bottom_left_origin = false;
    uint32_t margin_xrgb = 0;
    const uint32_t* overlay_argb = nullptr;  // 320x170 straight ARGB8888, or nullptr
    bool dim_outside_overlay = false;
};

struct RawMouseEvent {
    int32_t dx = 0;
    int32_t dy = 0;
    uint32_t buttons = 0;
};

enum : uint32_t {
    CZX_PAD_UP     = 1u << 0,
    CZX_PAD_DOWN   = 1u << 1,
    CZX_PAD_LEFT   = 1u << 2,
    CZX_PAD_RIGHT  = 1u << 3,
    CZX_PAD_SOUTH  = 1u << 4,   // A (XInput) / Cross -> FIRE1 / UI Confirm
    CZX_PAD_EAST   = 1u << 5,   // B (XInput) / Circle -> FIRE2 / UI Back
    CZX_PAD_WEST   = 1u << 6,   // X (XInput) / Square -> FIRE1 / Browser Up
    CZX_PAD_NORTH  = 1u << 7,   // Y (XInput) / Triangle -> FIRE2
    CZX_PAD_SELECT = 1u << 8,   // Back/View/Select -> ATARI UP+DOWN
    CZX_PAD_START  = 1u << 9,   // Start/Menu -> ATARI LEFT+RIGHT / UI Confirm
    CZX_PAD_META   = 1u << 10,  // Guide/Xbox/PS/Home (BTN_MODE) -> Toggle Menu
    CZX_PAD_L1     = 1u << 11,  // LB / L1
    CZX_PAD_R1     = 1u << 12,  // RB / R1
};

struct GamepadState {
    uint32_t port = 0;     // 0 = P1, 1 = P2
    uint32_t buttons = 0;  // CZX_PAD_* bitmask
};

// Maps CZX_PAD_* gamepad state into guest CZX_JOY_* bits, including ATARI
// opposite-direction conventions (SELECT -> UP+DOWN, START -> LEFT+RIGHT).
inline uint32_t map_gamepad_to_guest_joystick(uint32_t pad_buttons) noexcept {
    uint32_t joy = 0u;
    if ((pad_buttons & CZX_PAD_UP) != 0u) joy |= CZX_JOY_UP;
    if ((pad_buttons & CZX_PAD_DOWN) != 0u) joy |= CZX_JOY_DOWN;
    if ((pad_buttons & CZX_PAD_LEFT) != 0u) joy |= CZX_JOY_LEFT;
    if ((pad_buttons & CZX_PAD_RIGHT) != 0u) joy |= CZX_JOY_RIGHT;
    if ((pad_buttons & (CZX_PAD_SOUTH | CZX_PAD_WEST)) != 0u) joy |= CZX_JOY_FIRE1;
    if ((pad_buttons & (CZX_PAD_EAST | CZX_PAD_NORTH)) != 0u) joy |= CZX_JOY_FIRE2;
    if ((pad_buttons & CZX_PAD_SELECT) != 0u) joy |= (CZX_JOY_UP | CZX_JOY_DOWN);
    if ((pad_buttons & CZX_PAD_START) != 0u) joy |= (CZX_JOY_LEFT | CZX_JOY_RIGHT);
    return joy;
}

class PlatformDisplay {
public:
    virtual ~PlatformDisplay() = default;
    virtual bool init() = 0;
    virtual void present(const uint16_t* rgb565) = 0;
    virtual bool supports_native_present() const { return false; }
    virtual void present_native(const NativeFrameView&) {}
    virtual void resize_window(int /*width*/, int /*height*/) {}
    virtual bool get_desktop_bounds(int& max_w, int& max_h) const {
        max_w = 0;
        max_h = 0;
        return false;
    }
    virtual bool consume_redraw_request() { return false; }
    virtual void set_window_title(const char*) {}
    virtual bool should_close() const { return false; }
    virtual bool supports_vsync_toggle() const { return false; }
    virtual bool is_vsync_enabled() const { return false; }
    virtual bool set_vsync_enabled(bool /*enabled*/) { return false; }
    virtual bool is_fullscreen() const { return false; }
    virtual bool supports_hw_gles2() const { return false; }
    virtual bool hw_make_current() { return false; }
    virtual void* hw_get_proc_address(const char* /*sym*/) const { return nullptr; }
    virtual bool hw_ensure_fbo(int /*width*/, int /*height*/, bool /*depth*/, bool /*stencil*/) { return false; }
    virtual void hw_release_fbo() {}
    virtual uintptr_t hw_get_current_framebuffer() const { return 0; }
    virtual void present_hw(const NativeHwFrameView&) {}
    virtual const char* backend_name() const = 0;
};

class PlatformInput {
public:
    virtual ~PlatformInput() = default;
    virtual bool init() = 0;
    virtual void poll(std::vector<czx::input::RawKeyEvent>& out_events) = 0;
    virtual void poll_mouse(std::vector<RawMouseEvent>&) {}
    virtual void poll_gamepads(std::vector<GamepadState>&) {}
    virtual const char* backend_name() const = 0;
};

struct SystemVolumeState {
    int percent = -1;
    bool muted = false;
};

class PlatformAudio {
public:
    virtual ~PlatformAudio() = default;
    virtual bool init(uint32_t sample_rate) = 0;
    virtual void write_samples(const int16_t* stereo_s16, size_t frame_count) = 0;
    // Returns estimated playback buffer fill ratio in [0.0, 1.0], or < 0.0 if unavailable.
    virtual double get_buffer_fill_ratio() const { return -1.0; }
    virtual const char* backend_name() const = 0;
    virtual bool get_system_volume(SystemVolumeState& /*out*/) { return false; }
    virtual bool adjust_system_volume(int /*delta_percent*/, SystemVolumeState& /*out*/) { return false; }
    virtual bool toggle_system_mute(SystemVolumeState& /*out*/) { return false; }
};

struct PlatformBundle {
    std::unique_ptr<PlatformDisplay> display;
    std::unique_ptr<PlatformInput> input;
    std::unique_ptr<PlatformAudio> audio;
};

PlatformBundle create_platform_bundle(const PlatformOptions& opts);

}  // namespace czx::platform
