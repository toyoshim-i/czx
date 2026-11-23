#pragma once

#include "czx/core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace czx::video {

enum class LauncherAction {
    None = 0,
    SelectionChanged,
    BootSelected,
    OpenMenu,
};

struct LauncherItemView {
    std::string title;     // e.g. "PC-8801", "C64", "X68000"
    std::string subtitle;  // e.g. "QUASI88  |  Sorcerian.d88"
    std::string icon_key;  // passed to find_core_icon()
};

class LauncherView {
public:
    void set_selected(int index, int count, bool animate = false, int direction = 0) noexcept;
    int selected() const noexcept { return selected_; }
    bool is_animating() const noexcept;
    bool tick(double dt_sec) noexcept;  // returns true while anim_offset_ changed

    LauncherAction handle_key(const czx_key_event_t& ev, int item_count) noexcept;
    void render(uint16_t* fb_rgb565, const std::vector<LauncherItemView>& items) const;

private:
    int selected_ = 0;
    float anim_offset_ = 0.0f;  // in slot units; decays smoothly toward 0
};

}  // namespace czx::video
