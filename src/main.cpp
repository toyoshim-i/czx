#include "app/emulator_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int parse_desktop_scale(const char* s) noexcept {
    if (!s) return 0;
    if (std::strcmp(s, "1") == 0 || std::strcmp(s, "1x") == 0 || std::strcmp(s, "1X") == 0) return 1;
    if (std::strcmp(s, "2") == 0 || std::strcmp(s, "2x") == 0 || std::strcmp(s, "2X") == 0) return 2;
    if (std::strcmp(s, "3") == 0 || std::strcmp(s, "3x") == 0 || std::strcmp(s, "3X") == 0) return 3;
    return 0;
}

void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options] [media_file] [media_file2]\n"
        "  --core <id|path.so>           Initial core (auto-detected from media if omitted;\n"
        "                                e.g. applewin, vice_x64, quasi88, np2kai, bluemsx, emuz700, msx68k, g800, test_pattern)\n"
        "  --media <path>                Disk, tape, PRG, or cart image to load (Drive 1 / FDD 0)\n"
        "  --media2 <path>               Second disk image to load (Drive 2 / FDD 1 / HDD)\n"
        "  --core-dir <dir>              Additional directory to scan for *_libretro.so cores\n"
        "  --data-dir <dir>              Override CZX data directory (default: $CZX_DATA_DIR or ~/czx)\n"
        "  --desktop                     Use integer-scaled desktop window mode (default on PC, 2x)\n"
        "  --scale <1|2|3>               Desktop window scale factor (implies --desktop, default: 2)\n"
        "  --hdmi                        Prefer HDMI fullscreen GLES2 output on device\n"
        "  --fullscreen                  Start desktop GLES2 display in fullscreen mode\n"
        "  --lcd, --sim                  Use 320x170 Cardputer Zero LCD simulator display mode\n"
        "  --mode <pan|overflow|aspect>  Initial LCD scaling mode (implies --lcd, default: overflow)\n"
        "  --preset <0..4>               Initial test pattern preset (default: 0)\n"
        "  --borders                     Show overscan/PAL borders when present\n"
        "  --aspect <par|4:3|full>       Aspect ratio policy (default: par)\n"
        "  --crt43                       Use 4:3 CRT aspect policy\n"
        "  --margin <auto|black>         Fill letterbox/pillarbox margins (default: auto)\n"
        "  --activity <on|off>           Show bottom-right tape/disk access indicator (default: on)\n"
        "  --tape-warp <on|off>          Uncapped fast-forward during MZ/C64 tape loads (default: on)\n"
        "  --vsync <on|off>              Enable VSync on desktop SDL2 display (default: on)\n"
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
        } else if (std::strcmp(arg, "--media2") == 0 && i + 1 < argc) {
            cfg.media2_path = argv[++i];
        } else if (std::strcmp(arg, "--core-dir") == 0 && i + 1 < argc) {
            cfg.core_dir = argv[++i];
        } else if (std::strcmp(arg, "--data-dir") == 0 && i + 1 < argc) {
            cfg.data_dir = argv[++i];
        } else if (std::strcmp(arg, "--desktop") == 0) {
            cfg.force_desktop = true;
            cfg.force_lcd_sim = false;
        } else if (std::strcmp(arg, "--hdmi") == 0) {
            cfg.platform.prefer_hdmi = true;
            cfg.platform.force_lcd = false;
            cfg.force_desktop = true;
            cfg.force_lcd_sim = false;
        } else if (std::strcmp(arg, "--fullscreen") == 0) {
            cfg.platform.fullscreen = true;
        } else if (std::strcmp(arg, "--scale") == 0 && i + 1 < argc) {
            const char* sc = argv[++i];
            const int parsed = parse_desktop_scale(sc);
            if (parsed == 0) {
                std::fprintf(stderr, "Unknown scale: %s (expected 1, 2, or 3)\n", sc);
                return 2;
            }
            cfg.desktop_scale = parsed;
            cfg.force_desktop = true;
            cfg.force_lcd_sim = false;
        } else if (std::strcmp(arg, "--lcd") == 0 || std::strcmp(arg, "--sim") == 0) {
            cfg.force_lcd_sim = true;
            cfg.force_desktop = false;
            cfg.platform.force_lcd = true;
        } else if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            const char* m = argv[++i];
            cfg.display_mode_explicit = true;
            cfg.platform.force_lcd = true;
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
        } else if (std::strcmp(arg, "--aspect") == 0 && i + 1 < argc) {
            const char* ap = argv[++i];
            if (std::strcmp(ap, "par") == 0) {
                cfg.aspect_policy = czx::video::AspectPolicy::ParAware;
            } else if (std::strcmp(ap, "4:3") == 0 || std::strcmp(ap, "crt43") == 0) {
                cfg.aspect_policy = czx::video::AspectPolicy::Crt4x3;
            } else if (std::strcmp(ap, "full") == 0 || std::strcmp(ap, "stretch") == 0) {
                cfg.aspect_policy = czx::video::AspectPolicy::FullStretch;
            } else {
                std::fprintf(stderr, "Unknown aspect policy: %s\n", ap);
                return 2;
            }
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
        } else if (std::strcmp(arg, "--activity") == 0 && i + 1 < argc) {
            const char* act = argv[++i];
            if (std::strcmp(act, "on") == 0) {
                cfg.show_activity_indicator = true;
                cfg.activity_indicator_override = true;
            } else if (std::strcmp(act, "off") == 0) {
                cfg.show_activity_indicator = false;
                cfg.activity_indicator_override = true;
            } else {
                std::fprintf(stderr, "Unknown activity mode: %s\n", act);
                return 2;
            }
        } else if (std::strcmp(arg, "--tape-warp") == 0 && i + 1 < argc) {
            const char* tw = argv[++i];
            if (std::strcmp(tw, "on") == 0) {
                cfg.tape_warp_enabled = true;
                cfg.tape_warp_override = true;
            } else if (std::strcmp(tw, "off") == 0) {
                cfg.tape_warp_enabled = false;
                cfg.tape_warp_override = true;
            } else {
                std::fprintf(stderr, "Unknown tape-warp mode: %s\n", tw);
                return 2;
            }
        } else if (std::strcmp(arg, "--vsync") == 0 && i + 1 < argc) {
            const char* vs = argv[++i];
            if (std::strcmp(vs, "on") == 0) {
                cfg.vsync_enabled = true;
                cfg.vsync_override = true;
            } else if (std::strcmp(vs, "off") == 0) {
                cfg.vsync_enabled = false;
                cfg.vsync_override = true;
            } else {
                std::fprintf(stderr, "Unknown vsync mode: %s\n", vs);
                return 2;
            }
        } else if (std::strcmp(arg, "--drm") == 0 && i + 1 < argc) {
            cfg.platform.drm_device = argv[++i];
            cfg.platform.force_lcd = true;
        } else if (std::strcmp(arg, "--fb") == 0 && i + 1 < argc) {
            cfg.platform.fb_device = argv[++i];
            cfg.platform.force_lcd = true;
        } else if (std::strcmp(arg, "--fbdev") == 0) {
            cfg.platform.prefer_fbdev = true;
            cfg.platform.force_lcd = true;
        } else if (std::strcmp(arg, "--input") == 0 && i + 1 < argc) {
            cfg.platform.input_device = argv[++i];
        } else if (std::strcmp(arg, "--headless-frames") == 0 && i + 1 < argc) {
            cfg.headless_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--verify-nonblank") == 0) {
            cfg.verify_nonblank = true;
        } else if (arg[0] != '-' && cfg.media_path.empty()) {
            cfg.media_path = arg;
        } else if (arg[0] != '-' && cfg.media2_path.empty()) {
            cfg.media2_path = arg;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg);
            print_usage(argv[0]);
            return 2;
        }
    }

    czx::app::EmulatorApp app(cfg);
    return app.run();
}
