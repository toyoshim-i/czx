#pragma once

#include <cstddef>
#include <cstdint>

namespace czx::video {

extern const size_t kMisakiGlyphCount;
extern const uint16_t kMisakiCodepoints[];
extern const uint8_t kMisakiGlyphs[][8];
extern const uint8_t kMisakiHalfWidth[];
extern const uint16_t kCp932ToUnicode[60 * 188];

inline bool lookup_misaki_glyph(uint16_t cp, const uint8_t*& out_rows8, bool& out_half_width) noexcept {
    size_t lo = 0;
    size_t hi = kMisakiGlyphCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2u;
        const uint16_t mid_cp = kMisakiCodepoints[mid];
        if (mid_cp < cp) {
            lo = mid + 1u;
        } else if (mid_cp > cp) {
            hi = mid;
        } else {
            out_rows8 = kMisakiGlyphs[mid];
            out_half_width = ((kMisakiHalfWidth[mid >> 3] >> (mid & 7u)) & 1u) != 0;
            return true;
        }
    }
    return false;
}

inline uint16_t lookup_cp932_pair(uint8_t lead, uint8_t trail) noexcept {
    int r = -1;
    if (lead >= 0x81u && lead <= 0x9Fu) {
        r = static_cast<int>(lead - 0x81u);
    } else if (lead >= 0xE0u && lead <= 0xFCu) {
        r = static_cast<int>(lead - 0xE0u) + 31;
    } else {
        return 0;
    }

    int c = -1;
    if (trail >= 0x40u && trail <= 0x7Eu) {
        c = static_cast<int>(trail - 0x40u);
    } else if (trail >= 0x80u && trail <= 0xFCu) {
        c = static_cast<int>(trail - 0x80u) + 63;
    } else {
        return 0;
    }

    return kCp932ToUnicode[r * 188 + c];
}

}  // namespace czx::video
