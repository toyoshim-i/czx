#pragma once

#include "czx/core.h"

#include <cstdint>

namespace czx::video {

inline constexpr uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(r >> 3) << 11) |
                                 (static_cast<uint16_t>(g >> 2) << 5) |
                                 static_cast<uint16_t>(b >> 3));
}

inline constexpr void unpack_rgb565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) noexcept {
    const uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(c & 0x1F);
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

inline constexpr uint16_t blend_rgb565(uint16_t dst, uint16_t src, uint8_t alpha_0_to_255) noexcept {
    if (alpha_0_to_255 == 255) return src;
    if (alpha_0_to_255 == 0) return dst;
    uint8_t dr = 0, dg = 0, db = 0;
    uint8_t sr = 0, sg = 0, sb = 0;
    unpack_rgb565(dst, dr, dg, db);
    unpack_rgb565(src, sr, sg, sb);
    const uint32_t a = alpha_0_to_255;
    const uint32_t inv = 255u - a;
    const uint8_t r = static_cast<uint8_t>((sr * a + dr * inv + 127u) / 255u);
    const uint8_t g = static_cast<uint8_t>((sg * a + dg * inv + 127u) / 255u);
    const uint8_t b = static_cast<uint8_t>((sb * a + db * inv + 127u) / 255u);
    return pack_rgb565(r, g, b);
}

inline void read_pixel_rgb888(const uint8_t* row, uint32_t x, czx_pixel_format_t fmt,
                              uint8_t& r, uint8_t& g, uint8_t& b) noexcept {
    switch (fmt) {
        case CZX_PIXEL_RGB565: {
            const uint16_t p = reinterpret_cast<const uint16_t*>(row)[x];
            unpack_rgb565(p, r, g, b);
            break;
        }
        case CZX_PIXEL_XRGB8888: {
            const uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
            r = static_cast<uint8_t>((p >> 16) & 0xFFu);
            g = static_cast<uint8_t>((p >> 8) & 0xFFu);
            b = static_cast<uint8_t>(p & 0xFFu);
            break;
        }
        case CZX_PIXEL_ABGR8888: {
            const uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
            r = static_cast<uint8_t>(p & 0xFFu);
            g = static_cast<uint8_t>((p >> 8) & 0xFFu);
            b = static_cast<uint8_t>((p >> 16) & 0xFFu);
            break;
        }
    }
}

inline uint16_t read_pixel_rgb565(const uint8_t* row, uint32_t x, czx_pixel_format_t fmt) noexcept {
    if (fmt == CZX_PIXEL_RGB565) {
        return reinterpret_cast<const uint16_t*>(row)[x];
    }
    uint8_t r = 0, g = 0, b = 0;
    read_pixel_rgb888(row, x, fmt, r, g, b);
    return pack_rgb565(r, g, b);
}

}  // namespace czx::video
