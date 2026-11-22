#include "video/menu.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <array>
#include <cstdio>

namespace czx::video {

MenuAction OverlayMenu::handle_key(const czx_key_event_t& ev, const MenuViewModel& vm) {
    if (!open_ || !ev.pressed) {
        return MenuAction::None;
    }

    if (ev.key == CZX_KEY_ESC) {
        open_ = false;
        return MenuAction::Close;
    }

    if (ev.key == CZX_KEY_UP || ev.key == CZX_KEY_F) {
        cursor_ = (cursor_ + kItemCount - 1) % kItemCount;
        return MenuAction::None;
    }
    if (ev.key == CZX_KEY_DOWN || ev.key == CZX_KEY_X) {
        cursor_ = (cursor_ + 1) % kItemCount;
        return MenuAction::None;
    }

    const bool left = (ev.key == CZX_KEY_LEFT || ev.key == CZX_KEY_Z);
    const bool right = (ev.key == CZX_KEY_RIGHT || ev.key == CZX_KEY_C);
    const bool ok = (ev.key == CZX_KEY_ENTER || ev.key == CZX_KEY_SPACE);

    if (!left && !right && !ok) {
        return MenuAction::None;
    }

    switch (cursor_) {
        case 0:
            return left ? MenuAction::CycleCorePrev : MenuAction::CycleCoreNext;
        case 1:
            if (vm.drive_count >= 2u && vm.has_media_browser) {
                if (left) return MenuAction::EjectDrive1;
                return MenuAction::OpenDrive1Browser;
            }
            if (vm.has_media_browser) {
                return (ok || right) ? MenuAction::OpenMediaBrowser : MenuAction::None;
            }
            return left ? MenuAction::CyclePresetPrev : MenuAction::CyclePresetNext;
        case 2:
            if (vm.drive_count >= 2u && vm.has_media_browser) {
                if (left) return MenuAction::EjectDrive2;
                return MenuAction::OpenDrive2Browser;
            }
            return (ok && vm.can_eject) ? MenuAction::EjectMedia : MenuAction::None;
        case 3:
            return left ? MenuAction::CycleDisplayModePrev : MenuAction::CycleDisplayModeNext;
        case 4:
            return MenuAction::ToggleAspectPolicy;
        case 5:
            return MenuAction::ToggleBorders;
        case 6:
            return MenuAction::ToggleMarginFill;
        case 7:
            return MenuAction::ToggleActivityIndicator;
        case 8:
            return MenuAction::ToggleInputMode;
        case 9:
            if (ok) {
                open_ = false;
                return MenuAction::ResetCore;
            }
            return MenuAction::None;
        case 10:
            if (ok) {
                return MenuAction::Quit;
            }
            return MenuAction::None;
        default:
            return MenuAction::None;
    }
}

void OverlayMenu::render(uint16_t* fb_rgb565, const MenuViewModel& vm) const {
    if (!open_ || !fb_rgb565) return;

    Rgb565Canvas canvas(fb_rgb565, kLcdWidth, kLcdHeight);

    constexpr int kBoxX = 12;
    constexpr int kBoxY = 3;
    constexpr int kBoxW = 296;
    constexpr int kBoxH = 164;

    canvas.fill_rect_alpha(0, 0, kLcdWidth, kLcdHeight, pack_rgb565(0, 0, 0), 140);
    canvas.fill_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(18, 24, 34));
    canvas.draw_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(88, 210, 255));

    // Header
    canvas.fill_rect(kBoxX + 1, kBoxY + 1, kBoxW - 2, 14, pack_rgb565(26, 44, 68));
    char header[64];
    std::snprintf(header, sizeof(header), "CZX SYSTEM MENU  %dx%d", vm.src_width, vm.src_height);
    canvas.draw_text(kBoxX + 8, kBoxY + 4, header, pack_rgb565(255, 255, 255));

    const std::string core_val = truncate_tail_px(vm.core_name, 178, 1);
    const std::string media_val = truncate_tail_px(vm.preset_or_media_label, 178, 1);
    const std::string d1_val = truncate_tail_px(vm.drive1_label, 178, 1);
    const std::string d2_val = truncate_tail_px(vm.drive2_label, 178, 1);
    const char* input_label = (vm.input_mode == 3) ? "Mouse (FZXC)"
                            : ((vm.input_mode == 2) ? "Joy P2 (FZXC)"
                            : ((vm.input_mode == 1) ? "Joy P1 (FZXC)" : "Keyboard"));

    struct Row {
        const char* label;
        const char* value;
    };

    const bool dual = (vm.drive_count >= 2u && vm.has_media_browser);
    const std::array<Row, kItemCount> rows = {{
        {"Core", core_val.c_str()},
        {dual ? vm.drive1_name.c_str() : (vm.has_media_browser ? "Load Media" : "Preset"),
         dual ? d1_val.c_str() : media_val.c_str()},
        {dual ? vm.drive2_name.c_str() : (vm.is_tape_media ? "Tape Play" : "Eject/Swap"),
         dual ? d2_val.c_str() : (vm.can_eject ? "[Enter]" : "(N/A)")},
        {"Display", display_mode_name(vm.display_mode)},
        {"Aspect", aspect_policy_name(vm.aspect_policy)},
        {"Borders", vm.show_borders ? "Show Full" : "Crop Active"},
        {"Margin", margin_fill_name(vm.margin_fill)},
        {"Access LED", vm.show_activity_indicator ? "On" : "Off"},
        {"Input", input_label},
        {"Reset Core", "[Enter]"},
        {"Quit App", "[Enter]"},
    }};

    constexpr int kListY = kBoxY + 17;
    constexpr int kRowH = 11;

    for (int i = 0; i < kItemCount; ++i) {
        const int ry = kListY + i * kRowH;
        const bool selected = (i == cursor_);
        if (selected) {
            canvas.fill_rect(kBoxX + 6, ry, kBoxW - 12, kRowH - 1, pack_rgb565(30, 86, 124));
        }

        const uint16_t label_col = selected ? pack_rgb565(255, 240, 140) : pack_rgb565(195, 205, 220);
        const uint16_t val_col = selected ? pack_rgb565(255, 255, 255) : pack_rgb565(130, 210, 245);

        canvas.draw_char(kBoxX + 10, ry + 1, selected ? '>' : ' ', label_col);
        canvas.draw_text(kBoxX + 20, ry + 1, rows[static_cast<size_t>(i)].label, label_col);

        const char* val = rows[static_cast<size_t>(i)].value;
        const int vw = Rgb565Canvas::text_width(val);
        canvas.draw_text(kBoxX + kBoxW - 10 - vw, ry + 1, val, val_col);
    }

    canvas.draw_hline(kBoxX + 6, kBoxY + kBoxH - 14, kBoxW - 12, pack_rgb565(56, 74, 98));
    const char* footer = (dual && (cursor_ == 1 || cursor_ == 2))
                             ? "F/X:Move  Z:Eject  OK:Load  ESC:Back"
                             : "F/X:Move  Z/C:Change  OK:Go  ESC:Back";
    canvas.draw_text(kBoxX + 12, kBoxY + kBoxH - 10, footer, pack_rgb565(150, 170, 195));
}

}  // namespace czx::video
