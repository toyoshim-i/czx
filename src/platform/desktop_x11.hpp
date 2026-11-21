#pragma once

#include "platform/platform.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace czx::platform {

struct DesktopX11Context {
    static constexpr int kScale = 3;
    Display* display = nullptr;
    Window window = 0;
    GC gc = nullptr;
    XImage* image = nullptr;
    Atom wm_delete = 0;
    std::vector<uint32_t> xrgb_buf;
    std::vector<czx_key_t> held_keys;
    bool close_requested = false;

    ~DesktopX11Context();
    bool init();
    void present(const uint16_t* rgb565);
    void poll_events(std::vector<czx::input::RawKeyEvent>& out_events);
};

class DesktopX11Display final : public PlatformDisplay {
public:
    explicit DesktopX11Display(std::shared_ptr<DesktopX11Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->display != nullptr; }
    void present(const uint16_t* rgb565) override {
        if (ctx_) ctx_->present(rgb565);
    }
    bool should_close() const override { return ctx_ && ctx_->close_requested; }
    const char* backend_name() const override { return "desktop-x11"; }

private:
    std::shared_ptr<DesktopX11Context> ctx_;
};

class DesktopX11Input final : public PlatformInput {
public:
    explicit DesktopX11Input(std::shared_ptr<DesktopX11Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->display != nullptr; }
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override {
        if (ctx_) ctx_->poll_events(out_events);
    }
    const char* backend_name() const override { return "desktop-x11"; }

private:
    std::shared_ptr<DesktopX11Context> ctx_;
};

}  // namespace czx::platform
