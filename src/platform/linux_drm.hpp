#pragma once

#include "platform/platform.hpp"

#include <condition_variable>
#include <cstdint>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace czx::platform {

class LinuxDrmDisplay : public PlatformDisplay {
public:
    explicit LinuxDrmDisplay(std::string preferred_device = {});
    ~LinuxDrmDisplay() override;

    bool init() override;
    void present(const uint16_t* rgb565) override;
    const char* backend_name() const override { return "linux-drm"; }

private:
    std::string preferred_device_;
    int fd_ = -1;
    uint32_t conn_id_ = 0;
    uint32_t crtc_id_ = 0;
    uint32_t fb_id_ = 0;
    uint32_t handle_ = 0;
    uint32_t pitch_ = 0;
    uint64_t size_ = 0;
    uint8_t* map_ = nullptr;

    drm_mode_crtc saved_crtc_{};
    bool has_saved_crtc_ = false;
    bool first_frame_ = true;
    std::vector<uint16_t> prev_frame_;
    std::vector<uint16_t> pending_frame_;
    std::vector<uint16_t> upload_frame_;
    std::mutex present_mtx_;
    std::condition_variable present_cv_;
    std::thread present_thread_;
    bool has_pending_ = false;
    bool stop_thread_ = false;

    bool open_and_configure(const std::string& path);
    void present_loop();
    void flush_frame(const uint16_t* rgb565);
    void cleanup();
};

}  // namespace czx::platform
