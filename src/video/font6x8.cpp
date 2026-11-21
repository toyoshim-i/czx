#include "video/font6x8.hpp"

#include "video/font_jp.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace czx::video {

namespace {

// Classic 5x7 column-major ASCII font (0x20..0x7E). Bit 0 = top row, bit 6 = bottom row.
constexpr uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x10, 0x08, 0x08, 0x10, 0x08}, // '~'
};

}  // namespace

const uint8_t* glyph_columns_5x7(char ch) noexcept {
    const unsigned char uch = static_cast<unsigned char>(ch);
    const int idx = (uch >= 0x20 && uch <= 0x7E) ? (uch - 0x20) : ('?' - 0x20);
    return kFont5x7[idx];
}

uint32_t next_utf8_codepoint(const char*& p) noexcept {
    if (!p || *p == '\0') return 0;
    const uint8_t b0 = static_cast<uint8_t>(*p++);
    if (b0 < 0x80u) {
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        const uint8_t b1 = static_cast<uint8_t>(p[0]);
        if ((b1 & 0xC0u) == 0x80u) {
            const uint32_t cp = (static_cast<uint32_t>(b0 & 0x1Fu) << 6) |
                                static_cast<uint32_t>(b1 & 0x3Fu);
            if (cp >= 0x80u) {
                ++p;
                return cp;
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        const uint8_t b1 = static_cast<uint8_t>(p[0]);
        if ((b1 & 0xC0u) == 0x80u) {
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            if ((b2 & 0xC0u) == 0x80u) {
                const uint32_t cp = (static_cast<uint32_t>(b0 & 0x0Fu) << 12) |
                                    (static_cast<uint32_t>(b1 & 0x3Fu) << 6) |
                                    static_cast<uint32_t>(b2 & 0x3Fu);
                if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) {
                    p += 2;
                    return cp;
                }
            }
        }
        return 0xFFFDu;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        const uint8_t b1 = static_cast<uint8_t>(p[0]);
        if ((b1 & 0xC0u) == 0x80u) {
            const uint8_t b2 = static_cast<uint8_t>(p[1]);
            if ((b2 & 0xC0u) == 0x80u) {
                const uint8_t b3 = static_cast<uint8_t>(p[2]);
                if ((b3 & 0xC0u) == 0x80u) {
                    const uint32_t cp = (static_cast<uint32_t>(b0 & 0x07u) << 18) |
                                        (static_cast<uint32_t>(b1 & 0x3Fu) << 12) |
                                        (static_cast<uint32_t>(b2 & 0x3Fu) << 6) |
                                        static_cast<uint32_t>(b3 & 0x3Fu);
                    if (cp >= 0x10000u && cp <= 0x10FFFFu) {
                        p += 3;
                        return cp;
                    }
                }
            }
        }
        return 0xFFFDu;
    }
    return 0xFFFDu;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        append_utf8(out, 0xFFFDu);
    }
}

std::string sjis_to_utf8(std::string_view sjis) {
    std::string out;
    out.reserve(sjis.size() * 2u);
    for (size_t i = 0; i < sjis.size(); ++i) {
        const uint8_t b = static_cast<uint8_t>(sjis[i]);
        if (b < 0x80u) {
            out.push_back(static_cast<char>(b));
        } else if (b >= 0xA1u && b <= 0xDFu) {
            append_utf8(out, 0xFF61u + static_cast<uint32_t>(b - 0xA1u));
        } else if ((b >= 0x81u && b <= 0x9Fu) || (b >= 0xE0u && b <= 0xFCu)) {
            if (i + 1u < sjis.size()) {
                const uint8_t b2 = static_cast<uint8_t>(sjis[i + 1u]);
                const uint16_t u = lookup_cp932_pair(b, b2);
                ++i;
                if (u != 0) {
                    append_utf8(out, u);
                } else {
                    out.push_back('?');
                }
            } else {
                out.push_back('?');
            }
        } else {
            out.push_back('?');
        }
    }
    return out;
}

std::string truncate_head_px(const std::string& utf8, int max_px, int scale) {
    if (scale <= 0 || max_px <= 0) return {};
    if (Rgb565Canvas::text_width(utf8.c_str(), scale) <= max_px) return utf8;

    const int dots_w = Rgb565Canvas::text_width("...", scale);
    if (max_px <= dots_w) return "...";

    const char* base = utf8.c_str();
    const char* p = base;
    size_t best_end = 0;
    int accum_w = 0;
    while (*p != '\0') {
        const uint32_t cp = next_utf8_codepoint(p);
        const int cw = Rgb565Canvas::codepoint_width(cp, scale);
        if (accum_w + dots_w > max_px) break;
        accum_w += cw;
        if (accum_w + dots_w > max_px) break;
        best_end = static_cast<size_t>(p - base);
    }
    return utf8.substr(0, best_end) + "...";
}

std::string truncate_tail_px(const std::string& utf8, int max_px, int scale) {
    if (scale <= 0 || max_px <= 0) return {};
    if (Rgb565Canvas::text_width(utf8.c_str(), scale) <= max_px) return utf8;

    const int dots_w = Rgb565Canvas::text_width("...", scale);
    if (max_px <= dots_w) return "...";

    struct CpSpan {
        size_t start;
        int width;
    };
    std::vector<CpSpan> spans;
    const char* base = utf8.c_str();
    const char* p = base;
    while (*p != '\0') {
        const size_t start = static_cast<size_t>(p - base);
        const uint32_t cp = next_utf8_codepoint(p);
        spans.push_back(CpSpan{start, Rgb565Canvas::codepoint_width(cp, scale)});
    }

    size_t best_start = utf8.size();
    int suffix_w = 0;
    for (size_t idx = spans.size(); idx > 0; --idx) {
        const auto& sp = spans[idx - 1u];
        const int next_w = suffix_w + sp.width;
        if (dots_w + scale + next_w - scale > max_px) break;
        suffix_w = next_w;
        best_start = sp.start;
    }
    return "..." + utf8.substr(best_start);
}

void Rgb565Canvas::set_pixel(int x, int y, uint16_t color) noexcept {
    if (!pixels_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    pixels_[y * width_ + x] = color;
}

void Rgb565Canvas::blend_pixel(int x, int y, uint16_t color, uint8_t alpha) noexcept {
    if (!pixels_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint16_t& dst = pixels_[y * width_ + x];
    dst = blend_rgb565(dst, color, alpha);
}

void Rgb565Canvas::fill_rect(int x, int y, int w, int h, uint16_t color) noexcept {
    if (!pixels_ || w <= 0 || h <= 0) return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width_, x + w);
    const int y1 = std::min(height_, y + h);
    for (int py = y0; py < y1; ++py) {
        uint16_t* row = pixels_ + py * width_;
        for (int px = x0; px < x1; ++px) {
            row[px] = color;
        }
    }
}

void Rgb565Canvas::fill_rect_alpha(int x, int y, int w, int h, uint16_t color, uint8_t alpha) noexcept {
    if (!pixels_ || w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha == 255) {
        fill_rect(x, y, w, h, color);
        return;
    }
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width_, x + w);
    const int y1 = std::min(height_, y + h);
    for (int py = y0; py < y1; ++py) {
        uint16_t* row = pixels_ + py * width_;
        for (int px = x0; px < x1; ++px) {
            row[px] = blend_rgb565(row[px], color, alpha);
        }
    }
}

void Rgb565Canvas::draw_hline(int x, int y, int w, uint16_t color) noexcept {
    fill_rect(x, y, w, 1, color);
}

void Rgb565Canvas::draw_vline(int x, int y, int h, uint16_t color) noexcept {
    fill_rect(x, y, 1, h, color);
}

void Rgb565Canvas::draw_rect(int x, int y, int w, int h, uint16_t color) noexcept {
    if (w <= 0 || h <= 0) return;
    draw_hline(x, y, w, color);
    draw_hline(x, y + h - 1, w, color);
    draw_vline(x, y, h, color);
    draw_vline(x + w - 1, y, h, color);
}

void Rgb565Canvas::draw_char(int x, int y, char ch, uint16_t color, int scale) noexcept {
    if (scale <= 0) return;
    const uint8_t* cols = glyph_columns_5x7(ch);
    for (int col = 0; col < 5; ++col) {
        const uint8_t bits = cols[col];
        for (int row = 0; row < 7; ++row) {
            if ((bits & (1u << row)) != 0) {
                if (scale == 1) {
                    set_pixel(x + col, y + row, color);
                } else {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

int Rgb565Canvas::codepoint_width(uint32_t cp, int scale) noexcept {
    if (scale <= 0) return 0;
    if (cp < 0x80u) {
        return 6 * scale;
    }
    if (cp <= 0xFFFFu) {
        const uint8_t* rows = nullptr;
        bool half_width = false;
        if (lookup_misaki_glyph(static_cast<uint16_t>(cp), rows, half_width)) {
            return (half_width ? 5 : 8) * scale;
        }
    }
    return 6 * scale;
}

int Rgb565Canvas::draw_codepoint(int x, int y, uint32_t cp, uint16_t color, int scale) noexcept {
    if (scale <= 0) return 0;
    if (cp < 0x80u) {
        draw_char(x, y, static_cast<char>(cp), color, scale);
        return 6 * scale;
    }
    if (cp <= 0xFFFFu) {
        const uint8_t* rows = nullptr;
        bool half_width = false;
        if (lookup_misaki_glyph(static_cast<uint16_t>(cp), rows, half_width)) {
            for (int row = 0; row < 8; ++row) {
                const uint8_t bits = rows[row];
                if (bits == 0) continue;
                for (int col = 0; col < 8; ++col) {
                    if ((bits & (1u << (7 - col))) != 0) {
                        if (scale == 1) {
                            set_pixel(x + col, y + row, color);
                        } else {
                            fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                        }
                    }
                }
            }
            return (half_width ? 5 : 8) * scale;
        }
    }
    draw_char(x, y, '?', color, scale);
    return 6 * scale;
}

void Rgb565Canvas::draw_text(int x, int y, const char* text, uint16_t color, int scale) noexcept {
    if (!text || scale <= 0) return;
    int cx = x;
    const char* p = text;
    while (*p != '\0') {
        const uint32_t cp = next_utf8_codepoint(p);
        cx += draw_codepoint(cx, y, cp, color, scale);
    }
}

int Rgb565Canvas::text_width(const char* text, int scale) noexcept {
    if (!text || scale <= 0 || text[0] == '\0') return 0;
    int total = 0;
    const char* p = text;
    while (*p != '\0') {
        const uint32_t cp = next_utf8_codepoint(p);
        total += codepoint_width(cp, scale);
    }
    return std::max(0, total - scale);
}

void Rgb565Canvas::draw_icon_bits(int x, int y, int w, int h, const uint16_t* rows, uint16_t color) noexcept {
    if (!rows || w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r) {
        const uint16_t bits = rows[r];
        for (int c = 0; c < w; ++c) {
            if ((bits & (1u << (w - 1 - c))) != 0) {
                set_pixel(x + c, y + r, color);
            }
        }
    }
}

}  // namespace czx::video
