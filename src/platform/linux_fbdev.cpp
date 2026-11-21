#include "platform/linux_fbdev.hpp"

#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace czx::platform {

LinuxFbdevDisplay::LinuxFbdevDisplay(std::string preferred_device)
    : preferred_device_(std::move(preferred_device)) {}

LinuxFbdevDisplay::~LinuxFbdevDisplay() {
    cleanup();
}

void LinuxFbdevDisplay::cleanup() {
    if (map_ && map_ != MAP_FAILED && map_size_ > 0) {
        munmap(map_, map_size_);
        map_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool LinuxFbdevDisplay::try_open(const std::string& path, bool require_exact_lcd_size) {
    const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    fb_fix_screeninfo finfo{};
    fb_var_screeninfo vinfo{};
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        close(fd);
        return false;
    }

    const bool size_ok = require_exact_lcd_size
                             ? (vinfo.xres == static_cast<uint32_t>(video::kLcdWidth) &&
                                vinfo.yres == static_cast<uint32_t>(video::kLcdHeight))
                             : (vinfo.xres >= static_cast<uint32_t>(video::kLcdWidth) &&
                                vinfo.yres >= static_cast<uint32_t>(video::kLcdHeight));

    if (!size_ok || (vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32)) {
        close(fd);
        return false;
    }

    const size_t map_size = finfo.smem_len ? finfo.smem_len : (finfo.line_length * vinfo.yres_virtual);
    void* mapped = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false;
    }

    fd_ = fd;
    map_ = static_cast<uint8_t*>(mapped);
    map_size_ = map_size;
    stride_ = finfo.line_length;
    bpp_ = vinfo.bits_per_pixel;
    width_ = vinfo.xres;
    height_ = vinfo.yres;
    std::fprintf(stderr, "czx: fbdev active on %s (%ux%u %ubpp)\n",
                 path.c_str(), width_, height_, bpp_);
    return true;
}

bool LinuxFbdevDisplay::init() {
    if (!preferred_device_.empty()) {
        return try_open(preferred_device_, false);
    }
    if (const char* env = std::getenv("CZX_FB_DEVICE"); env && env[0] != '\0') {
        return try_open(env, false);
    }
    if (const char* lv_env = std::getenv("LV_LINUX_FBDEV_DEVICE"); lv_env && lv_env[0] != '\0') {
        return try_open(lv_env, false);
    }

    // Prefer official Cardputer Zero udev alias first, then /proc/fb match, then exact 320x170 scan.
    if (try_open("/dev/fb_lcd", false)) {
        return true;
    }

    std::ifstream proc_fb("/proc/fb");
    std::string line;
    while (std::getline(proc_fb, line)) {
        if (line.find("panel-mipi-dbi") != std::string::npos ||
            line.find("st7789") != std::string::npos) {
            unsigned int fb_idx = 0;
            if (std::sscanf(line.c_str(), "%u", &fb_idx) == 1) {
                if (try_open("/dev/fb" + std::to_string(fb_idx), true)) {
                    return true;
                }
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (try_open("/dev/fb" + std::to_string(i), true)) {
            return true;
        }
    }
    return false;
}

void LinuxFbdevDisplay::present(const uint16_t* rgb565) {
    if (!map_ || !rgb565) return;

    const int copy_w = std::min<int>(video::kLcdWidth, static_cast<int>(width_));
    const int copy_h = std::min<int>(video::kLcdHeight, static_cast<int>(height_));

    if (bpp_ == 16) {
        const size_t row_bytes = static_cast<size_t>(copy_w) * sizeof(uint16_t);
        for (int y = 0; y < copy_h; ++y) {
            std::memcpy(map_ + static_cast<size_t>(y) * stride_,
                        rgb565 + y * video::kLcdWidth,
                        row_bytes);
        }
    } else if (bpp_ == 32) {
        for (int y = 0; y < copy_h; ++y) {
            uint32_t* dst = reinterpret_cast<uint32_t*>(map_ + static_cast<size_t>(y) * stride_);
            const uint16_t* src = rgb565 + y * video::kLcdWidth;
            for (int x = 0; x < copy_w; ++x) {
                uint8_t r = 0, g = 0, b = 0;
                video::unpack_rgb565(src[x], r, g, b);
                dst[x] = (static_cast<uint32_t>(r) << 16) |
                         (static_cast<uint32_t>(g) << 8) |
                         static_cast<uint32_t>(b);
            }
        }
    }
}

}  // namespace czx::platform
