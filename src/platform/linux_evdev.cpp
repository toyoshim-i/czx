#include "platform/linux_evdev.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace czx::platform {

namespace {

bool test_bit(const unsigned long* bits, int bit) {
    constexpr int kBitsPerLong = static_cast<int>(sizeof(unsigned long) * 8);
    return (bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong))) != 0;
}

bool contains_icase(const std::string& haystack, const char* needle) {
    const size_t nlen = std::strlen(needle);
    if (nlen == 0 || haystack.size() < nlen) return false;
    for (size_t i = 0; i + nlen <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

}  // namespace

LinuxEvdevInput::LinuxEvdevInput(std::string preferred_device)
    : preferred_device_(std::move(preferred_device)) {}

LinuxEvdevInput::~LinuxEvdevInput() {
    cleanup();
}

void LinuxEvdevInput::cleanup() {
    for (auto& d : devices_) {
        if (d.fd >= 0) {
            close(d.fd);
            d.fd = -1;
        }
    }
    devices_.clear();
}

bool LinuxEvdevInput::try_open(const std::string& path, bool require_keypad_caps) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    unsigned long ev_bits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0 ||
        !test_bit(ev_bits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        close(fd);
        return false;
    }

    if (require_keypad_caps) {
        if (!test_bit(key_bits, KEY_A) || !test_bit(key_bits, KEY_4) || !test_bit(key_bits, KEY_ESC)) {
            close(fd);
            return false;
        }
    }

    char name_buf[128] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name_buf)), name_buf) < 0) {
        std::snprintf(name_buf, sizeof(name_buf), "unknown");
    }

    const bool is_tca8418 =
        contains_icase(path, "cardputer-zero-internal") ||
        contains_icase(path, "3f804000") ||
        contains_icase(name_buf, "tca8418") ||
        contains_icase(name_buf, "m5stack") ||
        test_bit(key_bits, 383 /* KEY_CD / SYM */);

    struct stat st{};
    dev_t rdev = 0;
    if (fstat(fd, &st) == 0) {
        rdev = st.st_rdev;
    }

    devices_.push_back(Device{fd, path, is_tca8418, rdev});
    std::fprintf(stderr, "czx: evdev input active on %s (%s, tca8418=%d)\n",
                 path.c_str(), name_buf, is_tca8418 ? 1 : 0);
    return true;
}

bool LinuxEvdevInput::try_open_volume_device(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    for (const auto& d : devices_) {
        if (d.fd >= 0 && d.rdev != 0 && d.rdev == st.st_rdev) {
            return false;
        }
    }

    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    unsigned long ev_bits[(EV_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0 ||
        !test_bit(ev_bits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 ||
        !(test_bit(key_bits, KEY_VOLUMEUP) ||
          test_bit(key_bits, KEY_VOLUMEDOWN) ||
          test_bit(key_bits, KEY_MUTE))) {
        close(fd);
        return false;
    }

    char name_buf[128] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name_buf)), name_buf) < 0) {
        std::snprintf(name_buf, sizeof(name_buf), "unknown");
    }

    devices_.push_back(Device{fd, path, false, st.st_rdev});
    std::fprintf(stderr, "czx: evdev volume keys active on %s (%s)\n", path.c_str(), name_buf);
    return true;
}

bool LinuxEvdevInput::init() {
    keymap_.load_from_file("/usr/share/keymaps/tca8418_keypad_m5stack_keymap.map");

    if (!preferred_device_.empty()) {
        return try_open(preferred_device_, false);
    }
    if (const char* env = std::getenv("CZX_INPUT_DEVICE"); env && env[0] != '\0') {
        return try_open(env, false);
    }

    bool opened_primary =
        try_open("/dev/input/cardputer-zero-internal", false) ||
        try_open("/dev/input/by-path/platform-3f804000.i2c-event", false);
    if (!opened_primary) {
        for (int i = 0; i < 16; ++i) {
            const std::string path = "/dev/input/event" + std::to_string(i);
            if (try_open(path, true)) {
                opened_primary = true;
                break;
            }
        }
    }
    if (opened_primary) {
        for (int i = 0; i < 16; ++i) {
            (void)try_open_volume_device("/dev/input/event" + std::to_string(i));
        }
    }
    return opened_primary;
}

void LinuxEvdevInput::poll(std::vector<czx::input::RawKeyEvent>& out_events) {
    for (const auto& d : devices_) {
        if (d.fd < 0) continue;
        input_event ev{};
        while (true) {
            const ssize_t n = read(d.fd, &ev, sizeof(ev));
            if (n != sizeof(ev)) {
                break;
            }
            if (ev.type != EV_KEY) {
                continue;
            }
            const auto mapped = keymap_.map_evdev(ev.code, ev.value, d.is_tca8418);
            if (mapped.key != CZX_KEY_NONE) {
                out_events.push_back(mapped);
            }
        }
    }
}

}  // namespace czx::platform
