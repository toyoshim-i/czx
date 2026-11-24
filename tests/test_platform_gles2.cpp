#include "platform/gles2_presenter.hpp"
#include "platform/linux_alsa.hpp"
#include "platform/linux_audio.hpp"
#include "platform/linux_gamepad.hpp"
#include "platform/sdl2_gles2.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

int main() {
    // Environment probing helpers must be safe in any container/headless environment.
    (void)czx::platform::has_connected_hdmi_connector();
    (void)czx::platform::has_desktop_session();

    // Uninitialized Gles2Presenter state and cleanup idempotency.
    {
        czx::platform::Gles2Presenter presenter;
        CHECK(presenter.hw_fbo() == 0);
        CHECK(!presenter.hw_ensure_fbo(640, 480, false, false));
        presenter.release_hw_fbo();
        presenter.release_native_texture();
        CHECK(presenter.hw_tex_width() == 0);
        CHECK(presenter.hw_tex_height() == 0);
        CHECK(!presenter.hw_is_rgb565());
        CHECK(presenter.get_proc_address("glClear") == nullptr);
        presenter.cleanup();
        presenter.cleanup();
        CHECK(presenter.hw_fbo() == 0);
    }

    // HW FBO dimension selection prefers active base size before max geometry.
    {
        uint32_t w = 0;
        uint32_t h = 0;
        czx::platform::compute_hw_fbo_dimensions(768, 512, 1024, 1024, w, h);
        CHECK(w == 768 && h == 512);
        czx::platform::compute_hw_fbo_dimensions(256, 256, 1024, 1024, w, h);
        CHECK(w == 256 && h == 256);
        czx::platform::compute_hw_fbo_dimensions(0, 0, 512, 448, w, h);
        CHECK(w == 512 && h == 448);
        czx::platform::compute_hw_fbo_dimensions(0, 0, 0, 0, w, h);
        CHECK(w == 320 && h == 240);
        czx::platform::compute_hw_fbo_dimensions(32, 4096, 0, 0, w, h);
        CHECK(w == 64 && h == 2048);
    }

    // Uninitialized Sdl2Gles2Context and wrapper classes must behave safely without a display server.
    {
        auto ctx = std::make_shared<czx::platform::Sdl2Gles2Context>();
        CHECK(!ctx->make_current());
        CHECK(!ctx->set_vsync_enabled(true));
        ctx->resize_window(800, 600);
        int max_w = 123;
        int max_h = 456;
        CHECK(!ctx->get_desktop_bounds(max_w, max_h));
        CHECK(max_w == 0 && max_h == 0);
        ctx->cleanup();
        ctx->cleanup();

        czx::platform::Sdl2Gles2Display disp(ctx);
        czx::platform::Sdl2Gles2Input input(ctx);
        CHECK(!disp.init());
        CHECK(!input.init());
        CHECK(disp.supports_native_present());
        CHECK(!disp.supports_hw_gles2());
        CHECK(!disp.hw_make_current());
        CHECK(disp.hw_get_proc_address("glClear") == nullptr);
        CHECK(!disp.hw_ensure_fbo(640, 480, false, false));
        CHECK(disp.hw_get_current_framebuffer() == 0);
        CHECK(!disp.is_fullscreen());
        CHECK(!disp.supports_vsync_toggle());
        CHECK(!disp.set_vsync_enabled(true));
        std::vector<czx::input::RawKeyEvent> keys;
        input.poll(keys);
        CHECK(keys.empty());
    }

    // Fullscreen buffer capping helper preserves aspect and aligns to even pixels.
    {
        int w = 0;
        int h = 0;
        CHECK(czx::platform::compute_fullscreen_buffer_size(1920, 1080, 540, w, h));
        CHECK(w == 960 && h == 540);
        CHECK(czx::platform::compute_fullscreen_buffer_size(1280, 720, 540, w, h));
        CHECK(w == 960 && h == 540);
        CHECK(czx::platform::compute_fullscreen_buffer_size(1280, 1024, 540, w, h));
        CHECK(w == 674 && h == 540);
        CHECK(!czx::platform::compute_fullscreen_buffer_size(640, 480, 540, w, h));
        CHECK(w == 640 && h == 480);
        CHECK(!czx::platform::compute_fullscreen_buffer_size(1920, 1080, 0, w, h));
        CHECK(w == 1920 && h == 1080);
        CHECK(czx::platform::compute_fullscreen_buffer_size(0, 0, 540, w, h));
        CHECK(w == 960 && h == 540);
    }

    // PulseAudio/PipeWire HDMI card profile selection chooses active sink profiles cleanly.
    {
        using czx::platform::PulseCardProfileCandidate;
        using czx::platform::select_best_hdmi_card_profile;

        CHECK(select_best_hdmi_card_profile({
                  {"off", 0, 0, true},
                  {"pro-audio", 1, 1, true},
              }) == "pro-audio");

        CHECK(select_best_hdmi_card_profile({
                  {"off", 0, 0, true},
                  {"pro-audio", 1, 1, true},
                  {"output:hdmi-stereo", 1, 5100, true},
              }) == "output:hdmi-stereo");

        CHECK(select_best_hdmi_card_profile({
                  {"off", 0, 0, true},
                  {"output:hdmi-stereo", 1, 5100, false},
                  {"pro-audio", 1, 1, true},
              }) == "pro-audio");

        CHECK(select_best_hdmi_card_profile({
                  {"off", 0, 0, true},
              })
                  .empty());
    }

    // ALSA /proc/asound/cards HDMI device parser finds Cardputer Zero vc4-hdmi and ignores non-HDMI cards.
    {
        using czx::platform::parse_alsa_hdmi_devices_from_proc_cards;

        const auto cz_devs = parse_alsa_hdmi_devices_from_proc_cards(
            " 0 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi\n"
            "                      vc4-hdmi\n"
            " 1 [ES8389Audio    ]: ES8389-Audio - ES8389-Audio\n"
            "                      ES8389-Audio\n");
        CHECK(cz_devs.size() == 2);
        CHECK(cz_devs[0] == "hdmi:CARD=vc4hdmi,DEV=0");
        CHECK(cz_devs[1] == "plughw:CARD=vc4hdmi,DEV=0");

        const auto dual_devs = parse_alsa_hdmi_devices_from_proc_cards(
            " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
            " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1\n");
        CHECK(dual_devs.size() == 4);
        CHECK(dual_devs[0] == "hdmi:CARD=vc4hdmi0,DEV=0");
        CHECK(dual_devs[1] == "plughw:CARD=vc4hdmi0,DEV=0");
        CHECK(dual_devs[2] == "hdmi:CARD=vc4hdmi1,DEV=0");
        CHECK(dual_devs[3] == "plughw:CARD=vc4hdmi1,DEV=0");

        const auto no_hdmi = parse_alsa_hdmi_devices_from_proc_cards(
            " 0 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
            " 1 [ES8389Audio    ]: ES8389-Audio - ES8389-Audio\n");
        CHECK(no_hdmi.empty());
    }

    // USB gamepad mapping (XInput / ATARI Select=UP+DOWN, Start=LEFT+RIGHT) and axis deadzone.
    {
        using namespace czx::platform;
        CHECK(map_gamepad_to_guest_joystick(0u) == 0u);
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_UP | CZX_PAD_SOUTH) == (CZX_JOY_UP | CZX_JOY_FIRE1));
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_RIGHT | CZX_PAD_EAST) == (CZX_JOY_RIGHT | CZX_JOY_FIRE2));
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_WEST | CZX_PAD_NORTH) == (CZX_JOY_FIRE1 | CZX_JOY_FIRE2));
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_SELECT) == (CZX_JOY_UP | CZX_JOY_DOWN));
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_START) == (CZX_JOY_LEFT | CZX_JOY_RIGHT));
        CHECK(map_gamepad_to_guest_joystick(CZX_PAD_META | CZX_PAD_L1 | CZX_PAD_R1) == 0u);

        // XInput signed 16-bit axis (-32768..32767)
        CHECK(eval_gamepad_axis_dir(0, -32768, 32767, 128) == 0);
        CHECK(eval_gamepad_axis_dir(5000, -32768, 32767, 128) == 0);
        CHECK(eval_gamepad_axis_dir(-20000, -32768, 32767, 128) == -1);
        CHECK(eval_gamepad_axis_dir(20000, -32768, 32767, 128) == 1);

        // Generic USB HID unsigned 8-bit axis (0..255)
        CHECK(eval_gamepad_axis_dir(128, 0, 255, 0) == 0);
        CHECK(eval_gamepad_axis_dir(10, 0, 255, 0) == -1);
        CHECK(eval_gamepad_axis_dir(245, 0, 255, 0) == 1);
    }

    std::printf("test_platform_gles2: OK\n");
    return 0;
}
