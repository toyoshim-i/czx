#include "app/emulator_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options] [media_file]\n"
        "  --core <id|path.so>           Initial core (e.g. applewin, vice_x64sc, vice_x64, test_pattern)\n"
        "  --media <path>                Disk, tape, PRG, or cart image to load\n"
        "  --core-dir <dir>              Additional directory to scan for *_libretro.so cores\n"
        "  --data-dir <dir>              Override CZX data directory (default: $CZX_DATA_DIR or ~/czx)\n"
        "  --mode <pan|overflow|aspect>  Initial display scaling mode (default: overflow)\n"
        "  --preset <0..4>               Initial test pattern preset (default: 0)\n"
        "  --borders                     Show overscan/PAL borders when present\n"
        "  --crt43                       Use 4:3 CRT aspect policy in aspect mode\n"
        "  --margin <auto|black>         Fill letterbox/pillarbox margins (default: auto)\n"
        "  --drm <path>                  Override DRM device path\n"
        "  --fb <path>                   Override fbdev device path\n"
        "  --fbdev                       Prefer fbdev over DRM on device\n"
        "  --input <path>                Override evdev keyboard device path\n"
        "  --headless-frames <N>         Run N frames headless and exit\n"
        "  --verify-nonblank             Exit non-zero if core outputs only blank frames\n"
        "  --help                        Show this help\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    czx::app::AppConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(arg, "--core") == 0 && i + 1 < argc) {
            cfg.core_spec = argv[++i];
        } else if (std::strcmp(arg, "--media") == 0 && i + 1 < argc) {
            cfg.media_path = argv[++i];
        } else if (std::strcmp(arg, "--core-dir") == 0 && i + 1 < argc) {
            cfg.core_dir = argv[++i];
        } else if (std::strcmp(arg, "--data-dir") == 0 && i + 1 < argc) {
            cfg.data_dir = argv[++i];
        } else if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            const char* m = argv[++i];
            if (std::strcmp(m, "pan") == 0) {
                cfg.display_mode = czx::video::DisplayMode::NativePan;
            } else if (std::strcmp(m, "overflow") == 0) {
                cfg.display_mode = czx::video::DisplayMode::OverflowFit;
            } else if (std::strcmp(m, "aspect") == 0) {
                cfg.display_mode = czx::video::DisplayMode::AspectFit;
            } else {
                std::fprintf(stderr, "Unknown mode: %s\n", m);
                return 2;
            }
        } else if (std::strcmp(arg, "--preset") == 0 && i + 1 < argc) {
            cfg.preset_index = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--borders") == 0) {
            cfg.show_borders = true;
        } else if (std::strcmp(arg, "--crt43") == 0) {
            cfg.aspect_policy = czx::video::AspectPolicy::Crt4x3;
        } else if (std::strcmp(arg, "--margin") == 0 && i + 1 < argc) {
            const char* mf = argv[++i];
            if (std::strcmp(mf, "auto") == 0) {
                cfg.margin_fill = czx::video::MarginFill::Auto;
            } else if (std::strcmp(mf, "black") == 0) {
                cfg.margin_fill = czx::video::MarginFill::Black;
            } else {
                std::fprintf(stderr, "Unknown margin mode: %s\n", mf);
                return 2;
            }
        } else if (std::strcmp(arg, "--drm") == 0 && i + 1 < argc) {
            cfg.platform.drm_device = argv[++i];
        } else if (std::strcmp(arg, "--fb") == 0 && i + 1 < argc) {
            cfg.platform.fb_device = argv[++i];
        } else if (std::strcmp(arg, "--fbdev") == 0) {
            cfg.platform.prefer_fbdev = true;
        } else if (std::strcmp(arg, "--input") == 0 && i + 1 < argc) {
            cfg.platform.input_device = argv[++i];
        } else if (std::strcmp(arg, "--headless-frames") == 0 && i + 1 < argc) {
            cfg.headless_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--verify-nonblank") == 0) {
            cfg.verify_nonblank = true;
        } else if (arg[0] != '-' && cfg.media_path.empty()) {
            cfg.media_path = arg;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg);
            print_usage(argv[0]);
            return 2;
        }
    }

    czx::app::EmulatorApp app(cfg);
    return app.run();
}
