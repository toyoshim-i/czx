#include "platform/desktop_x11.hpp"
#include "platform/linux_audio.hpp"
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

    auto ctx = std::make_shared<DesktopX11Context>();
    if (!ctx->init()) {
        bundle.display = std::make_unique<NullDisplay>();
        bundle.input = std::make_unique<NullInput>();
    } else {
        bundle.display = std::make_unique<DesktopX11Display>(ctx);
        bundle.input = std::make_unique<DesktopX11Input>(ctx);
    }
    bundle.audio = std::make_unique<LinuxAudio>();
    return bundle;
}

}  // namespace czx::platform
