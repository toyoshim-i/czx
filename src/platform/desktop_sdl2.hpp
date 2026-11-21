#pragma once

#include "platform/platform.hpp"

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace czx::platform {

struct DesktopSdl2Context {
    static constexpr int kScale = 3;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool close_requested = false;

    ~DesktopSdl2Context();
    bool init();
    void present(const uint16_t* rgb565);
    void poll_events(std::vector<czx::input::RawKeyEvent>& out_events);
};

class DesktopSdl2Display final : public PlatformDisplay {
public:
    explicit DesktopSdl2Display(std::shared_ptr<DesktopSdl2Context> ctx) : ctx_(std::move(ctx)) {}
    bool init() override { return ctx_ && ctx_->window != nullptr; }
    void present(const uint16_t* rgb565) override {
        if (ctx_) ctx_->present(rgb565);
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
    const char* backend_name() const override { return "desktop-sdl2"; }

private:
    std::shared_ptr<DesktopSdl2Context> ctx_;
};

}  // namespace czx::platform
