#include "platform/linux_audio.hpp"
#include "platform/linux_drm.hpp"
#include "platform/linux_evdev.hpp"
#include "platform/linux_fbdev.hpp"
#include "platform/null_devices.hpp"
#include "platform/platform.hpp"
#include "platform/sdl2_gles2.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace czx::platform {

namespace {

class CompositeDeviceInput final : public PlatformInput {
public:
    CompositeDeviceInput(std::unique_ptr<PlatformInput> sdl, std::unique_ptr<PlatformInput> evdev)
        : sdl_(std::move(sdl)), evdev_(std::move(evdev)) {}

    bool init() override { return sdl_ || evdev_; }

    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override {
        auto collect = [&](PlatformInput* dev, uint8_t src) {
            if (!dev) return;
            tmp_events_.clear();
            dev->poll(tmp_events_);
            for (const auto& ev : tmp_events_) {
                const size_t idx = static_cast<size_t>(ev.key);
                if (idx == 0 || idx >= key_owner_.size()) continue;
                if (ev.pressed) {
                    if (key_owner_[idx] == 0) {
                        key_owner_[idx] = src;
                        out_events.push_back(ev);
                    } else if (ev.repeat && key_owner_[idx] == src) {
                        out_events.push_back(ev);
                    }
                } else if (key_owner_[idx] == src) {
                    key_owner_[idx] = 0;
                    out_events.push_back(ev);
                }
            }
        };
        collect(sdl_.get(), 1);
        collect(evdev_.get(), 2);
    }

    void poll_mouse(std::vector<RawMouseEvent>& out_mouse) override {
        if (sdl_) {
            sdl_->poll_mouse(out_mouse);
        }
    }

    const char* backend_name() const override {
        return (sdl_ && evdev_) ? "sdl2-gles2+evdev"
                                : (sdl_ ? sdl_->backend_name() : "linux-evdev");
    }

private:
    std::unique_ptr<PlatformInput> sdl_;
    std::unique_ptr<PlatformInput> evdev_;
    std::vector<czx::input::RawKeyEvent> tmp_events_;
    std::array<uint8_t, 512> key_owner_{};
};

int resolve_hdmi_max_height() noexcept {
    int max_h = 540;
    if (const char* env_h = std::getenv("CZX_HDMI_MAX_HEIGHT"); env_h && env_h[0] != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(env_h, &end, 10);
        if (end && end != env_h) {
            max_h = (parsed <= 0) ? 0 : static_cast<int>(std::clamp(parsed, 240L, 2160L));
        }
    }
    return max_h;
}

}  // namespace

PlatformBundle create_platform_bundle(const PlatformOptions& opts) {
    PlatformBundle bundle;
    if (opts.headless) {
        bundle.display = std::make_unique<NullDisplay>();
        bundle.input = std::make_unique<NullInput>();
        bundle.audio = std::make_unique<NullAudio>();
        return bundle;
    }

    if (!opts.force_lcd &&
        (opts.prefer_hdmi || (has_connected_hdmi_connector() && has_desktop_session()))) {
        PlatformOptions hdmi_opts = opts;
        hdmi_opts.fullscreen = true;
        hdmi_opts.desktop_native = true;
        hdmi_opts.prefer_rgb565 = true;
        hdmi_opts.max_fullscreen_height = resolve_hdmi_max_height();
        auto ctx = std::make_shared<Sdl2Gles2Context>();
        if (ctx->init(hdmi_opts)) {
            std::fprintf(stderr, "czx: HDMI fullscreen GLES2 active via SDL2\n");
            bundle.display = std::make_unique<Sdl2Gles2Display>(ctx);
            auto sdl_input = std::make_unique<Sdl2Gles2Input>(ctx);
            auto ev_input = std::make_unique<LinuxEvdevInput>(opts.input_device);
            if (ev_input->init()) {
                bundle.input = std::make_unique<CompositeDeviceInput>(
                    std::move(sdl_input), std::move(ev_input));
            } else {
                bundle.input = std::move(sdl_input);
            }
            bundle.audio = std::make_unique<LinuxAudio>();
            return bundle;
        }
        std::fprintf(stderr, "czx: HDMI SDL2 GLES2 unavailable, falling back to internal LCD\n");
    }

    if (!opts.prefer_fbdev) {
        auto drm = std::make_unique<LinuxDrmDisplay>(opts.drm_device);
        if (drm->init()) {
            bundle.display = std::move(drm);
        }
    }

    if (!bundle.display) {
        auto fb = std::make_unique<LinuxFbdevDisplay>(opts.fb_device);
        if (fb->init()) {
            bundle.display = std::move(fb);
        }
    }

    if (!bundle.display) {
        bundle.display = std::make_unique<NullDisplay>();
    }

    auto ev = std::make_unique<LinuxEvdevInput>(opts.input_device);
    ev->init();
    bundle.input = std::move(ev);

    bundle.audio = std::make_unique<LinuxAudio>();
    return bundle;
}

}  // namespace czx::platform
