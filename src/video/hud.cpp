#include "video/hud.hpp"

#include "czx/core.h"
#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace czx::video {

namespace {

// Centers above physical Cardputer Zero keys 4, 5, 6, 7, 8 along the 320px LCD bottom edge.
constexpr std::array<int, 5> kSlotCentersX = {50, 107, 162, 215, 270};

// 12x10 1-bit icons (bit 11 = left, bit 0 = right).
constexpr uint16_t kIconNativePan[10] = {
    0b000011110000,
    0b000001100000,
    0b001111111100,
    0b010000000010,
    0b110110011011,
    0b110010010011,
    0b010000000010,
    0b001111111100,
    0b000001100000,
    0b000011110000,
};

constexpr uint16_t kIconOverflowFit[10] = {
    0b001000000100,
    0b011100001110,
    0b001011110100,
    0b000010010000,
    0b111110011111,
    0b111110011111,
    0b000010010000,
    0b001011110100,
    0b011100001110,
    0b001000000100,
};

constexpr uint16_t kIconAspectFit[10] = {
    0b111111111111,
    0b100000000001,
    0b100111111001,
    0b100100001001,
    0b100101101001,
    0b100101101001,
    0b100100001001,
    0b100111111001,
    0b100000000001,
    0b111111111111,
};

constexpr uint16_t kIconKeyboard[10] = {
    0b000000000000,
    0b111111111111,
    0b101010101001,
    0b100000000001,
    0b100101010101,
    0b100000000001,
    0b101011111001,
    0b111111111111,
    0b000000000000,
    0b000000000000,
};

constexpr uint16_t kIconJoystick[10] = {
    0b000000000000,
    0b000110000000,
    0b000110000000,
    0b011111100010,
    0b011111100111,
    0b000110000010,
    0b000110010000,
    0b000000111000,
    0b000000010000,
    0b000000000000,
};

constexpr uint16_t kIconMenu[10] = {
    0b000000000000,
    0b011111111110,
    0b011111111110,
    0b000000000000,
    0b011111111110,
    0b011111111110,
    0b000000000000,
    0b011111111110,
    0b011111111110,
    0b000000000000,
};

void draw_scrollbars(Rgb565Canvas& canvas, const HudState& hud, const ViewportLayout& layout) {
    if (hud.display_mode != DisplayMode::NativePan) return;
    if (!hud.alt_held && !hud.panning_active && hud.pan_hint_frames == 0) return;

    const int bottom_reserved = hud.alt_held ? 26 : 0;
    const uint16_t track_col = pack_rgb565(32, 42, 56);
    const uint16_t thumb_col = pack_rgb565(96, 224, 255);

    if (layout.max_pan_x > 0 && layout.active_w > 0) {
        const int track_x = 4;
        const int track_w = kLcdWidth - (layout.max_pan_y > 0 ? 12 : 8);
        const int track_y = kLcdHeight - bottom_reserved - 4;
        const int thumb_w = std::max(14, track_w * kLcdWidth / layout.active_w);
        const int thumb_x = track_x + (track_w - thumb_w) * layout.pan_x / layout.max_pan_x;
        canvas.fill_rect_alpha(track_x, track_y, track_w, 3, track_col, 190);
        canvas.fill_rect(thumb_x, track_y, thumb_w, 3, thumb_col);
    }

    if (layout.max_pan_y > 0 && layout.active_h > 0) {
        const int track_y = 4;
        const int track_h = kLcdHeight - bottom_reserved - (layout.max_pan_x > 0 ? 12 : 8);
        const int track_x = kLcdWidth - 4;
        const int thumb_h = std::max(12, track_h * kLcdHeight / layout.active_h);
        const int thumb_y = track_y + (track_h - thumb_h) * layout.pan_y / layout.max_pan_y;
        canvas.fill_rect_alpha(track_x, track_y, 3, track_h, track_col, 190);
        canvas.fill_rect(track_x, thumb_y, 3, thumb_h, thumb_col);
    }
}

void draw_activity_indicator(Rgb565Canvas& canvas, const HudState& hud, const ViewportLayout& layout) {
    if (!hud.show_activity_indicator || hud.activity_flags == 0u || hud.menu_open) {
        return;
    }

    const std::string label =
        format_activity_label(hud.activity_flags, hud.activity_tape_percent, hud.activity_drive_mask);
    if (label.empty()) return;

    uint16_t led_col = pack_rgb565(112, 224, 255);
    uint16_t border_col = pack_rgb565(84, 184, 224);
    if ((hud.activity_flags & CZX_ACTIVITY_TAPE_WRITE) != 0u) {
        led_col = pack_rgb565(255, 88, 72);
        border_col = pack_rgb565(230, 80, 68);
    } else if ((hud.activity_flags & CZX_ACTIVITY_TAPE_READ) != 0u) {
        led_col = pack_rgb565(64, 235, 160);
        border_col = pack_rgb565(56, 196, 136);
    } else if ((hud.activity_flags & CZX_ACTIVITY_QD) != 0u) {
        led_col = pack_rgb565(96, 200, 255);
        border_col = pack_rgb565(76, 176, 235);
    } else if ((hud.activity_flags & CZX_ACTIVITY_DISK) != 0u) {
        led_col = pack_rgb565(255, 204, 64);
        border_col = pack_rgb565(220, 172, 52);
    } else if ((hud.activity_flags & CZX_ACTIVITY_FASTFORWARD) != 0u) {
        led_col = pack_rgb565(96, 232, 255);
        border_col = pack_rgb565(76, 196, 235);
    }

    const int tw = Rgb565Canvas::text_width(label.c_str(), 1);
    const int box_w = 4 + 5 + 4 + tw + 4;
    constexpr int box_h = 13;

    const bool scrollbars_shown =
        (hud.display_mode == DisplayMode::NativePan) &&
        (hud.alt_held || hud.panning_active || hud.pan_hint_frames > 0);
    const int right_margin = (scrollbars_shown && layout.max_pan_y > 0) ? 8 : 4;
    int bottom_edge = hud.alt_held ? (144 - 3) : (kLcdHeight - 4);
    if (scrollbars_shown && layout.max_pan_x > 0) {
        bottom_edge -= 6;
    }

    const int bx = std::max(2, kLcdWidth - right_margin - box_w);
    const int by = std::max(2, bottom_edge - box_h);

    canvas.fill_rect_alpha(bx, by, box_w, box_h, pack_rgb565(10, 14, 22), 215);
    canvas.draw_rect(bx, by, box_w, box_h, border_col);
    canvas.fill_rect(bx + 4, by + 4, 5, 5, led_col);
    canvas.draw_text(bx + 13, by + 3, label.c_str(), pack_rgb565(245, 250, 255), 1);
}

}  // namespace

std::string format_activity_label(uint32_t flags, int8_t tape_percent, uint8_t drive_mask) {
    if (flags == 0u) return {};

    std::string out;
    auto append = [&](const std::string& token) {
        if (token.empty()) return;
        if (!out.empty()) out += ' ';
        out += token;
    };

    const bool has_pct = (tape_percent >= 0 && tape_percent <= 100);
    if ((flags & CZX_ACTIVITY_TAPE_WRITE) != 0u) {
        if (has_pct) {
            char buf[20];
            std::snprintf(buf, sizeof(buf), "REC %d%%", static_cast<int>(tape_percent));
            append(buf);
        } else {
            append("REC");
        }
    } else if ((flags & CZX_ACTIVITY_TAPE_READ) != 0u) {
        if (has_pct) {
            char buf[20];
            std::snprintf(buf, sizeof(buf), "TAPE %d%%", static_cast<int>(tape_percent));
            append(buf);
        } else {
            append("TAPE");
        }
    }

    if ((flags & CZX_ACTIVITY_QD) != 0u) {
        append("QD");
    }

    if ((flags & CZX_ACTIVITY_DISK) != 0u) {
        if (drive_mask == 0x01u) {
            append("FD1");
        } else if (drive_mask == 0x02u) {
            append("FD2");
        } else if (drive_mask == 0x03u) {
            append("FD1+2");
        } else {
            append("DISK");
        }
    }

    if ((flags & CZX_ACTIVITY_SIO) != 0u) {
        append("SIO");
    }

    if ((flags & CZX_ACTIVITY_FASTFORWARD) != 0u) {
        if (out.empty()) {
            out = "WARP >>";
        } else {
            out += " >>";
        }
    }

    return out;
}

void render_hud(uint16_t* fb_rgb565, const HudState& hud, const ViewportLayout& layout) {
    if (!fb_rgb565) return;
    Rgb565Canvas canvas(fb_rgb565, kLcdWidth, kLcdHeight);

    draw_scrollbars(canvas, hud, layout);
    draw_activity_indicator(canvas, hud, layout);

    if (hud.toast_frames > 0 && !hud.toast_text.empty() && !hud.menu_open) {
        const std::string toast = truncate_head_px(hud.toast_text, kLcdWidth - 22, 1);
        const int tw = Rgb565Canvas::text_width(toast.c_str(), 1);
        const int bw = tw + 14;
        const int bx = (kLcdWidth - bw) / 2;
        const int by = 5;
        canvas.fill_rect_alpha(bx, by, bw, 14, pack_rgb565(12, 18, 28), 220);
        canvas.draw_rect(bx, by, bw, 14, pack_rgb565(80, 210, 255));
        canvas.draw_text(bx + 7, by + 3, toast.c_str(), pack_rgb565(245, 250, 255));
    }

    if (!hud.alt_held) return;

    // Bottom Alt + [4..8] HUD bar
    constexpr int kBarY = 144;
    constexpr int kPillW = 50;
    constexpr int kPillH = 23;
    constexpr int kPillY = 145;

    canvas.fill_rect_alpha(0, kBarY, kLcdWidth, kLcdHeight - kBarY, pack_rgb565(10, 14, 22), 215);
    canvas.draw_hline(0, kBarY, kLcdWidth, pack_rgb565(64, 92, 128));

    struct Slot {
        char key;
        const char* label;
        const uint16_t* icon;
        bool active;
    };

    const char* joy_label = (hud.input_mode == 3) ? "MSE"
                          : ((hud.input_mode == 2) ? "JP2"
                          : ((hud.input_mode == 1) ? "JP1" : "KBD"));
    const bool joy_active = (hud.input_mode != 0);

    const std::array<Slot, 5> slots = {{
        {'4', hud.desktop_native ? "1X" : "1:1", kIconNativePan,
         hud.desktop_native ? (hud.desktop_scale == 1) : (hud.display_mode == DisplayMode::NativePan)},
        {'5', hud.desktop_native ? "2X" : "FIT", kIconOverflowFit,
         hud.desktop_native ? (hud.desktop_scale == 2) : (hud.display_mode == DisplayMode::OverflowFit)},
        {'6', hud.desktop_native ? "3X" : "ASP", kIconAspectFit,
         hud.desktop_native ? (hud.desktop_scale == 3) : (hud.display_mode == DisplayMode::AspectFit)},
        {'7', joy_label, joy_active ? kIconJoystick : kIconKeyboard, joy_active},
        {'8', "MENU", kIconMenu, hud.menu_open},
    }};

    for (size_t i = 0; i < slots.size(); ++i) {
        const auto& s = slots[i];
        const int px = kSlotCentersX[i] - kPillW / 2;
        const uint16_t bg = s.active ? pack_rgb565(18, 86, 118) : pack_rgb565(24, 30, 42);
        const uint16_t border = s.active ? pack_rgb565(88, 232, 255) : pack_rgb565(78, 92, 114);
        const uint16_t fg = s.active ? pack_rgb565(255, 255, 255) : pack_rgb565(205, 215, 230);
        const uint16_t key_col = s.active ? pack_rgb565(255, 230, 110) : pack_rgb565(140, 200, 255);

        canvas.fill_rect(px, kPillY, kPillW, kPillH, bg);
        canvas.draw_rect(px, kPillY, kPillW, kPillH, border);

        // Key number badge on upper-left, 12x10 icon on upper-right
        canvas.draw_char(px + 5, kPillY + 3, s.key, key_col, 1);
        canvas.draw_icon_bits(px + 26, kPillY + 2, 12, 10, s.icon, fg);

        // Centered short label on bottom row
        const int lw = Rgb565Canvas::text_width(s.label, 1);
        canvas.draw_text(px + (kPillW - lw) / 2, kPillY + 13, s.label, fg, 1);
    }
}

}  // namespace czx::video
