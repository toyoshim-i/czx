#include "input/input_router.hpp"

#include "input/tca8418_keymap.hpp"

#include <algorithm>
#include <cmath>

namespace czx::input {

namespace {

czx_key_t map_fn_chord_key(czx_key_t key) noexcept {
    switch (key) {
        case CZX_KEY_1: return CZX_KEY_F1;
        case CZX_KEY_2: return CZX_KEY_F2;
        case CZX_KEY_3: return CZX_KEY_F3;
        case CZX_KEY_4: return CZX_KEY_F4;
        case CZX_KEY_5: return CZX_KEY_F5;
        case CZX_KEY_6: return CZX_KEY_F6;
        case CZX_KEY_7: return CZX_KEY_F7;
        case CZX_KEY_8: return CZX_KEY_F8;
        case CZX_KEY_9: return CZX_KEY_F9;
        case CZX_KEY_0: return CZX_KEY_F10;
        case CZX_KEY_O: return CZX_KEY_F11;
        case CZX_KEY_P: return CZX_KEY_F12;
        default: return CZX_KEY_NONE;
    }
}

}  // namespace

void InputRouter::set_menu_open(bool open) {
    menu_open_ = open;
    clear_pan_hold();
    clear_mouse_state();
    if (open) {
        fn_mapped_keys_.clear();
    }
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
    clear_mouse_state();
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

void InputRouter::clear_mouse_state() {
    mouse_up_ = false;
    mouse_down_ = false;
    mouse_left_ = false;
    mouse_right_ = false;
    mouse_acc_x_ = 0.0;
    mouse_acc_y_ = 0.0;
    if (mouse_buttons_ != 0) {
        mouse_buttons_ = 0;
        if (cb_.on_guest_mouse) {
            cb_.on_guest_mouse(cb_.user_data, 0, 0, 0);
        }
    }
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

bool InputRouter::handle_volume_shortcut(const RawKeyEvent& raw) {
    if (raw.key == CZX_KEY_MUTE || raw.key == CZX_KEY_VOLUMEDOWN || raw.key == CZX_KEY_VOLUMEUP) {
        if (raw.pressed) {
            if (raw.key == CZX_KEY_MUTE) {
                if (!raw.repeat && cb_.on_host_command) {
                    cb_.on_host_command(cb_.user_data, HostCommand::VolumeMute);
                }
            } else if (cb_.on_host_command) {
                cb_.on_host_command(cb_.user_data,
                                    (raw.key == CZX_KEY_VOLUMEDOWN) ? HostCommand::VolumeDown
                                                                    : HostCommand::VolumeUp);
            }
        }
        return true;
    }

    if (fn_held_ && !alt_held() && raw.pressed) {
        HostCommand cmd = HostCommand::None;
        switch (raw.key) {
            case CZX_KEY_A:
                track_host_press(CZX_KEY_A);
                if (!raw.repeat) cmd = HostCommand::VolumeMute;
                break;
            case CZX_KEY_S:
                track_host_press(CZX_KEY_S);
                cmd = HostCommand::VolumeDown;
                break;
            case CZX_KEY_D:
                track_host_press(CZX_KEY_D);
                cmd = HostCommand::VolumeUp;
                break;
            default:
                return false;
        }
        if (cmd != HostCommand::None && cb_.on_host_command) {
            cb_.on_host_command(cb_.user_data, cmd);
        }
        return true;
    }

    return false;
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
    if (input_mode_ != InputMode::Joystick && input_mode_ != InputMode::JoystickP2) return false;

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

RawKeyEvent InputRouter::apply_fn_chord(const RawKeyEvent& raw) {
    const auto it = std::find_if(fn_mapped_keys_.begin(), fn_mapped_keys_.end(),
                                 [&](const auto& kv) { return kv.first == raw.key; });
    if (!raw.pressed) {
        if (it != fn_mapped_keys_.end()) {
            RawKeyEvent mapped{it->second, false, false, 0};
            fn_mapped_keys_.erase(it);
            return mapped;
        }
        return raw;
    }

    if (raw.repeat && it != fn_mapped_keys_.end()) {
        return RawKeyEvent{it->second, true, true, 0};
    }

    if (fn_held_ && !alt_held()) {
        const czx_key_t fn_key = map_fn_chord_key(raw.key);
        if (fn_key != CZX_KEY_NONE) {
            if (it == fn_mapped_keys_.end()) {
                fn_mapped_keys_.emplace_back(raw.key, fn_key);
            } else {
                it->second = fn_key;
            }
            return RawKeyEvent{fn_key, true, raw.repeat, 0};
        }
    }

    return raw;
}

bool InputRouter::handle_mouse_key(czx_key_t key, bool pressed, bool repeat) {
    if (input_mode_ != InputMode::Mouse) return false;

    uint32_t btn_bit = 0;
    bool* dir_flag = nullptr;
    int dir_x = 0;
    int dir_y = 0;

    switch (key) {
        case CZX_KEY_F:
        case CZX_KEY_UP:
            dir_flag = &mouse_up_;
            dir_y = -1;
            break;
        case CZX_KEY_X:
        case CZX_KEY_DOWN:
            dir_flag = &mouse_down_;
            dir_y = 1;
            break;
        case CZX_KEY_Z:
        case CZX_KEY_LEFT:
            dir_flag = &mouse_left_;
            dir_x = -1;
            break;
        case CZX_KEY_C:
        case CZX_KEY_RIGHT:
            dir_flag = &mouse_right_;
            dir_x = 1;
            break;
        case CZX_KEY_SPACE:
            btn_bit = CZX_MOUSE_LEFT;
            break;
        case CZX_KEY_ENTER:
            btn_bit = CZX_MOUSE_RIGHT;
            break;
        default:
            return false;
    }

    if (btn_bit != 0) {
        const uint32_t prev = mouse_buttons_;
        if (pressed) {
            mouse_buttons_ |= btn_bit;
        } else {
            mouse_buttons_ &= ~btn_bit;
        }
        if (mouse_buttons_ != prev && cb_.on_guest_mouse) {
            cb_.on_guest_mouse(cb_.user_data, 0, 0, mouse_buttons_);
        }
        return true;
    }

    *dir_flag = pressed;
    if (pressed && !repeat && cb_.on_guest_mouse) {
        const bool ctrl = (lctrl_held_ || rctrl_held_);
        const bool shift = (lshift_held_ || rshift_held_);
        const int step = ctrl ? 1 : (shift ? kTapMouseStepPx * 2 : kTapMouseStepPx);
        cb_.on_guest_mouse(cb_.user_data, dir_x * step, dir_y * step, mouse_buttons_);
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
    if (raw.key == CZX_KEY_NONE && raw.native_key == 0u) return;

    update_modifier(raw.key, raw.pressed);

    if (host_shortcuts_enabled_) {
        // Alt itself stays purely a host modifier so retro PCs never get stray Alt state.
        if (raw.key == CZX_KEY_LALT || raw.key == CZX_KEY_RALT) {
            return;
        }

        if (handle_alt_shortcut(raw)) {
            return;
        }

        if (handle_volume_shortcut(raw)) {
            return;
        }
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
            ev.native_key = raw.native_key;
            cb_.on_menu_key(cb_.user_data, ev);
        }
        return;
    }

    if (host_shortcuts_enabled_ && handle_pan_key(raw.key, raw.pressed, raw.repeat)) {
        consume_if_tracked_release(raw.key, raw.pressed);
        return;
    }

    if (consume_if_tracked_release(raw.key, raw.pressed)) {
        return;
    }

    const RawKeyEvent eff = host_shortcuts_enabled_ ? apply_fn_chord(raw) : raw;

    if (handle_joystick_key(eff.key, eff.pressed)) {
        return;
    }

    if (handle_mouse_key(eff.key, eff.pressed, eff.repeat)) {
        return;
    }

    if (cb_.on_guest_key) {
        czx_key_event_t ev{};
        ev.key = eff.key;
        ev.pressed = eff.pressed;
        ev.repeat = eff.repeat;
        ev.mods = modifiers();
        ev.codepoint = resolve_codepoint(eff);
        ev.native_key = eff.native_key;
        cb_.on_guest_key(cb_.user_data, ev);
    }
}

void InputRouter::tick(double dt_sec) {
    if (menu_open_ || dt_sec <= 0.0) {
        return;
    }

    auto step_axis = [&](int dir_x, int dir_y, double speed, double& acc_x, double& acc_y,
                         int& out_dx, int& out_dy) -> bool {
        if (dir_x == 0 && dir_y == 0) {
            acc_x = 0.0;
            acc_y = 0.0;
            return false;
        }
        acc_x += static_cast<double>(dir_x) * speed * dt_sec;
        acc_y += static_cast<double>(dir_y) * speed * dt_sec;
        out_dx = static_cast<int>(std::trunc(acc_x));
        out_dy = static_cast<int>(std::trunc(acc_y));
        if (out_dx == 0 && out_dy == 0) {
            return false;
        }
        acc_x -= static_cast<double>(out_dx);
        acc_y -= static_cast<double>(out_dy);
        return true;
    };

    const bool shift = (lshift_held_ || rshift_held_);
    if (native_pan_enabled_ && alt_held()) {
        const int dir_x = (pan_right_ ? 1 : 0) - (pan_left_ ? 1 : 0);
        const int dir_y = (pan_down_ ? 1 : 0) - (pan_up_ ? 1 : 0);
        int dx = 0;
        int dy = 0;
        if (step_axis(dir_x, dir_y, kHoldPanSpeedPxPerSec * (shift ? 2.0 : 1.0),
                      pan_acc_x_, pan_acc_y_, dx, dy) &&
            cb_.on_pan) {
            cb_.on_pan(cb_.user_data, dx, dy);
        }
    }

    if (input_mode_ == InputMode::Mouse && !alt_held()) {
        const int dir_x = (mouse_right_ ? 1 : 0) - (mouse_left_ ? 1 : 0);
        const int dir_y = (mouse_down_ ? 1 : 0) - (mouse_up_ ? 1 : 0);
        const bool ctrl = (lctrl_held_ || rctrl_held_);
        const double mult = ctrl ? 0.25 : (shift ? 2.0 : 1.0);
        int dx = 0;
        int dy = 0;
        if (step_axis(dir_x, dir_y, kHoldMouseSpeedPxPerSec * mult,
                      mouse_acc_x_, mouse_acc_y_, dx, dy) &&
            cb_.on_guest_mouse) {
            cb_.on_guest_mouse(cb_.user_data, dx, dy, mouse_buttons_);
        }
    }
}

}  // namespace czx::input
