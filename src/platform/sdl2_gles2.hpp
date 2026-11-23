#pragma once

#include "platform/gles2_presenter.hpp"
#include "platform/platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace czx::platform {

bool has_connected_hdmi_connector();
bool has_desktop_session();

struct Sdl2Gles2Context {
    static constexpr int kLcdScale = 3;
    static constexpr int kDefaultNativeW = 640;
    static constexpr int kDefaultNativeH = 480;
    static constexpr int kMinWinW = 128;
    static constexpr int kMinWinH = 96;

    struct SdlApi;

    void* dl = nullptr;
    SdlApi* sdl = nullptr;
    void* window = nullptr;
    void* gl_ctx = nullptr;
    bool sdl_video_inited = false;
    bool fullscreen = false;
    bool vsync_enabled = false;
    bool vsync_toggleable = false;
    bool redraw_needed = false;
    bool close_requested = false;
    uint32_t mouse_buttons = 0;
    std::string last_title;
    std::vector<czx_key_t> held_keys;
    std::vector<RawMouseEvent> pending_mouse;
    Gles2Presenter presenter;

    ~Sdl2Gles2Context();
    bool init(const PlatformOptions& opts);
    void cleanup();
    bool make_current() const;
    bool set_vsync_enabled(bool enabled);
    void present(const uint16_t* rgb565);
    void present_native(const NativeFrameView& view);
    void present_hw(const NativeHwFrameView& view);
    void resize_window(int w, int h);
    bool get_desktop_bounds(int& max_w, int& max_h) const;
    void set_window_title(const char* title);
    void poll_events(std::vector<czx::input::RawKeyEvent>& out_events);
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse);
    void drawable_size(int& out_w, int& out_h) const;
};

class Sdl2Gles2Display final : public PlatformDisplay {
public:
    explicit Sdl2Gles2Display(std::shared_ptr<Sdl2Gles2Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->window != nullptr && ctx_->gl_ctx != nullptr; }
    void present(const uint16_t* rgb565) override {
        if (ctx_) ctx_->present(rgb565);
    }
    bool supports_native_present() const override { return true; }
    void present_native(const NativeFrameView& view) override {
        if (ctx_) ctx_->present_native(view);
    }
    void resize_window(int width, int height) override {
        if (ctx_) ctx_->resize_window(width, height);
    }
    bool get_desktop_bounds(int& max_w, int& max_h) const override {
        return ctx_ && ctx_->get_desktop_bounds(max_w, max_h);
    }
    bool consume_redraw_request() override {
        if (!ctx_ || !ctx_->redraw_needed) return false;
        ctx_->redraw_needed = false;
        return true;
    }
    void set_window_title(const char* title) override {
        if (ctx_) ctx_->set_window_title(title);
    }
    bool should_close() const override { return ctx_ && ctx_->close_requested; }
    bool supports_vsync_toggle() const override { return ctx_ && ctx_->vsync_toggleable; }
    bool is_vsync_enabled() const override { return ctx_ && ctx_->vsync_enabled; }
    bool set_vsync_enabled(bool enabled) override { return ctx_ && ctx_->set_vsync_enabled(enabled); }
    bool is_fullscreen() const override { return ctx_ && ctx_->fullscreen; }
    bool supports_hw_gles2() const override { return ctx_ && ctx_->gl_ctx != nullptr; }
    bool hw_make_current() override { return ctx_ && ctx_->make_current(); }
    void* hw_get_proc_address(const char* sym) const override {
        return ctx_ ? ctx_->presenter.get_proc_address(sym) : nullptr;
    }
    bool hw_ensure_fbo(int width, int height, bool depth, bool stencil) override {
        return ctx_ && ctx_->make_current() &&
               ctx_->presenter.hw_ensure_fbo(width, height, depth, stencil);
    }
    uintptr_t hw_get_current_framebuffer() const override {
        return ctx_ ? ctx_->presenter.hw_fbo() : 0;
    }
    void present_hw(const NativeHwFrameView& view) override {
        if (ctx_) ctx_->present_hw(view);
    }
    const char* backend_name() const override {
        return (ctx_ && ctx_->fullscreen) ? "sdl2-gles2-fullscreen" : "sdl2-gles2";
    }

private:
    std::shared_ptr<Sdl2Gles2Context> ctx_;
};

class Sdl2Gles2Input final : public PlatformInput {
public:
    explicit Sdl2Gles2Input(std::shared_ptr<Sdl2Gles2Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->window != nullptr; }
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override {
        if (ctx_) ctx_->poll_events(out_events);
    }
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse) override {
        if (ctx_) ctx_->poll_mouse(out_mouse);
    }
    const char* backend_name() const override { return "sdl2-gles2"; }

private:
    std::shared_ptr<Sdl2Gles2Context> ctx_;
};

}  // namespace czx::platform
