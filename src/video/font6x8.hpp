#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace czx::video {

// Returns 5 column bytes (bits 0..6 top-to-bottom) for printable ASCII.
const uint8_t* glyph_columns_5x7(char ch) noexcept;

uint32_t next_utf8_codepoint(const char*& p) noexcept;
void append_utf8(std::string& out, uint32_t cp);
std::string sjis_to_utf8(std::string_view sjis);
std::string truncate_head_px(const std::string& utf8, int max_px, int scale = 1);
std::string truncate_tail_px(const std::string& utf8, int max_px, int scale = 1);

class Rgb565Canvas {
public:
    Rgb565Canvas(uint16_t* pixels, int width, int height) noexcept
        : pixels_(pixels), width_(width), height_(height) {}

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

    void set_pixel(int x, int y, uint16_t color) noexcept;
    void blend_pixel(int x, int y, uint16_t color, uint8_t alpha) noexcept;
    void fill_rect(int x, int y, int w, int h, uint16_t color) noexcept;
    void fill_rect_alpha(int x, int y, int w, int h, uint16_t color, uint8_t alpha) noexcept;
    void draw_rect(int x, int y, int w, int h, uint16_t color) noexcept;
    void draw_hline(int x, int y, int w, uint16_t color) noexcept;
    void draw_vline(int x, int y, int h, uint16_t color) noexcept;

    void draw_char(int x, int y, char ch, uint16_t color, int scale = 1) noexcept;
    int draw_codepoint(int x, int y, uint32_t cp, uint16_t color, int scale = 1) noexcept;
    void draw_text(int x, int y, const char* text, uint16_t color, int scale = 1) noexcept;

    static int codepoint_width(uint32_t cp, int scale = 1) noexcept;
    static int text_width(const char* text, int scale = 1) noexcept;

    // Draws a 1-bit-per-pixel icon where bit (w - 1 - col) is the leftmost pixel.
    void draw_icon_bits(int x, int y, int w, int h, const uint16_t* rows, uint16_t color) noexcept;

private:
    uint16_t* pixels_;
    int width_;
    int height_;
};

}  // namespace czx::video
