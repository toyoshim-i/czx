#pragma once

#include "video/scaler.hpp"

#include <cstdint>
#include <string>

namespace czx::input {
enum class InputMode;
}

namespace czx::video {

struct HudState {
    bool alt_held = false;
    bool panning_active = false;
    uint32_t pan_hint_frames = 0;
    uint32_t toast_frames = 0;
    std::string toast_text;
    DisplayMode display_mode = DisplayMode::OverflowFit;
    int input_mode = 0;  // 0=KBD, 1=JP1, 2=JP2
    bool menu_open = false;
};

// Renders the Alt-held 4..8 bottom icon bar, 1:1 viewport scrollbars, and transient toast.
void render_hud(uint16_t* fb_rgb565, const HudState& hud, const ViewportLayout& layout);

}  // namespace czx::video
