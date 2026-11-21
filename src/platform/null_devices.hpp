#pragma once

#include "platform/platform.hpp"

namespace czx::platform {

class NullDisplay final : public PlatformDisplay {
public:
    bool init() override { return true; }
    void present(const uint16_t*) override {}
    const char* backend_name() const override { return "null-display"; }
};

class NullInput final : public PlatformInput {
public:
    bool init() override { return true; }
    void poll(std::vector<czx::input::RawKeyEvent>&) override {}
    const char* backend_name() const override { return "null-input"; }
};

class NullAudio final : public PlatformAudio {
public:
    bool init(uint32_t) override { return true; }
    void write_samples(const int16_t*, size_t) override {}
    const char* backend_name() const override { return "null-audio"; }
};

}  // namespace czx::platform
