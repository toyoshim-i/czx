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
    bool force_alt_bar = false;
    bool panning_active = false;
    uint32_t pan_hint_frames = 0;
    uint32_t toast_frames = 0;
    std::string toast_text;
    int fps_x10 = 0;  // Tenths of FPS (e.g. 599 = 59.9 FPS); <=0 hides indicator
    bool desktop_native = false;
    int desktop_scale = 2;
    DisplayMode display_mode = DisplayMode::OverflowFit;
    int input_mode = 0;  // 0=KBD, 1=JP1, 2=JP2
    bool menu_open = false;
    bool show_activity_indicator = true;
    uint32_t activity_flags = 0;
    int8_t activity_tape_percent = -1;
    uint8_t activity_drive_mask = 0;
    uint32_t volume_osd_frames = 0;
    int volume_percent = -1;
    bool volume_muted = false;
};

std::string format_activity_label(uint32_t flags, int8_t tape_percent, uint8_t drive_mask);

// Renders the Alt-held 4..8 bottom icon bar, 1:1 viewport scrollbars, activity LED, and transient toast.
void render_hud(uint16_t* fb_rgb565, const HudState& hud, const ViewportLayout& layout);

}  // namespace czx::video
