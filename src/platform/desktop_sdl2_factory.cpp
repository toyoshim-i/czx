#include "platform/desktop_sdl2.hpp"
#include "platform/linux_audio.hpp"
#include "platform/linux_gamepad.hpp"
#include "platform/null_devices.hpp"
#include "platform/platform.hpp"
#include "platform/sdl2_gles2.hpp"

#include <cstdlib>
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

    const char* force_sdl2_2d = std::getenv("CZX_FORCE_SDL2_2D");
    if (!force_sdl2_2d || force_sdl2_2d[0] == '\0' || force_sdl2_2d[0] == '0') {
        auto gl_ctx = std::make_shared<Sdl2Gles2Context>();
        if (gl_ctx->init(opts)) {
            bundle.display = std::make_unique<Sdl2Gles2Display>(gl_ctx);
            bundle.input = std::make_unique<GamepadAugmentedInput>(std::make_unique<Sdl2Gles2Input>(gl_ctx));
            bundle.audio = std::make_unique<LinuxAudio>();
            return bundle;
        }
    }

    auto ctx = std::make_shared<DesktopSdl2Context>();
    if (!ctx->init(opts)) {
        bundle.display = std::make_unique<NullDisplay>();
        bundle.input = std::make_unique<NullInput>();
    } else {
        bundle.display = std::make_unique<DesktopSdl2Display>(ctx);
        bundle.input = std::make_unique<GamepadAugmentedInput>(std::make_unique<DesktopSdl2Input>(ctx));
    }
    bundle.audio = std::make_unique<LinuxAudio>();
    return bundle;
}

}  // namespace czx::platform
