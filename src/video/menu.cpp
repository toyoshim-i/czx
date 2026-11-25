#include "video/menu.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace czx::video {

namespace {

enum class MenuRow {
    Core = 0,
    Variant,
    Region,
    AdpcmLpf,
    CrtWhine,
    Mt32,
    Drive1OrMediaOrPreset,
    Drive2OrEject,
    Display,
    Vsync,
    Aspect,
    Borders,
    Margin,
    AccessLed,
    TapeWarp,
    Input,
    ResetOrLaunch,
    Quit,
};

std::vector<MenuRow> build_rows(const MenuViewModel& vm) {
    std::vector<MenuRow> rows;
    rows.reserve(15);
    rows.push_back(MenuRow::Core);
    if (vm.has_variant_option) {
        rows.push_back(MenuRow::Variant);
    }
    if (vm.has_region_option) {
        rows.push_back(MenuRow::Region);
    }
    if (vm.has_adpcm_lpf_option) {
        rows.push_back(MenuRow::AdpcmLpf);
    }
    if (vm.has_crt_whine_option) {
        rows.push_back(MenuRow::CrtWhine);
    }
    if (vm.has_mt32_option) {
        rows.push_back(MenuRow::Mt32);
    }
    rows.push_back(MenuRow::Drive1OrMediaOrPreset);
    rows.push_back(MenuRow::Drive2OrEject);
    if (vm.fullscreen_native) {
        rows.push_back(MenuRow::Aspect);
        if (vm.has_vsync_option) {
            rows.push_back(MenuRow::Vsync);
        }
    } else {
        rows.push_back(MenuRow::Display);
        if (vm.has_vsync_option) {
            rows.push_back(MenuRow::Vsync);
        }
        rows.push_back(MenuRow::Aspect);
    }
    rows.push_back(MenuRow::Borders);
    rows.push_back(MenuRow::Margin);
    rows.push_back(MenuRow::AccessLed);
    rows.push_back(MenuRow::TapeWarp);
    rows.push_back(MenuRow::Input);
    rows.push_back(MenuRow::ResetOrLaunch);
    rows.push_back(MenuRow::Quit);
    return rows;
}

}  // namespace

MenuAction OverlayMenu::handle_key(const czx_key_event_t& ev, const MenuViewModel& vm) {
    if (!open_ || !ev.pressed) {
        return MenuAction::None;
    }

    if (ev.key == CZX_KEY_ESC) {
        open_ = false;
        return MenuAction::Close;
    }

    const auto rows = build_rows(vm);
    const int count = static_cast<int>(rows.size());
    cursor_ = std::clamp(cursor_, 0, count - 1);

    if (ev.key == CZX_KEY_UP || ev.key == CZX_KEY_F) {
        cursor_ = (cursor_ + count - 1) % count;
        return MenuAction::None;
    }
    if (ev.key == CZX_KEY_DOWN || ev.key == CZX_KEY_X) {
        cursor_ = (cursor_ + 1) % count;
        return MenuAction::None;
    }

    const bool left = (ev.key == CZX_KEY_LEFT || ev.key == CZX_KEY_Z);
    const bool right = (ev.key == CZX_KEY_RIGHT || ev.key == CZX_KEY_C);
    const bool ok = (ev.key == CZX_KEY_ENTER || ev.key == CZX_KEY_SPACE);

    if (!left && !right && !ok) {
        return MenuAction::None;
    }

    switch (rows[static_cast<size_t>(cursor_)]) {
        case MenuRow::Core:
            if (left) return MenuAction::CycleCorePrev;
            if (right) return MenuAction::CycleCoreNext;
            if (ok && (vm.core_pending || vm.from_launcher)) {
                open_ = false;
                return MenuAction::ApplyCore;
            }
            return MenuAction::None;
        case MenuRow::Variant:
            return left ? MenuAction::CycleVariantPrev : MenuAction::CycleVariantNext;
        case MenuRow::Region:
            return left ? MenuAction::CycleRegionPrev : MenuAction::CycleRegionNext;
        case MenuRow::AdpcmLpf:
            return MenuAction::ToggleAdpcmLpf;
        case MenuRow::CrtWhine:
            return MenuAction::ToggleCrtWhine;
        case MenuRow::Mt32:
            return MenuAction::ToggleMt32;
        case MenuRow::Drive1OrMediaOrPreset:
            if (vm.drive_count >= 2u && vm.has_media_browser) {
                if (left) return MenuAction::EjectDrive1;
                return MenuAction::OpenDrive1Browser;
            }
            if (vm.has_media_browser) {
                return (ok || right) ? MenuAction::OpenMediaBrowser : MenuAction::None;
            }
            return left ? MenuAction::CyclePresetPrev : MenuAction::CyclePresetNext;
        case MenuRow::Drive2OrEject:
            if (vm.drive_count >= 2u && vm.has_media_browser) {
                if (left) return MenuAction::EjectDrive2;
                return MenuAction::OpenDrive2Browser;
            }
            return (ok && vm.can_eject) ? MenuAction::EjectMedia : MenuAction::None;
        case MenuRow::Display:
            return left ? MenuAction::CycleDisplayModePrev : MenuAction::CycleDisplayModeNext;
        case MenuRow::Vsync:
            return MenuAction::ToggleVsync;
        case MenuRow::Aspect:
            return left ? MenuAction::CycleAspectPolicyPrev : MenuAction::CycleAspectPolicyNext;
        case MenuRow::Borders:
            return MenuAction::ToggleBorders;
        case MenuRow::Margin:
            return MenuAction::ToggleMarginFill;
        case MenuRow::AccessLed:
            return MenuAction::ToggleActivityIndicator;
        case MenuRow::TapeWarp:
            return MenuAction::ToggleTapeWarp;
        case MenuRow::Input:
            return MenuAction::ToggleInputMode;
        case MenuRow::ResetOrLaunch:
            if (ok) {
                open_ = false;
                return MenuAction::ResetCore;
            }
            return MenuAction::None;
        case MenuRow::Quit:
            if (ok) {
                return MenuAction::Quit;
            }
            return MenuAction::None;
    }
    return MenuAction::None;
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
    if (vm.from_launcher) {
        std::snprintf(header, sizeof(header), "CZX LAUNCHER MENU");
    } else if (vm.src_width > 0 && vm.src_height > 0) {
        std::snprintf(header, sizeof(header), "CZX SYSTEM MENU  %dx%d", vm.src_width, vm.src_height);
    } else {
        std::snprintf(header, sizeof(header), "CZX SYSTEM MENU");
    }
    canvas.draw_text(kBoxX + 8, kBoxY + 4, header, pack_rgb565(255, 255, 255));

    const bool core_amber = vm.core_pending || vm.core_is_launcher;
    const std::string raw_core_name =
        (vm.core_pending && !vm.core_is_launcher && !vm.from_launcher) ? ("* " + vm.core_name)
                                                                       : vm.core_name;
    const std::string core_val = truncate_tail_px(raw_core_name, 178, 1);
    const std::string media_val = truncate_tail_px(vm.preset_or_media_label, 178, 1);
    const std::string d1_val = truncate_tail_px(vm.drive1_label, 178, 1);
    const std::string d2_val = truncate_tail_px(vm.drive2_label, 178, 1);
    const char* input_label = (vm.input_mode == 3) ? "Mouse (FZXC)"
                            : ((vm.input_mode == 2) ? "Joy P2 (FZXC)"
                            : ((vm.input_mode == 1) ? "Joy P1 (FZXC)" : "Keyboard"));

    const char* desktop_label = (vm.desktop_scale <= 1) ? "Desktop 1x"
                                                        : ((vm.desktop_scale == 2) ? "Desktop 2x"
                                                                                   : "Desktop 3x");
    const bool dual = (vm.drive_count >= 2u && vm.has_media_browser);

    const auto row_types = build_rows(vm);
    const int count = static_cast<int>(row_types.size());
    const int sel = std::clamp(cursor_, 0, count - 1);

    constexpr int kListY = kBoxY + 17;
    int row_h = 11;
    int visible = count;
    if (count <= 12) {
        row_h = 11;
        visible = count;
        scroll_ = 0;
    } else if (count == 13) {
        row_h = 10;
        visible = 13;
        scroll_ = 0;
    } else {
        row_h = 11;
        visible = 12;
        if (sel < scroll_) {
            scroll_ = sel;
        } else if (sel >= scroll_ + visible) {
            scroll_ = sel - visible + 1;
        }
        scroll_ = std::clamp(scroll_, 0, count - visible);
    }

    for (int i = scroll_; i < scroll_ + visible; ++i) {
        const MenuRow kind = row_types[static_cast<size_t>(i)];
        const char* label = "";
        const char* val = "";
        switch (kind) {
            case MenuRow::Core:
                label = "Core";
                val = core_val.c_str();
                break;
            case MenuRow::Variant:
                label = !vm.variant_option_label.empty() ? vm.variant_option_label.c_str() : "Model";
                val = vm.variant_value_label.c_str();
                break;
            case MenuRow::Region:
                label = "Region";
                val = vm.region_value_label.c_str();
                break;
            case MenuRow::AdpcmLpf:
                label = "ADPCM LPF";
                val = vm.adpcm_lpf_enabled ? "On" : "Off";
                break;
            case MenuRow::CrtWhine:
                label = "CRT Whine";
                val = vm.crt_whine_enabled ? "On" : "Off";
                break;
            case MenuRow::Mt32:
                label = "MT-32";
                val = !vm.mt32_value_label.empty() ? vm.mt32_value_label.c_str() : "Off";
                break;
            case MenuRow::Drive1OrMediaOrPreset:
                label = dual ? vm.drive1_name.c_str() : (vm.has_media_browser ? "Load Media" : "Preset");
                val = dual ? d1_val.c_str() : media_val.c_str();
                break;
            case MenuRow::Drive2OrEject:
                if (dual) {
                    label = vm.drive2_name.c_str();
                    val = d2_val.c_str();
                } else if (vm.from_launcher && vm.has_media_browser) {
                    label = "Clear Media";
                    val = vm.can_eject ? "[Enter]" : "(Empty)";
                } else {
                    label = vm.is_tape_media ? "Tape Play" : "Eject/Swap";
                    val = vm.can_eject ? "[Enter]" : "(N/A)";
                }
                break;
            case MenuRow::Display:
                label = "Display";
                val = vm.desktop_native_active ? desktop_label : display_mode_name(vm.display_mode);
                break;
            case MenuRow::Vsync:
                label = "VSync";
                val = vm.vsync_enabled ? "On" : "Off";
                break;
            case MenuRow::Aspect:
                label = "Aspect";
                val = aspect_policy_name(vm.aspect_policy);
                break;
            case MenuRow::Borders:
                label = "Borders";
                val = vm.show_borders ? "Show Full" : "Crop Active";
                break;
            case MenuRow::Margin:
                label = "Margin";
                val = margin_fill_name(vm.margin_fill);
                break;
            case MenuRow::AccessLed:
                label = "Access LED";
                val = vm.show_activity_indicator ? "On" : "Off";
                break;
            case MenuRow::TapeWarp:
                label = "Tape Warp";
                val = vm.tape_warp_enabled ? "On" : "Off";
                break;
            case MenuRow::Input:
                label = "Input";
                val = input_label;
                break;
            case MenuRow::ResetOrLaunch:
                label = vm.from_launcher
                            ? "Launch Core"
                            : (vm.core_is_launcher ? "Go Launcher"
                                                   : (vm.core_pending ? "Launch Core" : "Reset Core"));
                val = "[Enter]";
                break;
            case MenuRow::Quit:
                label = "Quit App";
                val = "[Enter]";
                break;
        }

        const int ry = kListY + (i - scroll_) * row_h;
        const bool selected = (i == sel);
        if (selected) {
            canvas.fill_rect(kBoxX + 6, ry, kBoxW - 12, row_h - 1, pack_rgb565(30, 86, 124));
        }

        const uint16_t label_col = selected ? pack_rgb565(255, 240, 140) : pack_rgb565(195, 205, 220);
        uint16_t val_col = selected ? pack_rgb565(255, 255, 255) : pack_rgb565(130, 210, 245);
        if (kind == MenuRow::Core && core_amber) {
            val_col = selected ? pack_rgb565(255, 230, 120) : pack_rgb565(255, 200, 90);
        }

        canvas.draw_char(kBoxX + 10, ry + 1, selected ? '>' : ' ', label_col);
        canvas.draw_text(kBoxX + 20, ry + 1, label, label_col);

        const int vw = Rgb565Canvas::text_width(val);
        canvas.draw_text(kBoxX + kBoxW - 10 - vw, ry + 1, val, val_col);
    }

    if (count > visible) {
        const int track_x = kBoxX + kBoxW - 5;
        const int track_y = kListY;
        const int track_h = visible * row_h - 1;
        canvas.fill_rect(track_x, track_y, 2, track_h, pack_rgb565(40, 54, 72));
        const int thumb_h = std::max(8, (track_h * visible) / count);
        const int max_scroll = count - visible;
        const int thumb_y =
            track_y + ((max_scroll > 0) ? ((track_h - thumb_h) * scroll_) / max_scroll : 0);
        canvas.fill_rect(track_x, thumb_y, 2, thumb_h, pack_rgb565(88, 210, 255));
    }

    canvas.draw_hline(kBoxX + 6, kBoxY + kBoxH - 14, kBoxW - 12, pack_rgb565(56, 74, 98));
    const MenuRow cur_row = row_types[static_cast<size_t>(sel)];
    const char* footer = nullptr;
    if (vm.from_launcher) {
        footer = (dual && (cur_row == MenuRow::Drive1OrMediaOrPreset || cur_row == MenuRow::Drive2OrEject))
                     ? "F/X:Move  Z:Clear  OK:Pick  ESC:Save"
                     : "F/X:Move  Z/C:Edit  OK:Go  ESC:Save";
    } else if (vm.core_is_launcher) {
        footer = "F/X:Move  Z/C:Change  OK/ESC:Launcher";
    } else if (dual && (cur_row == MenuRow::Drive1OrMediaOrPreset || cur_row == MenuRow::Drive2OrEject)) {
        footer = vm.core_pending ? "F/X:Move  OK:Load+Boot  ESC:Launch"
                                 : "F/X:Move  Z:Eject  OK:Load  ESC:Back";
    } else if (vm.core_pending) {
        footer = "F/X:Move  Z/C:Change  OK/ESC:Launch";
    } else {
        footer = "F/X:Move  Z/C:Change  OK:Go  ESC:Back";
    }
    canvas.draw_text(kBoxX + 12, kBoxY + kBoxH - 10, footer, pack_rgb565(150, 170, 195));
}

}  // namespace czx::video
