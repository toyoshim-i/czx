#include "platform/linux_audio.hpp"
#include "platform/linux_drm.hpp"
#include "platform/linux_evdev.hpp"
#include "platform/linux_fbdev.hpp"
#include "platform/null_devices.hpp"
#include "platform/platform.hpp"
#include "platform/sdl2_gles2.hpp"

#include <cstdio>
#include <memory>

namespace czx::platform {

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
        auto ctx = std::make_shared<Sdl2Gles2Context>();
        if (ctx->init(hdmi_opts)) {
            std::fprintf(stderr, "czx: HDMI fullscreen GLES2 active via SDL2\n");
            bundle.display = std::make_unique<Sdl2Gles2Display>(ctx);
            bundle.input = std::make_unique<Sdl2Gles2Input>(ctx);
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
