#include "platform/linux_alsa.hpp"
#include "platform/linux_drm.hpp"
#include "platform/linux_evdev.hpp"
#include "platform/linux_fbdev.hpp"
#include "platform/null_devices.hpp"
#include "platform/platform.hpp"

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

    bundle.audio = std::make_unique<LinuxAlsaAudio>();
    return bundle;
}

}  // namespace czx::platform
