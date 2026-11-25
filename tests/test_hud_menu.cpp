#include "czx/core.h"
#include "video/hud.hpp"
#include "video/menu.hpp"
#include "video/pixel.hpp"
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

    // Desktop native mode renders 1X/2X/3X pills in the bottom Alt bar.
    std::fill(fb.begin(), fb.end(), 0);
    hud.desktop_native = true;
    hud.desktop_scale = 2;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 0, 144, czx::video::kLcdWidth, czx::video::kLcdHeight) > 400);

    // Fullscreen native mode renders PAR / 4:3 / FULL pills on slots 4 / 5 / 6.
    const uint16_t active_pill_bg = czx::video::pack_rgb565(18, 86, 118);
    const uint16_t inactive_pill_bg = czx::video::pack_rgb565(24, 30, 42);
    const size_t slot4_bg = 147u * czx::video::kLcdWidth + 27u;
    const size_t slot5_bg = 147u * czx::video::kLcdWidth + 84u;
    const size_t slot6_bg = 147u * czx::video::kLcdWidth + 139u;

    std::fill(fb.begin(), fb.end(), 0);
    hud.fullscreen_native = true;
    hud.aspect_policy = czx::video::AspectPolicy::ParAware;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[slot4_bg] == active_pill_bg);
    CHECK(fb[slot5_bg] == inactive_pill_bg);
    CHECK(fb[slot6_bg] == inactive_pill_bg);

    std::fill(fb.begin(), fb.end(), 0);
    hud.aspect_policy = czx::video::AspectPolicy::Crt4x3;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[slot4_bg] == inactive_pill_bg);
    CHECK(fb[slot5_bg] == active_pill_bg);
    CHECK(fb[slot6_bg] == inactive_pill_bg);

    std::fill(fb.begin(), fb.end(), 0);
    hud.aspect_policy = czx::video::AspectPolicy::FullStretch;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[slot4_bg] == inactive_pill_bg);
    CHECK(fb[slot5_bg] == inactive_pill_bg);
    CHECK(fb[slot6_bg] == active_pill_bg);
    hud.fullscreen_native = false;

    // Alt held with fps_x10 > 0 draws top-left FPS pill; releasing Alt hides it.
    std::fill(fb.begin(), fb.end(), 0);
    hud.fps_x10 = 312;
    hud.target_fps_x10 = 600;
    hud.alt_held = true;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 4, 4, 110, 18) > 40);

    std::fill(fb.begin(), fb.end(), 0);
    hud.alt_held = false;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 4, 4, 110, 18) == 0);

    // FPS indicator dot color reflects % of target_fps_x10 (PAL 50.1 FPS at 100% is green).
    const size_t dot_idx = 8u * czx::video::kLcdWidth + 8u;
    std::fill(fb.begin(), fb.end(), 0);
    hud.alt_held = true;
    hud.force_alt_bar = false;
    hud.menu_open = false;
    hud.target_fps_x10 = 501;
    hud.fps_x10 = 501;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[dot_idx] == czx::video::pack_rgb565(64, 235, 160));

    std::fill(fb.begin(), fb.end(), 0);
    hud.fps_x10 = 450;  // ~90% -> yellow
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[dot_idx] == czx::video::pack_rgb565(255, 204, 64));

    std::fill(fb.begin(), fb.end(), 0);
    hud.fps_x10 = 200;  // ~40% -> red
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(fb[dot_idx] == czx::video::pack_rgb565(255, 88, 72));

    // Volume OSD draws centered pill when running, top-right inside header when menu open, nothing when timer=0.
    std::fill(fb.begin(), fb.end(), 0);
    hud.alt_held = false;
    hud.show_activity_indicator = false;
    hud.fps_x10 = 0;
    hud.volume_osd_frames = 30;
    hud.volume_percent = 65;
    hud.volume_muted = false;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 99, 5, 221, 18) > 100);

    std::fill(fb.begin(), fb.end(), 0);
    hud.menu_open = true;
    hud.volume_percent = 30;
    hud.volume_muted = true;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 183, 4, 305, 17) > 100);

    std::fill(fb.begin(), fb.end(), 0);
    hud.menu_open = false;
    hud.volume_osd_frames = 0;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 0, 0, czx::video::kLcdWidth, czx::video::kLcdHeight) == 0);

    // force_alt_bar draws the amber ALT+ tab and bottom bar even when alt_held is false,
    // and suppresses top-left FPS indicator.
    std::fill(fb.begin(), fb.end(), 0);
    hud.alt_held = false;
    hud.force_alt_bar = true;
    hud.fps_x10 = 600;
    czx::video::render_hud(fb.data(), hud, layout);
    CHECK(count_nonzero_in_rect(fb, 4, 132, 46, 144) > 100);
    CHECK(count_nonzero_in_rect(fb, 0, 144, czx::video::kLcdWidth, czx::video::kLcdHeight) > 400);
    CHECK(count_nonzero_in_rect(fb, 4, 4, 70, 18) == 0);

    // NativePan horizontal scrollbar with alt_held=true starts at x=50, never overlapping ALT+ tab.
    std::fill(fb.begin(), fb.end(), 0);
    hud.force_alt_bar = false;
    hud.alt_held = true;
    hud.fps_x10 = 0;
    hud.display_mode = czx::video::DisplayMode::NativePan;
    czx::video::ViewportLayout pan_layout{};
    pan_layout.max_pan_x = 100;
    pan_layout.active_w = 420;
    pan_layout.pan_x = 0;
    czx::video::render_hud(fb.data(), hud, pan_layout);
    const uint16_t amber = czx::video::pack_rgb565(245, 200, 66);
    CHECK(fb[140u * czx::video::kLcdWidth + 6u] == amber);
    CHECK(fb[140u * czx::video::kLcdWidth + 45u] == amber);
    CHECK(count_nonzero_in_rect(fb, 50, 140, 120, 143) > 10);
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
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), vm) == czx::video::MenuAction::CycleCorePrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), vm) == czx::video::MenuAction::CycleCoreNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), vm) == czx::video::MenuAction::None);
    CHECK(menu.is_open());

    vm.core_pending = true;
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), vm) == czx::video::MenuAction::CycleCorePrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), vm) == czx::video::MenuAction::CycleCoreNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), vm) == czx::video::MenuAction::ApplyCore);
    CHECK(!menu.is_open());

    menu.set_open(true);
    CHECK(menu.handle_key(key_press(CZX_KEY_ESC), vm) == czx::video::MenuAction::Close);
    CHECK(!menu.is_open());

    menu.set_open(true);
    std::vector<uint16_t> fb(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0);
    menu.render(fb.data(), vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);

    std::fill(fb.begin(), fb.end(), 0);
    vm.core_pending = false;
    vm.desktop_native_active = true;
    vm.desktop_scale = 3;
    menu.render(fb.data(), vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);

    // Variant option (13 rows) right after Core row (C64 Accuracy + FM-7 Model).
    czx::video::MenuViewModel c64_vm{};
    c64_vm.core_name = "C64 (VICE x64)";
    c64_vm.has_variant_option = true;
    c64_vm.variant_option_label = "Accuracy";
    c64_vm.variant_value_label = "Fast";
    c64_vm.has_media_browser = true;
    menu.set_open(true);
    menu.reset_cursor();
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_vm) == czx::video::MenuAction::None);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), c64_vm) == czx::video::MenuAction::CycleVariantPrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), c64_vm) == czx::video::MenuAction::CycleVariantNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), c64_vm) == czx::video::MenuAction::CycleVariantNext);
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), c64_vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
    for (int i = 0; i < 12; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_vm) == czx::video::MenuAction::None);
    }
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), c64_vm) == czx::video::MenuAction::CycleCoreNext);

    czx::video::MenuViewModel fm7_vm{};
    fm7_vm.core_name = "FM77AV40 (eFM77AV40)";
    fm7_vm.has_variant_option = true;
    fm7_vm.variant_option_label = "Model";
    fm7_vm.variant_value_label = "FM77AV40";
    fm7_vm.has_media_browser = true;
    fm7_vm.drive_count = 2u;
    fm7_vm.drive1_name = "FDD 0";
    fm7_vm.drive2_name = "FDD 1";
    menu.set_open(true);
    menu.reset_cursor();
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), fm7_vm) == czx::video::MenuAction::None);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), fm7_vm) == czx::video::MenuAction::CycleVariantPrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), fm7_vm) == czx::video::MenuAction::CycleVariantNext);
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), fm7_vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);

    // Launcher pre-launch menu mode (from_launcher = true).
    czx::video::MenuViewModel launch_vm = c64_vm;
    launch_vm.from_launcher = true;
    menu.set_open(true);
    menu.reset_cursor();
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), launch_vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), launch_vm) == czx::video::MenuAction::ApplyCore);
    CHECK(!menu.is_open());
    menu.set_open(true);
    CHECK(menu.handle_key(key_press(CZX_KEY_ESC), launch_vm) == czx::video::MenuAction::Close);
    CHECK(!menu.is_open());

    // Core row set to Launcher (core_is_launcher = true).
    czx::video::MenuViewModel ret_vm{};
    ret_vm.core_name = "Launcher";
    ret_vm.core_is_launcher = true;
    ret_vm.core_pending = true;
    menu.set_open(true);
    menu.reset_cursor();
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), ret_vm);
    CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), ret_vm) == czx::video::MenuAction::ApplyCore);
    CHECK(!menu.is_open());

    // Region + VSync + Variant (15 rows with scrolling).
    czx::video::MenuViewModel c64_full_vm = c64_vm;
    c64_full_vm.has_region_option = true;
    c64_full_vm.region_value_label = "PAL (50Hz)";
    c64_full_vm.has_vsync_option = true;
    c64_full_vm.vsync_enabled = true;
    menu.set_open(true);
    menu.reset_cursor();
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_full_vm) == czx::video::MenuAction::None);  // 1: Variant
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_full_vm) == czx::video::MenuAction::None);  // 2: Region
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), c64_full_vm) == czx::video::MenuAction::CycleRegionPrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), c64_full_vm) == czx::video::MenuAction::CycleRegionNext);
    for (int i = 0; i < 4; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_full_vm) == czx::video::MenuAction::None);
    }  // 6: VSync
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), c64_full_vm) == czx::video::MenuAction::ToggleVsync);
    for (int i = 0; i < 8; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_full_vm) == czx::video::MenuAction::None);
        std::fill(fb.begin(), fb.end(), 0);
        menu.render(fb.data(), c64_full_vm);
        CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
    }  // 14: Quit -> wrap to 0: Core on next DOWN
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), c64_full_vm) == czx::video::MenuAction::None);
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), c64_full_vm);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), c64_full_vm) == czx::video::MenuAction::CycleCoreNext);

    // Non-fullscreen menu keeps Display -> VSync -> Aspect (rows 3, 4, 5 for 1-variant no-region vm).
    czx::video::MenuViewModel non_fs_vm{};
    non_fs_vm.core_name = "X68000 (msx68k)";
    non_fs_vm.fullscreen_native = false;
    non_fs_vm.desktop_native_active = true;
    non_fs_vm.desktop_scale = 2;
    non_fs_vm.has_vsync_option = true;
    non_fs_vm.vsync_enabled = true;
    menu.set_open(true);
    menu.reset_cursor();
    for (int i = 0; i < 3; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), non_fs_vm) == czx::video::MenuAction::None);
    }  // 3: Display
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), non_fs_vm) == czx::video::MenuAction::CycleDisplayModeNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), non_fs_vm) == czx::video::MenuAction::None);  // 4: VSync
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), non_fs_vm) == czx::video::MenuAction::ToggleVsync);
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), non_fs_vm) == czx::video::MenuAction::None);  // 5: Aspect
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), non_fs_vm) == czx::video::MenuAction::CycleAspectPolicyPrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), non_fs_vm) == czx::video::MenuAction::CycleAspectPolicyNext);

    // Fullscreen native menu omits Display row and places Aspect right after Drive2OrEject (index 3).
    czx::video::MenuViewModel fs_vm = non_fs_vm;
    fs_vm.fullscreen_native = true;
    menu.set_open(true);
    menu.reset_cursor();
    for (int i = 0; i < 3; ++i) {
        CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), fs_vm) == czx::video::MenuAction::None);
    }  // 3: Aspect
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), fs_vm) == czx::video::MenuAction::CycleAspectPolicyPrev);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), fs_vm) == czx::video::MenuAction::CycleAspectPolicyNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), fs_vm) == czx::video::MenuAction::CycleAspectPolicyNext);
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), fs_vm) == czx::video::MenuAction::None);  // 4: VSync
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), fs_vm) == czx::video::MenuAction::ToggleVsync);
    for (const auto pol : {czx::video::AspectPolicy::ParAware,
                           czx::video::AspectPolicy::Crt4x3,
                           czx::video::AspectPolicy::FullStretch}) {
        fs_vm.aspect_policy = pol;
        std::fill(fb.begin(), fb.end(), 0);
        menu.render(fb.data(), fs_vm);
        CHECK(count_nonzero_in_rect(fb, 12, 3, 308, 167) > 1000);
    }

    // X68000 ADPCM LPF + CRT Whine + MT-32 toggle rows right after Core.
    czx::video::MenuViewModel x68_vm = fs_vm;
    x68_vm.has_adpcm_lpf_option = true;
    x68_vm.adpcm_lpf_enabled = true;
    x68_vm.has_crt_whine_option = true;
    x68_vm.crt_whine_enabled = false;
    x68_vm.has_mt32_option = true;
    x68_vm.mt32_value_label = "CM-32L";
    menu.set_open(true);
    menu.reset_cursor();
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), x68_vm) == czx::video::MenuAction::None);  // 1: ADPCM LPF
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), x68_vm) == czx::video::MenuAction::ToggleAdpcmLpf);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), x68_vm) == czx::video::MenuAction::ToggleAdpcmLpf);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), x68_vm) == czx::video::MenuAction::ToggleAdpcmLpf);
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), x68_vm) == czx::video::MenuAction::None);  // 2: CRT Whine
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), x68_vm) == czx::video::MenuAction::ToggleCrtWhine);
    CHECK(menu.handle_key(key_press(CZX_KEY_LEFT), x68_vm) == czx::video::MenuAction::ToggleCrtWhine);
    CHECK(menu.handle_key(key_press(CZX_KEY_RIGHT), x68_vm) == czx::video::MenuAction::ToggleCrtWhine);
    CHECK(menu.handle_key(key_press(CZX_KEY_DOWN), x68_vm) == czx::video::MenuAction::None);  // 3: MT-32
    CHECK(menu.handle_key(key_press(CZX_KEY_ENTER), x68_vm) == czx::video::MenuAction::ToggleMt32);
    std::fill(fb.begin(), fb.end(), 0);
    menu.render(fb.data(), x68_vm);
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
