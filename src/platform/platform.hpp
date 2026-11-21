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
};

class PlatformDisplay {
public:
    virtual ~PlatformDisplay() = default;
    virtual bool init() = 0;
    virtual void present(const uint16_t* rgb565) = 0;
    virtual bool should_close() const { return false; }
    virtual const char* backend_name() const = 0;
};

class PlatformInput {
public:
    virtual ~PlatformInput() = default;
    virtual bool init() = 0;
    virtual void poll(std::vector<czx::input::RawKeyEvent>& out_events) = 0;
    virtual const char* backend_name() const = 0;
};

class PlatformAudio {
public:
    virtual ~PlatformAudio() = default;
    virtual bool init(uint32_t sample_rate) = 0;
    virtual void write_samples(const int16_t* stereo_s16, size_t frame_count) = 0;
    virtual const char* backend_name() const = 0;
};

struct PlatformBundle {
    std::unique_ptr<PlatformDisplay> display;
    std::unique_ptr<PlatformInput> input;
    std::unique_ptr<PlatformAudio> audio;
};

PlatformBundle create_platform_bundle(const PlatformOptions& opts);

}  // namespace czx::platform
