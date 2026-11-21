#include "video/font6x8.hpp"
#include "video/font_jp.hpp"
#include "video/pixel.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace {

void check_valid_utf8(const std::string& s) {
    const char* p = s.c_str();
    while (*p != '\0') {
        const uint32_t cp = czx::video::next_utf8_codepoint(p);
        CHECK(cp != 0xFFFDu);
    }
}

void test_utf8_decode() {
    const char* valid = "A\xE3\x81\x82\xF0\x9F\x98\x80";  // 'A', U+3042, U+1F600
    CHECK(czx::video::next_utf8_codepoint(valid) == 'A');
    CHECK(czx::video::next_utf8_codepoint(valid) == 0x3042u);
    CHECK(czx::video::next_utf8_codepoint(valid) == 0x1F600u);
    CHECK(*valid == '\0');

    // Overlong & truncated sequences must emit 0xFFFD without overreading.
    const char* invalid = "\xC0\xAF\xE3\x81";
    CHECK(czx::video::next_utf8_codepoint(invalid) == 0xFFFDu);
    CHECK(czx::video::next_utf8_codepoint(invalid) == 0xFFFDu);
}

void test_sjis_decode() {
    // Shift_JIS: "システム" + halfwidth "ｱ" + ASCII "1"
    const std::string sjis = "\x83\x56\x83\x58\x83\x65\x83\x80\xB1" "1";
    const std::string u8 = czx::video::sjis_to_utf8(sjis);
    CHECK(u8 == "システムｱ1");

    // Shift_JIS: "日本語"
    CHECK(czx::video::sjis_to_utf8("\x93\xFA\x96\x7B\x8C\xEA") == "日本語");

    // Invalid lead byte and truncated trail byte fall back cleanly to '?'.
    CHECK(czx::video::sjis_to_utf8("\x80\x81") == "??");
}

void test_glyph_and_render() {
    const uint8_t* rows = nullptr;
    bool hw = false;
    CHECK(czx::video::lookup_misaki_glyph(0x65E5, rows, hw));  // '日'
    CHECK(rows != nullptr && !hw);
    CHECK(czx::video::lookup_misaki_glyph(0xFF71, rows, hw));  // 'ｱ'
    CHECK(rows != nullptr && hw);

    std::vector<uint16_t> fb(160 * 20, 0);
    czx::video::Rgb565Canvas canvas(fb.data(), 160, 20);
    const uint16_t white = czx::video::pack_rgb565(255, 255, 255);
    canvas.draw_text(2, 4, "PC88: システム起動", white, 1);

    int non_zero = 0;
    for (uint16_t px : fb) {
        if (px != 0) ++non_zero;
    }
    CHECK(non_zero > 40);

    CHECK(czx::video::Rgb565Canvas::text_width("ABC", 1) == 17);
    CHECK(czx::video::Rgb565Canvas::text_width("あいう", 1) == 23);
    CHECK(czx::video::Rgb565Canvas::text_width("ｱｲｳ", 1) == 14);
}

void test_truncation() {
    const std::string s = "日本語ファイルネーム_Disk1.d88";
    const std::string h = czx::video::truncate_head_px(s, 72, 1);
    check_valid_utf8(h);
    CHECK(h.size() >= 3 && h.substr(h.size() - 3) == "...");
    CHECK(czx::video::Rgb565Canvas::text_width(h.c_str(), 1) <= 72);

    const std::string t = czx::video::truncate_tail_px(s, 72, 1);
    check_valid_utf8(t);
    CHECK(t.size() >= 3 && t.substr(0, 3) == "...");
    CHECK(czx::video::Rgb565Canvas::text_width(t.c_str(), 1) <= 72);
}

}  // namespace

int main() {
    test_utf8_decode();
    test_sjis_decode();
    test_glyph_and_render();
    test_truncation();
    return 0;
}
