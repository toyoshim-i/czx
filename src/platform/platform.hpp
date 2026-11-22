#pragma once

#include "input/keycodes.hpp"

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
};

struct NativeFrameView {
    const uint32_t* xrgb = nullptr;
    int width = 0;
    int height = 0;
    int par_num = 1;
    int par_den = 1;
    bool crt_4x3 = false;
    uint32_t margin_xrgb = 0;
    const uint32_t* overlay_argb = nullptr;  // 320x170 straight ARGB8888, or nullptr
    bool dim_outside_overlay = false;
};

struct RawMouseEvent {
    int32_t dx = 0;
    int32_t dy = 0;
    uint32_t buttons = 0;
};

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
    virtual const char* backend_name() const = 0;
};

class PlatformInput {
public:
    virtual ~PlatformInput() = default;
    virtual bool init() = 0;
    virtual void poll(std::vector<czx::input::RawKeyEvent>& out_events) = 0;
    virtual void poll_mouse(std::vector<RawMouseEvent>&) {}
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
