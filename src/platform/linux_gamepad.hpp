#pragma once

#include "platform/platform.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

namespace czx::platform {

// Evaluates a raw evdev axis reading into -1, 0, or +1 with calibrated center and deadzone.
int eval_gamepad_axis_dir(int value, int min_val, int max_val, int flat) noexcept;

class LinuxGamepadReader {
public:
    LinuxGamepadReader() = default;
    ~LinuxGamepadReader();

    LinuxGamepadReader(const LinuxGamepadReader&) = delete;
    LinuxGamepadReader& operator=(const LinuxGamepadReader&) = delete;

    void init();
    void cleanup();
    void poll(std::vector<GamepadState>& out_states);

private:
    static constexpr size_t kMaxGamepads = 2;

    struct AxisCalib {
        bool valid = false;
        int min_val = -32768;
        int max_val = 32767;
        int flat = 0;
        int dir = 0;  // -1, 0, +1
    };

    struct PadDevice {
        int fd = -1;
        std::string path;
        std::string name;
        dev_t rdev = 0;
        uint32_t key_buttons = 0;   // CZX_PAD_* from EV_KEY (including digital DPAD)
        int hat_x = 0;              // -1, 0, +1
        int hat_y = 0;              // -1, 0, +1
        AxisCalib axis_x{};
        AxisCalib axis_y{};

        uint32_t combined_buttons() const noexcept;
    };

    std::array<PadDevice, kMaxGamepads> pads_{};
    int inotify_fd_ = -1;
    int inotify_wd_ = -1;

    void init_inotify();
    void poll_inotify();
    void scan_devices();
    bool is_rdev_open(dev_t rdev) const noexcept;
    int find_free_slot() const noexcept;
    bool try_open_device(const std::string& path, int slot);
    void close_slot(int slot, bool log_disconnect);
    void sync_device_state(PadDevice& pad, bool initial_open);
    void handle_ev_key(PadDevice& pad, uint16_t code, int32_t value) noexcept;
    void handle_ev_abs(PadDevice& pad, uint16_t code, int32_t value) noexcept;
};

// Wraps any PlatformInput and augments it with Linux evdev USB gamepad polling.
class GamepadAugmentedInput final : public PlatformInput {
public:
    explicit GamepadAugmentedInput(std::unique_ptr<PlatformInput> base);

    bool init() override;
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override;
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse) override;
    void poll_gamepads(std::vector<GamepadState>& out_gamepads) override;
    const char* backend_name() const override;

private:
    std::unique_ptr<PlatformInput> base_;
    LinuxGamepadReader gamepads_;
};

}  // namespace czx::platform
