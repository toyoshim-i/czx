#pragma once

#include "platform/platform.hpp"

#include <cstdint>
#include <string>

namespace czx::platform {

class LinuxFbdevDisplay : public PlatformDisplay {
public:
    explicit LinuxFbdevDisplay(std::string preferred_device = {});
    ~LinuxFbdevDisplay() override;

    bool init() override;
    void present(const uint16_t* rgb565) override;
    const char* backend_name() const override { return "linux-fbdev"; }

private:
    std::string preferred_device_;
    int fd_ = -1;
    uint8_t* map_ = nullptr;
    size_t map_size_ = 0;
    uint32_t stride_ = 0;
    uint32_t bpp_ = 16;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    bool try_open(const std::string& path, bool require_exact_lcd_size);
    void cleanup();
};

}  // namespace czx::platform
