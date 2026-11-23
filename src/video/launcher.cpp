#include "video/launcher.hpp"

#include "video/core_icons.hpp"
#include "video/font6x8.hpp"
#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace czx::video {

namespace {

uint8_t icon_alpha_for_distance(float dist) noexcept {
    if (dist <= 0.1f) {
        return 255u;
    }
    if (dist <= 1.0f) {
        const float t = (dist - 0.1f) / 0.9f;
        const int a = static_cast<int>(std::lround(255.0f + t * (132.0f - 255.0f)));
        return static_cast<uint8_t>(std::clamp(a, 132, 255));
    }
    if (dist <= 2.0f) {
        const float t = dist - 1.0f;
        const int a = static_cast<int>(std::lround(132.0f + t * (76.0f - 132.0f)));
        return static_cast<uint8_t>(std::clamp(a, 76, 132));
    }
    return 76u;
}

void blit_core_icon(Rgb565Canvas& canvas,
                    int x,
                    int y,
                    const CoreIconSprite& sp,
                    uint8_t alpha) noexcept {
    if (!sp.pixels || sp.width <= 0 || sp.height <= 0 || alpha == 0u) {
        return;
    }
    for (int sy = 0; sy < sp.height; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= kLcdHeight) continue;
        for (int sx = 0; sx < sp.width; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= kLcdWidth) continue;
            const uint16_t px = sp.pixels[static_cast<size_t>(sy) * static_cast<size_t>(sp.width) +
                                          static_cast<size_t>(sx)];
            if (px == kCoreIconTransparent) continue;
            if (alpha >= 254u) {
                canvas.set_pixel(dx, dy, px);
            } else {
                canvas.blend_pixel(dx, dy, px, alpha);
            }
        }
    }
}

}  // namespace

void LauncherView::set_selected(int index, int count, bool animate, int direction) noexcept {
    if (count <= 0) {
        selected_ = 0;
        anim_offset_ = 0.0f;
        return;
    }
    selected_ = ((index % count) + count) % count;
    if (!animate || count <= 1 || direction == 0) {
        anim_offset_ = 0.0f;
    } else {
        anim_offset_ = std::clamp(anim_offset_ + static_cast<float>(direction), -2.0f, 2.0f);
    }
}

bool LauncherView::is_animating() const noexcept {
    return std::abs(anim_offset_) >= 0.015f;
}

bool LauncherView::tick(double dt_sec) noexcept {
    if (anim_offset_ == 0.0f) {
        return false;
    }
    if (std::abs(anim_offset_) < 0.015f) {
        anim_offset_ = 0.0f;
        return true;
    }
    const double dt = std::clamp(dt_sec, 0.0, 0.05);
    if (dt <= 0.0) {
        return false;
    }
    anim_offset_ *= static_cast<float>(std::exp(-18.0 * dt));
    if (std::abs(anim_offset_) < 0.015f) {
        anim_offset_ = 0.0f;
    }
    return true;
}

LauncherAction LauncherView::handle_key(const czx_key_event_t& ev, int item_count) noexcept {
    if (!ev.pressed || item_count <= 0) {
        return LauncherAction::None;
    }

    switch (ev.key) {
        case CZX_KEY_LEFT:
        case CZX_KEY_Z:
            if (item_count > 1) {
                selected_ = (selected_ - 1 + item_count) % item_count;
                anim_offset_ = std::clamp(anim_offset_ - 1.0f, -2.0f, 2.0f);
                return LauncherAction::SelectionChanged;
            }
            return LauncherAction::None;
        case CZX_KEY_RIGHT:
        case CZX_KEY_C:
            if (item_count > 1) {
                selected_ = (selected_ + 1) % item_count;
                anim_offset_ = std::clamp(anim_offset_ + 1.0f, -2.0f, 2.0f);
                return LauncherAction::SelectionChanged;
            }
            return LauncherAction::None;
        case CZX_KEY_ENTER:
        case CZX_KEY_SPACE:
            return ev.repeat ? LauncherAction::None : LauncherAction::BootSelected;
        case CZX_KEY_UP:
        case CZX_KEY_DOWN:
        case CZX_KEY_F:
        case CZX_KEY_X:
            return ev.repeat ? LauncherAction::None : LauncherAction::OpenMenu;
        default:
            return LauncherAction::None;
    }
}

void LauncherView::render(uint16_t* fb_rgb565, const std::vector<LauncherItemView>& items) const {
    if (!fb_rgb565) return;
    Rgb565Canvas canvas(fb_rgb565, kLcdWidth, kLcdHeight);

    canvas.fill_rect(0, 0, kLcdWidth, kLcdHeight, pack_rgb565(16, 20, 30));

    // Top bar
    canvas.fill_rect(0, 0, kLcdWidth, 13, pack_rgb565(18, 24, 36));
    canvas.draw_text(6, 3, "CZX", pack_rgb565(245, 250, 255), 1);
    canvas.draw_text(28, 3, "LAUNCHER", pack_rgb565(88, 210, 255), 1);
    canvas.draw_hline(0, 13, kLcdWidth, pack_rgb565(44, 66, 94));

    const int n = static_cast<int>(items.size());
    if (n <= 0) {
        const char* msg = "NO CORES FOUND";
        const int mw = Rgb565Canvas::text_width(msg, 1);
        canvas.draw_text((kLcdWidth - mw) / 2, 76, msg, pack_rgb565(180, 195, 215), 1);
        return;
    }

    const int sel = ((selected_ % n) + n) % n;

    constexpr int kDotPitch = 6;
    const int dots_x0 = kLcdWidth - 6 - n * kDotPitch;
    if (dots_x0 >= 86) {
        for (int i = 0; i < n; ++i) {
            const int dx = dots_x0 + i * kDotPitch;
            const bool active = (i == sel);
            const int sz = active ? 3 : 2;
            const uint16_t col = active ? pack_rgb565(88, 210, 255) : pack_rgb565(52, 72, 98);
            canvas.fill_rect(dx, 5, sz, sz, col);
        }
    }

    // Continuous horizontal ribbon band
    constexpr int kBandY = 19;
    constexpr int kBandH = 64;
    canvas.fill_rect(0, kBandY, kLcdWidth, kBandH, pack_rgb565(24, 34, 50));
    canvas.draw_hline(0, kBandY, kLcdWidth, pack_rgb565(56, 84, 118));
    canvas.draw_hline(0, kBandY + kBandH, kLcdWidth, pack_rgb565(56, 84, 118));

    // Center highlight bracket frame
    constexpr int kFrameX = 126;
    constexpr int kFrameY = 16;
    constexpr int kFrameW = 68;
    constexpr int kFrameH = 71;
    canvas.fill_rect(kFrameX, kFrameY, kFrameW, kFrameH, pack_rgb565(30, 52, 76));
    canvas.draw_rect(kFrameX, kFrameY, kFrameW, kFrameH, pack_rgb565(56, 132, 176));

    const uint16_t corner_hi = pack_rgb565(88, 224, 255);
    // Top-left bracket
    canvas.draw_hline(kFrameX, kFrameY, 9, corner_hi);
    canvas.draw_hline(kFrameX, kFrameY + 1, 9, corner_hi);
    canvas.draw_vline(kFrameX, kFrameY, 9, corner_hi);
    canvas.draw_vline(kFrameX + 1, kFrameY, 9, corner_hi);
    // Top-right bracket
    canvas.draw_hline(kFrameX + kFrameW - 9, kFrameY, 9, corner_hi);
    canvas.draw_hline(kFrameX + kFrameW - 9, kFrameY + 1, 9, corner_hi);
    canvas.draw_vline(kFrameX + kFrameW - 1, kFrameY, 9, corner_hi);
    canvas.draw_vline(kFrameX + kFrameW - 2, kFrameY, 9, corner_hi);
    // Bottom-left bracket
    canvas.draw_hline(kFrameX, kFrameY + kFrameH - 1, 9, corner_hi);
    canvas.draw_hline(kFrameX, kFrameY + kFrameH - 2, 9, corner_hi);
    canvas.draw_vline(kFrameX, kFrameY + kFrameH - 9, 9, corner_hi);
    canvas.draw_vline(kFrameX + 1, kFrameY + kFrameH - 9, 9, corner_hi);
    // Bottom-right bracket
    canvas.draw_hline(kFrameX + kFrameW - 9, kFrameY + kFrameH - 1, 9, corner_hi);
    canvas.draw_hline(kFrameX + kFrameW - 9, kFrameY + kFrameH - 2, 9, corner_hi);
    canvas.draw_vline(kFrameX + kFrameW - 1, kFrameY + kFrameH - 9, 9, corner_hi);
    canvas.draw_vline(kFrameX + kFrameW - 2, kFrameY + kFrameH - 9, 9, corner_hi);

    // Chevron indicators flanking the center frame
    if (n > 1) {
        constexpr std::array<uint8_t, 5> kArrowMask = {0b001, 0b010, 0b100, 0b010, 0b001};
        const uint16_t arrow_col = pack_rgb565(88, 210, 255);
        for (int row = 0; row < 5; ++row) {
            const uint8_t bits = kArrowMask[static_cast<size_t>(row)];
            for (int col = 0; col < 3; ++col) {
                if (((bits >> (2 - col)) & 1u) != 0u) {
                    canvas.set_pixel(121 + col, 49 + row, arrow_col);
                    canvas.set_pixel(196 + (2 - col), 49 + row, arrow_col);
                }
            }
        }
    }

    // Machine icons on horizontal ribbon (56x48 at 66px pitch)
    constexpr int kPitchPx = 66;
    constexpr int kIconY = kBandY + (kBandH - kCoreIconHeight) / 2 + 1;
    const int min_off = (n == 1) ? 0 : -3;
    const int max_off = (n == 1) ? 0 : 3;
    for (int offset = min_off; offset <= max_off; ++offset) {
        const float rel = static_cast<float>(offset) + anim_offset_;
        const int cx = 160 + static_cast<int>(std::lround(rel * static_cast<float>(kPitchPx)));
        const int ix = cx - kCoreIconWidth / 2;
        if (ix + kCoreIconWidth <= 0 || ix >= kLcdWidth) {
            continue;
        }
        const int idx = ((sel + offset) % n + n) % n;
        const uint8_t alpha = icon_alpha_for_distance(std::abs(rel));
        const CoreIconSprite sp = find_core_icon(items[static_cast<size_t>(idx)].icon_key);
        blit_core_icon(canvas, ix, kIconY, sp, alpha);
    }

    // Machine title + subtitle
    const auto& cur = items[static_cast<size_t>(sel)];
    const int title_scale = (Rgb565Canvas::text_width(cur.title.c_str(), 2) <= kLcdWidth - 12) ? 2 : 1;
    const std::string title = truncate_tail_px(cur.title, kLcdWidth - 12, title_scale);
    const int tw = Rgb565Canvas::text_width(title.c_str(), title_scale);
    canvas.draw_text((kLcdWidth - tw) / 2,
                     (title_scale == 2) ? 91 : 95,
                     title.c_str(),
                     pack_rgb565(245, 250, 255),
                     title_scale);

    const std::string sub = truncate_tail_px(cur.subtitle, kLcdWidth - 16, 1);
    const int sw = Rgb565Canvas::text_width(sub.c_str(), 1);
    canvas.draw_text((kLcdWidth - sw) / 2, 111, sub.c_str(), pack_rgb565(130, 210, 245), 1);

    // Right-aligned operation guide above the bottom HUD bar
    constexpr const char* kGuide = "</>:CORE  OK:BOOT  ^/v:MENU";
    const int gw = Rgb565Canvas::text_width(kGuide, 1);
    canvas.draw_text(kLcdWidth - 6 - gw, 134, kGuide, pack_rgb565(132, 152, 178), 1);
}

}  // namespace czx::video
