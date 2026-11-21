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
    ToggleAspectPolicy,
    ToggleBorders,
    ToggleMarginFill,
    ToggleInputMode,
    ResetCore,
    Quit,
};

struct MenuViewModel {
    std::string core_name;
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
    DisplayMode display_mode = DisplayMode::OverflowFit;
    AspectPolicy aspect_policy = AspectPolicy::ParAware;
    bool show_borders = false;
    MarginFill margin_fill = MarginFill::Auto;
    int input_mode = 0;  // 0=Keyboard, 1=Joy P1, 2=Joy P2
};

class OverlayMenu {
public:
    bool is_open() const noexcept { return open_; }
    void set_open(bool open) noexcept { open_ = open; }
    void toggle() noexcept { open_ = !open_; }

    MenuAction handle_key(const czx_key_event_t& ev, const MenuViewModel& vm);
    void render(uint16_t* fb_rgb565, const MenuViewModel& vm) const;

private:
    static constexpr int kItemCount = 10;
    bool open_ = false;
    int cursor_ = 0;
};

}  // namespace czx::video
