#include "input/input_router.hpp"

#include "input/tca8418_keymap.hpp"

#include <algorithm>
#include <cmath>

namespace czx::input {

void InputRouter::set_menu_open(bool open) {
    menu_open_ = open;
    clear_pan_hold();
    if (joy_buttons_ != 0) {
        joy_buttons_ = 0;
        if (cb_.on_guest_joystick) {
            cb_.on_guest_joystick(cb_.user_data, 0, 0);
            cb_.on_guest_joystick(cb_.user_data, 1, 0);
        }
    }
}

void InputRouter::set_input_mode(InputMode mode) {
    if (input_mode_ == mode) return;
    input_mode_ = mode;
    if (joy_buttons_ != 0) {
        joy_buttons_ = 0;
        if (cb_.on_guest_joystick) {
            cb_.on_guest_joystick(cb_.user_data, 0, 0);
            cb_.on_guest_joystick(cb_.user_data, 1, 0);
        }
    }
}

uint32_t InputRouter::modifiers() const noexcept {
    uint32_t m = 0;
    if (lshift_held_ || rshift_held_) m |= CZX_MOD_SHIFT;
    if (lctrl_held_ || rctrl_held_) m |= CZX_MOD_CTRL;
    if (alt_held()) m |= CZX_MOD_ALT;
    if (sym_held_) m |= CZX_MOD_SYM;
    if (fn_held_) m |= CZX_MOD_FN;
    return m;
}

void InputRouter::clear_pan_hold() {
    pan_up_ = false;
    pan_down_ = false;
    pan_left_ = false;
    pan_right_ = false;
    pan_acc_x_ = 0.0;
    pan_acc_y_ = 0.0;
}

void InputRouter::update_modifier(czx_key_t key, bool pressed) {
    switch (key) {
        case CZX_KEY_LSHIFT: lshift_held_ = pressed; break;
        case CZX_KEY_RSHIFT: rshift_held_ = pressed; break;
        case CZX_KEY_LCTRL: lctrl_held_ = pressed; break;
        case CZX_KEY_RCTRL: rctrl_held_ = pressed; break;
        case CZX_KEY_LALT: lalt_held_ = pressed; break;
        case CZX_KEY_RALT: ralt_held_ = pressed; break;
        case CZX_KEY_SYM: sym_held_ = pressed; break;
        case CZX_KEY_FN: fn_held_ = pressed; break;
        default: break;
    }
    if (!alt_held()) {
        clear_pan_hold();
    }
}

void InputRouter::track_host_press(czx_key_t key) {
    if (std::find(consumed_host_keys_.begin(), consumed_host_keys_.end(), key) == consumed_host_keys_.end()) {
        consumed_host_keys_.push_back(key);
    }
}

bool InputRouter::consume_if_tracked_release(czx_key_t key, bool pressed) {
    if (pressed) return false;
    const auto it = std::find(consumed_host_keys_.begin(), consumed_host_keys_.end(), key);
    if (it == consumed_host_keys_.end()) return false;
    consumed_host_keys_.erase(it);
    return true;
}

bool InputRouter::handle_alt_shortcut(const RawKeyEvent& raw) {
    if (!alt_held() || !raw.pressed || raw.repeat) {
        return false;
    }

    HostCommand cmd = HostCommand::None;
    switch (raw.key) {
        case CZX_KEY_4:
        case CZX_KEY_F4:
            cmd = HostCommand::SetModeNativePan;
            break;
        case CZX_KEY_5:
        case CZX_KEY_F5:
            cmd = HostCommand::SetModeOverflowFit;
            break;
        case CZX_KEY_6:
        case CZX_KEY_F6:
            cmd = HostCommand::SetModeAspectFit;
            break;
        case CZX_KEY_7:
        case CZX_KEY_F7:
            cmd = HostCommand::ToggleInputMode;
            break;
        case CZX_KEY_8:
        case CZX_KEY_F8:
            cmd = HostCommand::ToggleMenu;
            break;
        default:
            return false;
    }

    track_host_press(raw.key);
    if (cb_.on_host_command) {
        cb_.on_host_command(cb_.user_data, cmd);
    }
    return true;
}

bool InputRouter::handle_pan_key(czx_key_t key, bool pressed, bool repeat) {
    if (!native_pan_enabled_) return false;
    if (pressed && !alt_held()) return false;

    bool* dir_flag = nullptr;
    int step_x = 0;
    int step_y = 0;

    switch (key) {
        case CZX_KEY_F:
        case CZX_KEY_UP:
            dir_flag = &pan_up_;
            step_y = -kTapPanStepPx;
            break;
        case CZX_KEY_X:
        case CZX_KEY_DOWN:
            dir_flag = &pan_down_;
            step_y = kTapPanStepPx;
            break;
        case CZX_KEY_Z:
        case CZX_KEY_LEFT:
            dir_flag = &pan_left_;
            step_x = -kTapPanStepPx;
            break;
        case CZX_KEY_C:
        case CZX_KEY_RIGHT:
            dir_flag = &pan_right_;
            step_x = kTapPanStepPx;
            break;
        default:
            return false;
    }

    if (pressed) {
        *dir_flag = true;
        track_host_press(key);
        if (!repeat && cb_.on_pan) {
            const int mult = (lshift_held_ || rshift_held_) ? 2 : 1;
            cb_.on_pan(cb_.user_data, step_x * mult, step_y * mult);
        }
        return true;
    }

    const bool was_panning = *dir_flag;
    *dir_flag = false;
    return was_panning;
}

bool InputRouter::handle_joystick_key(czx_key_t key, bool pressed) {
    if (input_mode_ == InputMode::Keyboard) return false;

    uint32_t bit = 0;
    switch (key) {
        case CZX_KEY_F:
        case CZX_KEY_UP:
            bit = CZX_JOY_UP;
            break;
        case CZX_KEY_X:
        case CZX_KEY_DOWN:
            bit = CZX_JOY_DOWN;
            break;
        case CZX_KEY_Z:
        case CZX_KEY_LEFT:
            bit = CZX_JOY_LEFT;
            break;
        case CZX_KEY_C:
        case CZX_KEY_RIGHT:
            bit = CZX_JOY_RIGHT;
            break;
        case CZX_KEY_SPACE:
            bit = CZX_JOY_FIRE1;
            break;
        case CZX_KEY_ENTER:
            bit = CZX_JOY_FIRE2;
            break;
        default:
            return false;
    }

    const uint32_t prev = joy_buttons_;
    if (pressed) {
        joy_buttons_ |= bit;
    } else {
        joy_buttons_ &= ~bit;
    }
    if (joy_buttons_ != prev && cb_.on_guest_joystick) {
        const uint32_t port = (input_mode_ == InputMode::JoystickP2) ? 1u : 0u;
        cb_.on_guest_joystick(cb_.user_data, port, joy_buttons_);
    }
    return true;
}

uint32_t InputRouter::resolve_codepoint(const RawKeyEvent& raw) const noexcept {
    if (raw.codepoint != 0) {
        return raw.codepoint;
    }
    const bool shift = (lshift_held_ || rshift_held_);
    return Tca8418Keymap::key_to_ascii(raw.key, shift);
}

void InputRouter::handle_raw_key(const RawKeyEvent& raw) {
    if (raw.key == CZX_KEY_NONE) return;

    update_modifier(raw.key, raw.pressed);

    // Alt itself stays purely a host modifier so retro PCs never get stray Alt state.
    if (raw.key == CZX_KEY_LALT || raw.key == CZX_KEY_RALT) {
        return;
    }

    if (handle_alt_shortcut(raw)) {
        return;
    }

    if (menu_open_) {
        if (consume_if_tracked_release(raw.key, raw.pressed)) {
            return;
        }
        if (cb_.on_menu_key) {
            czx_key_event_t ev{};
            ev.key = raw.key;
            ev.pressed = raw.pressed;
            ev.repeat = raw.repeat;
            ev.mods = modifiers();
            ev.codepoint = resolve_codepoint(raw);
            cb_.on_menu_key(cb_.user_data, ev);
        }
        return;
    }

    if (handle_pan_key(raw.key, raw.pressed, raw.repeat)) {
        consume_if_tracked_release(raw.key, raw.pressed);
        return;
    }

    if (consume_if_tracked_release(raw.key, raw.pressed)) {
        return;
    }

    if (handle_joystick_key(raw.key, raw.pressed)) {
        return;
    }

    if (cb_.on_guest_key) {
        czx_key_event_t ev{};
        ev.key = raw.key;
        ev.pressed = raw.pressed;
        ev.repeat = raw.repeat;
        ev.mods = modifiers();
        ev.codepoint = resolve_codepoint(raw);
        cb_.on_guest_key(cb_.user_data, ev);
    }
}

void InputRouter::tick(double dt_sec) {
    if (menu_open_ || !native_pan_enabled_ || !alt_held() || dt_sec <= 0.0) {
        return;
    }
    const int dir_x = (pan_right_ ? 1 : 0) - (pan_left_ ? 1 : 0);
    const int dir_y = (pan_down_ ? 1 : 0) - (pan_up_ ? 1 : 0);
    if (dir_x == 0 && dir_y == 0) {
        pan_acc_x_ = 0.0;
        pan_acc_y_ = 0.0;
        return;
    }

    const double speed = kHoldPanSpeedPxPerSec * ((lshift_held_ || rshift_held_) ? 2.0 : 1.0);
    pan_acc_x_ += static_cast<double>(dir_x) * speed * dt_sec;
    pan_acc_y_ += static_cast<double>(dir_y) * speed * dt_sec;

    const int dx = static_cast<int>(std::trunc(pan_acc_x_));
    const int dy = static_cast<int>(std::trunc(pan_acc_y_));
    if (dx != 0 || dy != 0) {
        pan_acc_x_ -= static_cast<double>(dx);
        pan_acc_y_ -= static_cast<double>(dy);
        if (cb_.on_pan) {
            cb_.on_pan(cb_.user_data, dx, dy);
        }
    }
}

}  // namespace czx::input
