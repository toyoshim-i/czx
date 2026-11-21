#pragma once

#include "input/tca8418_keymap.hpp"
#include "platform/platform.hpp"

#include <string>
#include <vector>

namespace czx::platform {

class LinuxEvdevInput : public PlatformInput {
public:
    explicit LinuxEvdevInput(std::string preferred_device = {});
    ~LinuxEvdevInput() override;

    bool init() override;
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override;
    const char* backend_name() const override { return "linux-evdev"; }

private:
    struct Device {
        int fd = -1;
        std::string path;
        bool is_tca8418 = false;
    };

    std::string preferred_device_;
    czx::input::Tca8418Keymap keymap_;
    std::vector<Device> devices_;

    bool try_open(const std::string& path, bool require_keypad_caps);
    void cleanup();
};

}  // namespace czx::platform
