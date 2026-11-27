#pragma once

#include "czx/core.h"
#include "input/keycodes.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace czx::input {

enum class InputMode {
    Keyboard = 0,
    Joystick = 1,
    JoystickP2 = 2,
    Mouse = 3,
};

enum class HostCommand {
    None = 0,
    SetModeNativePan,
    SetModeOverflowFit,
    SetModeAspectFit,
    ToggleInputMode,
    ToggleMenu,
    VolumeDown,
    VolumeUp,
    VolumeMute,
    ReloadCore,
};

struct PanDelta {
    int dx = 0;
    int dy = 0;
};

struct RouterCallbacks {
    void (*on_host_command)(void* user_data, HostCommand cmd) = nullptr;
    void (*on_pan)(void* user_data, int dx, int dy) = nullptr;
    void (*on_menu_key)(void* user_data, const czx_key_event_t& ev) = nullptr;
    void (*on_guest_key)(void* user_data, const czx_key_event_t& ev) = nullptr;
    void (*on_guest_joystick)(void* user_data, uint32_t port, uint32_t buttons) = nullptr;
    void (*on_guest_mouse)(void* user_data, int32_t dx, int32_t dy, uint32_t buttons) = nullptr;
    void* user_data = nullptr;
};

class InputRouter {
public:
    void set_callbacks(const RouterCallbacks& cb) noexcept { cb_ = cb; }

    void set_menu_open(bool open);
    bool is_menu_open() const noexcept { return menu_open_; }

    void set_input_mode(InputMode mode);
    InputMode input_mode() const noexcept { return input_mode_; }

    void set_native_pan_enabled(bool enabled) noexcept { native_pan_enabled_ = enabled; }
    void set_host_shortcuts_enabled(bool enabled) noexcept { host_shortcuts_enabled_ = enabled; }

    bool alt_held() const noexcept { return lalt_held_ || ralt_held_; }
    bool panning_held() const noexcept { return pan_up_ || pan_down_ || pan_left_ || pan_right_; }
    uint32_t modifiers() const noexcept;

    void handle_raw_key(const RawKeyEvent& raw);
    void tick(double dt_sec);

private:
    static constexpr int kTapPanStepPx = 8;
    static constexpr double kHoldPanSpeedPxPerSec = 180.0;
    static constexpr int kTapMouseStepPx = 4;
    static constexpr double kHoldMouseSpeedPxPerSec = 160.0;

    RouterCallbacks cb_{};
    InputMode input_mode_ = InputMode::Keyboard;
    bool menu_open_ = false;
    bool native_pan_enabled_ = false;
    bool host_shortcuts_enabled_ = true;

    bool lshift_held_ = false;
    bool rshift_held_ = false;
    bool lctrl_held_ = false;
    bool rctrl_held_ = false;
    bool lalt_held_ = false;
    bool ralt_held_ = false;
    bool sym_held_ = false;
    bool fn_held_ = false;

    bool pan_up_ = false;
    bool pan_down_ = false;
    bool pan_left_ = false;
    bool pan_right_ = false;
    double pan_acc_x_ = 0.0;
    double pan_acc_y_ = 0.0;

    uint32_t joy_buttons_ = 0;
    bool mouse_up_ = false;
    bool mouse_down_ = false;
    bool mouse_left_ = false;
    bool mouse_right_ = false;
    double mouse_acc_x_ = 0.0;
    double mouse_acc_y_ = 0.0;
    uint32_t mouse_buttons_ = 0;
    std::vector<czx_key_t> consumed_host_keys_;
    std::vector<std::pair<czx_key_t, czx_key_t>> fn_mapped_keys_;

    bool consume_if_tracked_release(czx_key_t key, bool pressed);
    void track_host_press(czx_key_t key);
    void clear_pan_hold();
    void clear_mouse_state();
    void update_modifier(czx_key_t key, bool pressed);
    bool handle_alt_shortcut(const RawKeyEvent& raw);
    bool handle_volume_shortcut(const RawKeyEvent& raw);
    RawKeyEvent apply_fn_chord(const RawKeyEvent& raw);
    bool handle_pan_key(czx_key_t key, bool pressed, bool repeat);
    bool handle_joystick_key(czx_key_t key, bool pressed);
    bool handle_mouse_key(czx_key_t key, bool pressed, bool repeat);
    uint32_t resolve_codepoint(const RawKeyEvent& raw) const noexcept;
};

}  // namespace czx::input
