#include "platform/linux_gamepad.hpp"

#include "czx/core.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace czx::platform {

namespace {

bool test_bit(const unsigned long* bits, int bit) noexcept {
    constexpr int kBitsPerLong = static_cast<int>(sizeof(unsigned long) * 8);
    return (bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong))) != 0;
}

uint32_t map_evdev_button(uint16_t code) noexcept {
    switch (code) {
        case BTN_SOUTH:    // Xbox A / PS Cross
        case BTN_TRIGGER:  // Generic joystick button 0
            return CZX_PAD_SOUTH;
        case BTN_EAST:     // Xbox B / PS Circle
        case BTN_THUMB:    // Generic joystick button 1
            return CZX_PAD_EAST;
        case BTN_WEST:     // Xbox X / PS Square
        case BTN_C:
        case BTN_THUMB2:   // Generic joystick button 2
            return CZX_PAD_WEST;
        case BTN_NORTH:    // Xbox Y / PS Triangle
        case BTN_Z:
        case BTN_TOP:      // Generic joystick button 3
            return CZX_PAD_NORTH;
        case BTN_TL:       // LB / L1
        case BTN_TOP2:
            return CZX_PAD_L1;
        case BTN_TR:       // RB / R1
        case BTN_PINKIE:
            return CZX_PAD_R1;
        case BTN_SELECT:   // Xbox View/Back / PS Share
        case BTN_BASE3:
            return CZX_PAD_SELECT;
        case BTN_START:    // Xbox Menu/Start / PS Options
        case BTN_BASE4:
            return CZX_PAD_START;
        case BTN_MODE:     // Xbox Guide / PS / Home
        case BTN_BASE5:
        case KEY_HOMEPAGE:
            return CZX_PAD_META;
        case BTN_DPAD_UP:
            return CZX_PAD_UP;
        case BTN_DPAD_DOWN:
            return CZX_PAD_DOWN;
        case BTN_DPAD_LEFT:
            return CZX_PAD_LEFT;
        case BTN_DPAD_RIGHT:
            return CZX_PAD_RIGHT;
        default:
            return 0u;
    }
}

bool parse_event_number(const char* name, int& out_num) noexcept {
    if (!name || std::strncmp(name, "event", 5) != 0 || name[5] == '\0') {
        return false;
    }
    int n = 0;
    for (const char* p = name + 5; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return false;
        n = n * 10 + (*p - '0');
    }
    out_num = n;
    return true;
}

}  // namespace

int eval_gamepad_axis_dir(int value, int min_val, int max_val, int flat) noexcept {
    if (max_val <= min_val) return 0;
    const int64_t center = (static_cast<int64_t>(min_val) + static_cast<int64_t>(max_val)) / 2;
    const int64_t half_span = std::max<int64_t>(1, (static_cast<int64_t>(max_val) - static_cast<int64_t>(min_val)) / 2);
    const int64_t deadzone = std::max<int64_t>(std::max(0, flat), (half_span * 35) / 100);
    const int64_t delta = static_cast<int64_t>(value) - center;
    if (delta <= -deadzone) return -1;
    if (delta >= deadzone) return 1;
    return 0;
}

uint32_t LinuxGamepadReader::PadDevice::combined_buttons() const noexcept {
    uint32_t out = key_buttons;
    if (hat_y < 0 || axis_y.dir < 0) out |= CZX_PAD_UP;
    if (hat_y > 0 || axis_y.dir > 0) out |= CZX_PAD_DOWN;
    if (hat_x < 0 || axis_x.dir < 0) out |= CZX_PAD_LEFT;
    if (hat_x > 0 || axis_x.dir > 0) out |= CZX_PAD_RIGHT;
    return out;
}

LinuxGamepadReader::~LinuxGamepadReader() {
    cleanup();
}

void LinuxGamepadReader::init() {
    cleanup();
    scan_devices();
}

void LinuxGamepadReader::cleanup() {
    for (int i = 0; i < static_cast<int>(kMaxGamepads); ++i) {
        close_slot(i, false);
    }
    has_scanned_ = false;
}

bool LinuxGamepadReader::is_rdev_open(dev_t rdev) const noexcept {
    if (rdev == 0) return false;
    for (const auto& p : pads_) {
        if (p.fd >= 0 && p.rdev == rdev) return true;
    }
    return false;
}

int LinuxGamepadReader::find_free_slot() const noexcept {
    for (int i = 0; i < static_cast<int>(kMaxGamepads); ++i) {
        if (pads_[static_cast<size_t>(i)].fd < 0) return i;
    }
    return -1;
}

void LinuxGamepadReader::close_slot(int slot, bool log_disconnect) {
    if (slot < 0 || static_cast<size_t>(slot) >= kMaxGamepads) return;
    auto& p = pads_[static_cast<size_t>(slot)];
    if (p.fd >= 0) {
        if (log_disconnect) {
            std::fprintf(stderr, "czx: gamepad P%d disconnected (%s)\n", slot + 1, p.name.c_str());
        }
        close(p.fd);
    }
    p = PadDevice{};
}

void LinuxGamepadReader::sync_device_state(PadDevice& pad) {
    if (pad.fd < 0) return;

    unsigned long key_state[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(pad.fd, EVIOCGKEY(sizeof(key_state)), key_state) >= 0) {
        static constexpr uint16_t kTrackedCodes[] = {
            BTN_SOUTH, BTN_EAST, BTN_C, BTN_WEST, BTN_NORTH, BTN_Z,
            BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE,
            BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
            BTN_TRIGGER, BTN_THUMB, BTN_THUMB2, BTN_TOP, BTN_TOP2, BTN_PINKIE,
            BTN_BASE3, BTN_BASE4, BTN_BASE5, KEY_HOMEPAGE,
        };
        uint32_t mask = 0u;
        for (uint16_t code : kTrackedCodes) {
            if (test_bit(key_state, code)) {
                mask |= map_evdev_button(code);
            }
        }
        pad.key_buttons = mask;
    } else {
        pad.key_buttons = 0u;
    }

    struct input_absinfo ai{};
    if (ioctl(pad.fd, EVIOCGABS(ABS_HAT0X), &ai) >= 0) {
        pad.hat_x = (ai.value < 0) ? -1 : ((ai.value > 0) ? 1 : 0);
    } else {
        pad.hat_x = 0;
    }
    if (ioctl(pad.fd, EVIOCGABS(ABS_HAT0Y), &ai) >= 0) {
        pad.hat_y = (ai.value < 0) ? -1 : ((ai.value > 0) ? 1 : 0);
    } else {
        pad.hat_y = 0;
    }
    if (ioctl(pad.fd, EVIOCGABS(ABS_X), &ai) >= 0 && ai.maximum > ai.minimum) {
        pad.axis_x = AxisCalib{true, ai.minimum, ai.maximum, ai.flat,
                               eval_gamepad_axis_dir(ai.value, ai.minimum, ai.maximum, ai.flat)};
    } else {
        pad.axis_x = AxisCalib{};
    }
    if (ioctl(pad.fd, EVIOCGABS(ABS_Y), &ai) >= 0 && ai.maximum > ai.minimum) {
        pad.axis_y = AxisCalib{true, ai.minimum, ai.maximum, ai.flat,
                               eval_gamepad_axis_dir(ai.value, ai.minimum, ai.maximum, ai.flat)};
    } else {
        pad.axis_y = AxisCalib{};
    }
}

bool LinuxGamepadReader::try_open_device(const std::string& path, int slot) {
    if (slot < 0 || static_cast<size_t>(slot) >= kMaxGamepads) return false;
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat st{};
    dev_t rdev = 0;
    if (fstat(fd, &st) == 0) {
        rdev = st.st_rdev;
        if (is_rdev_open(rdev)) {
            close(fd);
            return false;
        }
    }

    unsigned long ev_bits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0 ||
        !test_bit(ev_bits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        close(fd);
        return false;
    }

    const bool has_gamepad_btn =
        test_bit(key_bits, BTN_SOUTH) || test_bit(key_bits, BTN_TRIGGER) ||
        test_bit(key_bits, BTN_DPAD_UP);
    const bool looks_like_keyboard = test_bit(key_bits, KEY_A) && test_bit(key_bits, KEY_ENTER);
    if (!has_gamepad_btn || looks_like_keyboard) {
        close(fd);
        return false;
    }

    char name_buf[128] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name_buf)), name_buf) < 0) {
        std::snprintf(name_buf, sizeof(name_buf), "unknown");
    }

    auto& pad = pads_[static_cast<size_t>(slot)];
    pad = PadDevice{};
    pad.fd = fd;
    pad.path = path;
    pad.name = name_buf;
    pad.rdev = rdev;
    sync_device_state(pad);

    std::fprintf(stderr, "czx: gamepad P%d active on %s (%s)\n",
                 slot + 1, path.c_str(), name_buf);
    return true;
}

void LinuxGamepadReader::scan_devices() {
    last_scan_ = std::chrono::steady_clock::now();
    has_scanned_ = true;

    if (find_free_slot() < 0) return;

    DIR* dir = opendir("/dev/input");
    if (!dir) return;

    std::vector<int> event_nums;
    while (struct dirent* ent = readdir(dir)) {
        int num = -1;
        if (parse_event_number(ent->d_name, num)) {
            event_nums.push_back(num);
        }
    }
    closedir(dir);

    std::sort(event_nums.begin(), event_nums.end());
    for (int num : event_nums) {
        const int slot = find_free_slot();
        if (slot < 0) break;
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/event%d", num);
        (void)try_open_device(path, slot);
    }
}

void LinuxGamepadReader::handle_ev_key(PadDevice& pad, uint16_t code, int32_t value) noexcept {
    const uint32_t bit = map_evdev_button(code);
    if (bit == 0u) return;
    if (value != 0) {
        pad.key_buttons |= bit;
    } else {
        pad.key_buttons &= ~bit;
    }
}

void LinuxGamepadReader::handle_ev_abs(PadDevice& pad, uint16_t code, int32_t value) noexcept {
    switch (code) {
        case ABS_HAT0X:
            pad.hat_x = (value < 0) ? -1 : ((value > 0) ? 1 : 0);
            break;
        case ABS_HAT0Y:
            pad.hat_y = (value < 0) ? -1 : ((value > 0) ? 1 : 0);
            break;
        case ABS_X:
            if (pad.axis_x.valid) {
                pad.axis_x.dir = eval_gamepad_axis_dir(
                    value, pad.axis_x.min_val, pad.axis_x.max_val, pad.axis_x.flat);
            }
            break;
        case ABS_Y:
            if (pad.axis_y.valid) {
                pad.axis_y.dir = eval_gamepad_axis_dir(
                    value, pad.axis_y.min_val, pad.axis_y.max_val, pad.axis_y.flat);
            }
            break;
        default:
            break;
    }
}

void LinuxGamepadReader::poll(std::vector<GamepadState>& out_states) {
    const auto now = std::chrono::steady_clock::now();
    if (!has_scanned_ ||
        (find_free_slot() >= 0 &&
         std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scan_).count() >= 1000)) {
        scan_devices();
    }

    struct input_event evs[64];
    for (int i = 0; i < static_cast<int>(kMaxGamepads); ++i) {
        auto& pad = pads_[static_cast<size_t>(i)];
        if (pad.fd < 0) continue;

        bool disconnected = false;
        while (true) {
            const ssize_t n = read(pad.fd, evs, sizeof(evs));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                disconnected = true;
                break;
            }
            if (n == 0) {
                disconnected = true;
                break;
            }
            const size_t count = static_cast<size_t>(n) / sizeof(struct input_event);
            for (size_t k = 0; k < count; ++k) {
                const auto& ev = evs[k];
                if (ev.type == EV_KEY) {
                    handle_ev_key(pad, ev.code, ev.value);
                } else if (ev.type == EV_ABS) {
                    handle_ev_abs(pad, ev.code, ev.value);
                } else if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
                    sync_device_state(pad);
                }
            }
        }

        if (disconnected) {
            close_slot(i, true);
            continue;
        }

        out_states.push_back(GamepadState{static_cast<uint32_t>(i), pad.combined_buttons()});
    }
}

GamepadAugmentedInput::GamepadAugmentedInput(std::unique_ptr<PlatformInput> base)
    : base_(std::move(base)) {
    gamepads_.init();
}

bool GamepadAugmentedInput::init() {
    const bool ok = base_ ? base_->init() : false;
    gamepads_.init();
    return ok;
}

void GamepadAugmentedInput::poll(std::vector<czx::input::RawKeyEvent>& out_events) {
    if (base_) base_->poll(out_events);
}

void GamepadAugmentedInput::poll_mouse(std::vector<RawMouseEvent>& out_mouse) {
    if (base_) base_->poll_mouse(out_mouse);
}

void GamepadAugmentedInput::poll_gamepads(std::vector<GamepadState>& out_gamepads) {
    if (base_) base_->poll_gamepads(out_gamepads);
    gamepads_.poll(out_gamepads);
}

const char* GamepadAugmentedInput::backend_name() const {
    return base_ ? base_->backend_name() : "gamepad";
}

}  // namespace czx::platform
