#include "platform/gles2_presenter.hpp"
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

    std::printf("test_platform_gles2: OK\n");
    return 0;
}
