#include "czx/core.h"
#include "video/hud.hpp"
#include "video/menu.hpp"
#include "video/scaler.hpp"

#include <algorithm>
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

int count_nonzero_in_rect(const std::vector<uint16_t>& fb, int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (fb[static_cast<size_t>(y) * czx::video::kLcdWidth + static_cast<size_t>(x)] != 0u) {
                ++count;
            }
        }
    }
    return count;
}

czx_key_event_t key_press(czx_key_t k) {
    czx_key_event_t ev{};
    ev.key = k;
    ev.pressed = true;
    return ev;
}

void test_format_activity_label() {
    using czx::video::format_activity_label;
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_READ, 42, 0u) == "TAPE 42%");
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_READ, -1, 0u) == "TAPE");
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_WRITE, 15, 0u) == "REC 15%");
    CHECK(format_activity_label(CZX_ACTIVITY_DISK, -1, 0x01u) == "FD1");
    CHECK(format_activity_label(CZX_ACTIVITY_DISK, -1, 0x02u) == "FD2");
    CHECK(format_activity_label(CZX_ACTIVITY_DISK, -1, 0x00u) == "DISK");
    CHECK(format_activity_label(CZX_ACTIVITY_QD, -1, 0u) == "QD");
    CHECK(format_activity_label(CZX_ACTIVITY_SIO, -1, 0u) == "SIO");
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_READ | CZX_ACTIVITY_DISK, 5, 0x01u) == "TAPE 5% FD1");
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_READ | CZX_ACTIVITY_FASTFORWARD, 42, 0u) ==
          "TAPE 42% >>");
    CHECK(format_activity_label(CZX_ACTIVITY_TAPE_READ | CZX_ACTIVITY_FASTFORWARD, -1, 0u) ==
          "TAPE >>");
    CHECK(format_activity_label(CZX_ACTIVITY_DISK | CZX_ACTIVITY_FASTFORWARD, -1, 0u) ==
          "DISK >>");
    CHECK(format_activity_label(CZX_ACTIVITY_FASTFORWARD, -1, 0u) == "WARP >>");
    CHECK(format_activity_label(0u, -1, 0u).empty());
}

void test_render_hud_activity_overlay() {
    const size_t fb_size = static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight);
    czx::video::ViewportLayout layout{};

    // Active + enabled draws only in bottom-right corner.
    std::vector<uint16_t> fb(fb_size, 0);
    czx::video::HudState hud{};
    hud.show_activity_indicator = true;
    hud.activity_flags = CZX_ACTIVITY_TAPE_READ;
    hud.activity_tape_percent = 37;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 240, 150, czx::video::kLcdWidth, czx::video::kLcdHeight) > 40);
    CHECK(count_nonzero_in_rect(fb, 0, 0, 160, 100) == 0);

    // Disabled suppresses overlay completely.
    std::fill(fb.begin(), fb.end(), 0);
    hud.show_activity_indicator = false;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 0, 0, czx::video::kLcdWidth, czx::video::kLcdHeight) == 0);

    // Menu open suppresses overlay completely.
    std::fill(fb.begin(), fb.end(), 0);
    hud.show_activity_indicator = true;
    hud.menu_open = true;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 0, 0, czx::video::kLcdWidth, czx::video::kLcdHeight) == 0);

    // Alt held places activity pill above the Alt bar (y in 120..143).
    std::fill(fb.begin(), fb.end(), 0);
    hud.menu_open = false;
    hud.alt_held = true;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 240, 120, czx::video::kLcdWidth, 144) > 40);
}

void test_overlay_menu() {
    czx::video::OverlayMenu menu;
    czx::video::MenuViewModel vm{};
    vm.core_name = "MZ-700 (EmuZ-700)";
    vm.has_media_browser = true;
    vm.is_tape_media = true;
    vm.can_eject = true;
    vm.src_width = 320;
    vm.src_height = 200;
    vm.show_activity_indicator = true;
    vm.tape_warp_enabled = true;

    menu.set_open(true);
    for (int i = 0; i < 7; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), vm) == czx::video::MenuAction::None);
    }
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), vm) == czx::video::MenuAction::ToggleActivityIndicator);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), vm) == czx::video::MenuAction::ToggleActivityIndicator);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), vm) == czx::video::MenuAction::ToggleActivityIndicator);

    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), vm) == czx::video::MenuAction::None);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), vm) == czx::video::MenuAction::ToggleTapeWarp);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), vm) == czx::video::MenuAction::ToggleTapeWarp);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), vm) == czx::video::MenuAction::ToggleTapeWarp);

    for (int i = 0; i < 3; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), vm) == czx::video::MenuAction::None);
    }
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), vm) == czx::video::MenuAction::Quit);

    // Wrap around from last item (11) back to Core (0).
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), vm) == czx::video::MenuAction::None);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), vm) == czx::video::MenuAction::CycleCoreNext);

    std::vector<uint16_t> fb(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0);
    menu.render(fb.data(), vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
}

}  // namespace

int main() {
    test_format_activity_label();
    test_render_hud_activity_overlay();
    test_overlay_menu();
    std::printf("test_hud_menu: all checks passed\n");
    return 0;
}
