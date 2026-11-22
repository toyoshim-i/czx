#pragma once

#include "platform/platform.hpp"

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace czx::platform {

struct DesktopSdl2Context {
    static constexpr int kLcdScale = 3;
    static constexpr int kDefaultNativeW = 640;
    static constexpr int kDefaultNativeH = 480;
    static constexpr int kMinWinW = 128;
    static constexpr int kMinWinH = 96;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_Texture* native_texture = nullptr;
    SDL_Texture* overlay_texture = nullptr;
    int native_tex_w = 0;
    int native_tex_h = 0;
    bool redraw_needed = false;
    uint32_t mouse_buttons = 0;
    std::string last_title;
    std::vector<RawMouseEvent> pending_mouse;
    bool close_requested = false;

    ~DesktopSdl2Context();
    bool init(const PlatformOptions& opts);
    void present(const uint16_t* rgb565);
    void present_native(const NativeFrameView& view);
    void resize_window(int w, int h);
    bool get_desktop_bounds(int& max_w, int& max_h) const;
    void set_window_title(const char* title);
    void poll_events(std::vector<czx::input::RawKeyEvent>& out_events);
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse);
};

class DesktopSdl2Display final : public PlatformDisplay {
public:
    explicit DesktopSdl2Display(std::shared_ptr<DesktopSdl2Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->window != nullptr; }
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
    const char* backend_name() const override { return "desktop-sdl2"; }

private:
    std::shared_ptr<DesktopSdl2Context> ctx_;
};

class DesktopSdl2Input final : public PlatformInput {
public:
    explicit DesktopSdl2Input(std::shared_ptr<DesktopSdl2Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->window != nullptr; }
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override {
        if (ctx_) ctx_->poll_events(out_events);
    }
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse) override {
        if (ctx_) ctx_->poll_mouse(out_mouse);
    }
    const char* backend_name() const override { return "desktop-sdl2"; }

private:
    std::shared_ptr<DesktopSdl2Context> ctx_;
};

}  // namespace czx::platform
