#pragma once

#include <cstdint>
#include <string_view>

namespace czx::video {

inline constexpr int kCoreIconWidth = 56;
inline constexpr int kCoreIconHeight = 48;
inline constexpr uint16_t kCoreIconTransparent = 0xF81Fu;

struct CoreIconSprite {
    int width;
    int height;
    const uint16_t* pixels;  // row-major RGB565; kCoreIconTransparent = transparent
};

// Resolves a target/core id ("applewin", "c64", "vice_x64", "vice_x64sc",
// "quasi88", "np2kai", "bluemsx", "emuz700", "fm7", "efm7", "efm77av",
// "efm77av40", "msx68k", "dosbox_pure", "g800", "setup", "test_pattern", "quit").
// Unknown ids fall back to the test_pattern icon.
CoreIconSprite find_core_icon(std::string_view key) noexcept;

}  // namespace czx::video
