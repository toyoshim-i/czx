#pragma once

#include "czx/core.h"
#include "video/scaler.hpp"

#include <cstdint>
#include <string>

namespace czx::video {

enum class MenuAction {
    None = 0,
    Close,
    CycleCorePrev,
    CycleCoreNext,
    ApplyCore,
    CycleVariantPrev,
    CycleVariantNext,
    CycleRegionPrev,
    CycleRegionNext,
    OpenMediaBrowser,
    OpenDrive1Browser,
    OpenDrive2Browser,
    EjectDrive1,
    EjectDrive2,
    CyclePresetPrev,
    CyclePresetNext,
    EjectMedia,
    CycleDisplayModePrev,
    CycleDisplayModeNext,
    ToggleVsync,
    CycleAspectPolicyPrev,
    CycleAspectPolicyNext,
    ToggleBorders,
    ToggleMarginFill,
    ToggleActivityIndicator,
    ToggleTapeWarp,
    ToggleAdpcmLpf,
    ToggleCrtWhine,
    ToggleMt32,
    ToggleInputMode,
    ReturnToLauncher,
    ResetCore,
};

struct MenuViewModel {
    std::string core_name;
    bool core_pending = false;
    bool from_launcher = false;
    bool core_is_launcher = false;
    bool has_variant_option = false;
    std::string variant_option_label;
    std::string variant_value_label;
    bool has_region_option = false;
    std::string region_value_label;
    bool has_adpcm_lpf_option = false;
    bool adpcm_lpf_enabled = true;
    bool has_crt_whine_option = false;
    bool crt_whine_enabled = false;
    bool has_mt32_option = false;
    std::string mt32_value_label;
    std::string preset_or_media_label;
    bool has_media_browser = false;
    bool can_eject = false;
    bool is_tape_media = false;
    uint32_t drive_count = 1;
    std::string drive1_name = "Drive 1";
    std::string drive2_name = "Drive 2";
    std::string drive1_label = "Empty";
    std::string drive2_label = "Empty";
    int src_width = 0;
    int src_height = 0;
    bool fullscreen_native = false;
    bool desktop_native_active = false;
    int desktop_scale = 2;
    DisplayMode display_mode = DisplayMode::OverflowFit;
    bool has_vsync_option = false;
    bool vsync_enabled = true;
    AspectPolicy aspect_policy = AspectPolicy::ParAware;
    bool show_borders = false;
    MarginFill margin_fill = MarginFill::Auto;
    bool show_activity_indicator = true;
    bool tape_warp_enabled = true;
    int input_mode = 0;  // 0=Keyboard, 1=Joy P1, 2=Joy P2, 3=Mouse
};

class OverlayMenu {
public:
    bool is_open() const noexcept { return open_; }
    void set_open(bool open) noexcept { open_ = open; }
    void toggle() noexcept { open_ = !open_; }
    void reset_cursor() noexcept {
        cursor_ = 0;
        scroll_ = 0;
    }

    MenuAction handle_key(const czx_key_event_t& ev, const MenuViewModel& vm);
    void render(uint16_t* fb_rgb565, const MenuViewModel& vm) const;

private:
    bool open_ = false;
    int cursor_ = 0;
    mutable int scroll_ = 0;
};

}  // namespace czx::video
