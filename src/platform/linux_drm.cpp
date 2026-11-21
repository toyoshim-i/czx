#include "platform/linux_drm.hpp"

#include "video/scaler.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drm/drm_fourcc.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace czx::platform {

namespace {

int drm_ioctl(int fd, unsigned long req, void* arg) {
    int r = 0;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

constexpr uint32_t kDrmModeConnected = 1;

}  // namespace

LinuxDrmDisplay::LinuxDrmDisplay(std::string preferred_device)
    : preferred_device_(std::move(preferred_device)) {}

LinuxDrmDisplay::~LinuxDrmDisplay() {
    cleanup();
}

void LinuxDrmDisplay::cleanup() {
    if (present_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(present_mtx_);
            stop_thread_ = true;
        }
        present_cv_.notify_one();
        present_thread_.join();
    }
    has_pending_ = false;
    stop_thread_ = false;

    if (fd_ >= 0 && has_saved_crtc_) {
        saved_crtc_.set_connectors_ptr = reinterpret_cast<uint64_t>(&conn_id_);
        saved_crtc_.count_connectors = 1;
        drm_ioctl(fd_, DRM_IOCTL_MODE_SETCRTC, &saved_crtc_);
        has_saved_crtc_ = false;
    }
    if (map_ && map_ != MAP_FAILED && size_ > 0) {
        munmap(map_, static_cast<size_t>(size_));
        map_ = nullptr;
    }
    if (fd_ >= 0 && fb_id_ != 0) {
        unsigned int id = fb_id_;
        drm_ioctl(fd_, DRM_IOCTL_MODE_RMFB, &id);
        fb_id_ = 0;
    }
    if (fd_ >= 0 && handle_ != 0) {
        drm_mode_destroy_dumb d{};
        d.handle = handle_;
        drm_ioctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        handle_ = 0;
    }
    if (fd_ >= 0) {
        drm_ioctl(fd_, DRM_IOCTL_DROP_MASTER, nullptr);
        close(fd_);
        fd_ = -1;
    }
    prev_frame_.clear();
    pending_frame_.clear();
    upload_frame_.clear();
}

bool LinuxDrmDisplay::open_and_configure(const std::string& path) {
    const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    fd_ = fd;

    uint64_t has_dumb = 0;
    drm_get_cap cap{DRM_CAP_DUMB_BUFFER, 0};
    if (drm_ioctl(fd_, DRM_IOCTL_GET_CAP, &cap) != 0 || cap.value == 0) {
        cleanup();
        return false;
    }
    (void)has_dumb;

    drm_mode_card_res res{};
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0 || res.count_connectors == 0) {
        cleanup();
        return false;
    }

    std::vector<uint32_t> fbs(res.count_fbs);
    std::vector<uint32_t> crtcs(res.count_crtcs);
    std::vector<uint32_t> connectors(res.count_connectors);
    std::vector<uint32_t> encoders(res.count_encoders);

    res.fb_id_ptr = reinterpret_cast<uint64_t>(fbs.data());
    res.crtc_id_ptr = reinterpret_cast<uint64_t>(crtcs.data());
    res.connector_id_ptr = reinterpret_cast<uint64_t>(connectors.data());
    res.encoder_id_ptr = reinterpret_cast<uint64_t>(encoders.data());
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        cleanup();
        return false;
    }

    drm_mode_modeinfo chosen_mode{};
    bool found = false;

    for (uint32_t cid : connectors) {
        drm_mode_get_connector conn{};
        conn.connector_id = cid;
        if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
            continue;
        }
        if (conn.connection != kDrmModeConnected || conn.count_modes == 0) {
            continue;
        }

        std::vector<drm_mode_modeinfo> modes(conn.count_modes);
        std::vector<uint32_t> encs(std::max<uint32_t>(1u, conn.count_encoders));
        std::vector<uint32_t> props(std::max<uint32_t>(1u, conn.count_props));
        std::vector<uint64_t> prop_vals(std::max<uint32_t>(1u, conn.count_props));

        conn.modes_ptr = reinterpret_cast<uint64_t>(modes.data());
        conn.encoders_ptr = reinterpret_cast<uint64_t>(encs.data());
        conn.props_ptr = reinterpret_cast<uint64_t>(props.data());
        conn.prop_values_ptr = reinterpret_cast<uint64_t>(prop_vals.data());

        if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
            continue;
        }

        // Look for native 320x170 panel mode.
        int mode_idx = -1;
        for (uint32_t m = 0; m < conn.count_modes; ++m) {
            if (modes[m].hdisplay == video::kLcdWidth && modes[m].vdisplay == video::kLcdHeight) {
                mode_idx = static_cast<int>(m);
                break;
            }
        }
        if (mode_idx < 0) {
            continue;
        }

        uint32_t resolved_crtc = 0;
        if (conn.encoder_id != 0) {
            drm_mode_get_encoder enc{};
            enc.encoder_id = conn.encoder_id;
            if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id != 0) {
                resolved_crtc = enc.crtc_id;
            }
        }
        if (resolved_crtc == 0) {
            for (uint32_t eid : encs) {
                drm_mode_get_encoder enc{};
                enc.encoder_id = eid;
                if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                    for (uint32_t c = 0; c < crtcs.size(); ++c) {
                        if ((enc.possible_crtcs & (1u << c)) != 0) {
                            resolved_crtc = crtcs[c];
                            break;
                        }
                    }
                }
                if (resolved_crtc != 0) break;
            }
        }
        if (resolved_crtc == 0) {
            continue;
        }

        conn_id_ = cid;
        crtc_id_ = resolved_crtc;
        chosen_mode = modes[static_cast<size_t>(mode_idx)];
        found = true;
        break;
    }

    if (!found) {
        cleanup();
        return false;
    }

    saved_crtc_.crtc_id = crtc_id_;
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_GETCRTC, &saved_crtc_) == 0) {
        has_saved_crtc_ = true;
    }

    drm_mode_create_dumb creq{};
    creq.width = video::kLcdWidth;
    creq.height = video::kLcdHeight;
    creq.bpp = 16;
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &creq) != 0) {
        cleanup();
        return false;
    }
    handle_ = creq.handle;
    pitch_ = creq.pitch;
    size_ = creq.size;

    drm_mode_fb_cmd2 fb_cmd{};
    fb_cmd.width = video::kLcdWidth;
    fb_cmd.height = video::kLcdHeight;
    fb_cmd.pixel_format = DRM_FORMAT_RGB565;
    fb_cmd.handles[0] = handle_;
    fb_cmd.pitches[0] = pitch_;
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_ADDFB2, &fb_cmd) != 0) {
        cleanup();
        return false;
    }
    fb_id_ = fb_cmd.fb_id;

    drm_mode_map_dumb mreq{};
    mreq.handle = handle_;
    if (drm_ioctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        cleanup();
        return false;
    }

    void* mapped = mmap(nullptr, static_cast<size_t>(size_), PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd_, static_cast<off_t>(mreq.offset));
    if (mapped == MAP_FAILED) {
        cleanup();
        return false;
    }
    map_ = static_cast<uint8_t*>(mapped);
    std::memset(map_, 0, static_cast<size_t>(size_));

    drm_mode_crtc set_crtc{};
    set_crtc.crtc_id = crtc_id_;
    set_crtc.fb_id = fb_id_;
    set_crtc.set_connectors_ptr = reinterpret_cast<uint64_t>(&conn_id_);
    set_crtc.count_connectors = 1;
    set_crtc.mode_valid = 1;
    set_crtc.mode = chosen_mode;

    if (drm_ioctl(fd_, DRM_IOCTL_MODE_SETCRTC, &set_crtc) != 0) {
        std::fprintf(stderr, "czx: DRM SETCRTC failed on %s: %s\n", path.c_str(), std::strerror(errno));
        cleanup();
        return false;
    }

    const size_t frame_pixels = static_cast<size_t>(video::kLcdWidth * video::kLcdHeight);
    prev_frame_.assign(frame_pixels, 0);
    pending_frame_.assign(frame_pixels, 0);
    upload_frame_.assign(frame_pixels, 0);
    first_frame_ = true;
    has_pending_ = false;
    stop_thread_ = false;
    present_thread_ = std::thread(&LinuxDrmDisplay::present_loop, this);
    std::fprintf(stderr, "czx: DRM RGB565 active on %s (%ux%u)\n",
                 path.c_str(), chosen_mode.hdisplay, chosen_mode.vdisplay);
    return true;
}

bool LinuxDrmDisplay::init() {
    std::vector<std::string> candidates;
    if (!preferred_device_.empty()) {
        candidates.push_back(preferred_device_);
    } else if (const char* env = std::getenv("CZX_DRM_DEVICE"); env && env[0] != '\0') {
        candidates.emplace_back(env);
    } else {
        candidates.emplace_back("/dev/dri/cardputer-zero-internal");
        candidates.emplace_back("/dev/dri/by-path/platform-3f204000.spi-cs-0-card");
        for (int i = 0; i < 4; ++i) {
            candidates.push_back("/dev/dri/card" + std::to_string(i));
        }
    }

    for (const auto& path : candidates) {
        if (open_and_configure(path)) {
            return true;
        }
    }
    return false;
}

void LinuxDrmDisplay::present(const uint16_t* rgb565) {
    if (!map_ || !rgb565 || !present_thread_.joinable()) return;

    const size_t copy_bytes =
        static_cast<size_t>(video::kLcdWidth * video::kLcdHeight) * sizeof(uint16_t);
    {
        std::lock_guard<std::mutex> lock(present_mtx_);
        std::memcpy(pending_frame_.data(), rgb565, copy_bytes);
        has_pending_ = true;
    }
    present_cv_.notify_one();
}

void LinuxDrmDisplay::present_loop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(present_mtx_);
            present_cv_.wait(lock, [&]() { return stop_thread_ || has_pending_; });
            if (stop_thread_) {
                break;
            }
            upload_frame_.swap(pending_frame_);
            has_pending_ = false;
        }
        flush_frame(upload_frame_.data());
    }
}

void LinuxDrmDisplay::flush_frame(const uint16_t* rgb565) {
    if (!map_ || !rgb565) return;

    int x1 = video::kLcdWidth;
    int y1 = video::kLcdHeight;
    int x2 = 0;
    int y2 = 0;

    if (first_frame_) {
        x1 = 0;
        y1 = 0;
        x2 = video::kLcdWidth;
        y2 = video::kLcdHeight;
        first_frame_ = false;
    } else {
        for (int y = 0; y < video::kLcdHeight; ++y) {
            const uint16_t* cur = rgb565 + y * video::kLcdWidth;
            const uint16_t* prev = prev_frame_.data() + y * video::kLcdWidth;
            for (int x = 0; x < video::kLcdWidth; ++x) {
                if (cur[x] != prev[x]) {
                    if (x < x1) x1 = x;
                    if (x + 1 > x2) x2 = x + 1;
                    if (y < y1) y1 = y;
                    if (y + 1 > y2) y2 = y + 1;
                }
            }
        }
    }

    if (x1 >= x2 || y1 >= y2) {
        return;
    }

    const size_t copy_bytes = static_cast<size_t>(x2 - x1) * sizeof(uint16_t);
    for (int y = y1; y < y2; ++y) {
        const uint16_t* src_row = rgb565 + y * video::kLcdWidth + x1;
        uint16_t* dst_row = reinterpret_cast<uint16_t*>(map_ + static_cast<size_t>(y) * pitch_) + x1;
        uint16_t* prev_row = prev_frame_.data() + y * video::kLcdWidth + x1;
        std::memcpy(dst_row, src_row, copy_bytes);
        std::memcpy(prev_row, src_row, copy_bytes);
    }

    drm_clip_rect clip{};
    clip.x1 = static_cast<unsigned short>(x1);
    clip.y1 = static_cast<unsigned short>(y1);
    clip.x2 = static_cast<unsigned short>(x2);
    clip.y2 = static_cast<unsigned short>(y2);

    drm_mode_fb_dirty_cmd dirty{};
    dirty.fb_id = fb_id_;
    dirty.num_clips = 1;
    dirty.clips_ptr = reinterpret_cast<uint64_t>(&clip);
    drm_ioctl(fd_, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

}  // namespace czx::platform
