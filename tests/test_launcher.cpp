#include "app/launcher_state.hpp"
#include "core/core_registry.hpp"
#include "czx/core.h"
#include "video/core_icons.hpp"
#include "video/launcher.hpp"
#include "video/scaler.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace {

czx_key_event_t make_key(czx_key_t k, bool pressed = true, bool repeat = false) {
    czx_key_event_t ev{};
    ev.key = k;
    ev.pressed = pressed;
    ev.repeat = repeat;
    return ev;
}

const czx_core_desc_t* dummy_get_desc(const czx_core_api_t* api) {
    return static_cast<const czx_core_desc_t*>(api->api_data);
}

void make_dummy_core(czx_core_desc_t& desc, czx_core_api_t& api, const char* id, const char* name) {
    desc = czx_core_desc_t{CZX_CORE_API_VERSION, id, name, 60, 1, 48000};
    std::memset(&api, 0, sizeof(api));
    api.api_data = &desc;
    api.get_desc = dummy_get_desc;
}

void test_core_icons() {
    using namespace czx::video;
    const char* keys[] = {
        "applewin",
        "c64",
        "vice_x64",
        "vice_x64sc",
        "quasi88",
        "np2kai",
        "bluemsx",
        "emuz700",
        "fm7",
        "efm7",
        "efm77av",
        "efm77av40",
        "msx68k",
        "g800",
        "test_pattern",
        "quit",
    };

    const CoreIconSprite c64 = find_core_icon("c64");
    const CoreIconSprite x64 = find_core_icon("vice_x64");
    const CoreIconSprite x64sc = find_core_icon("vice_x64sc");
    CHECK(c64.pixels != nullptr);
    CHECK(x64.pixels == c64.pixels);
    CHECK(x64sc.pixels == c64.pixels);

    const CoreIconSprite fm7 = find_core_icon("fm7");
    const CoreIconSprite efm7 = find_core_icon("efm7");
    const CoreIconSprite efm77av = find_core_icon("efm77av");
    const CoreIconSprite efm77av40 = find_core_icon("efm77av40");
    CHECK(fm7.pixels != nullptr);
    CHECK(efm7.pixels == fm7.pixels);
    CHECK(efm77av.pixels == fm7.pixels);
    CHECK(efm77av40.pixels == fm7.pixels);

    for (const char* k : keys) {
        const CoreIconSprite sp = find_core_icon(k);
        CHECK(sp.width == kCoreIconWidth);
        CHECK(sp.height == kCoreIconHeight);
        CHECK(sp.pixels != nullptr);
        int transparent_count = 0;
        int opaque_count = 0;
        const int total = sp.width * sp.height;
        for (int i = 0; i < total; ++i) {
            if (sp.pixels[i] == kCoreIconTransparent) {
                ++transparent_count;
            } else {
                ++opaque_count;
            }
        }
        CHECK(transparent_count > 100);
        CHECK(opaque_count > 200);
    }

    const CoreIconSprite test_sp = find_core_icon("test_pattern");
    const CoreIconSprite quit_sp = find_core_icon("quit");
    CHECK(quit_sp.pixels != nullptr);
    CHECK(quit_sp.pixels != test_sp.pixels);
    const CoreIconSprite unknown_sp = find_core_icon("nonexistent_core_id");
    CHECK(unknown_sp.pixels == test_sp.pixels);
    CHECK(unknown_sp.width == kCoreIconWidth);
    CHECK(unknown_sp.height == kCoreIconHeight);
}

void test_build_machine_targets() {
    using namespace czx::app;
    const char* ids[13] = {
        "applewin",
        "vice_x64",
        "vice_x64sc",
        "quasi88",
        "np2kai",
        "bluemsx",
        "emuz700",
        "efm7",
        "efm77av",
        "efm77av40",
        "msx68k",
        "g800",
        "test_pattern",
    };
    const char* names[13] = {
        "Apple II (AppleWin)",
        "C64 (VICE x64)",
        "C64 (VICE x64sc)",
        "PC-8801 (QUASI88)",
        "PC-9801 (NP2kai)",
        "MSX (blueMSX)",
        "MZ-700 (EmuZ-700)",
        "FM-7 (eFM-7)",
        "FM77AV (eFM77AV)",
        "FM77AV40 (eFM77AV40)",
        "X68000 (msx68k)",
        "PC-G850 (g800)",
        "CZX Resolution & Input Pattern",
    };

    czx_core_desc_t descs[13]{};
    czx_core_api_t apis[13]{};
    czx::core::CoreRegistry reg;
    for (int i = 0; i < 13; ++i) {
        make_dummy_core(descs[i], apis[i], ids[i], names[i]);
        reg.register_core(&apis[i]);
    }

    const auto targets = build_machine_targets(reg);
    CHECK(targets.size() == 10u);

    const char* expected_titles[10] = {
        "APPLE II",
        "COMMODORE 64",
        "PC-8801",
        "PC-9801",
        "MSX",
        "MZ-700",
        "FM-7",
        "X68000",
        "PC-G850",
        "TEST",
    };
    for (size_t i = 0; i < 10u; ++i) {
        CHECK(targets[i].title == expected_titles[i]);
    }

    const int c64_idx = find_target_index_by_id(targets, "c64");
    CHECK(c64_idx == 1);
    CHECK(targets[1].has_variants());
    CHECK(targets[1].variant_option_label == "Accuracy");
    CHECK(targets[1].variant_count() == 2);
    CHECK(targets[1].default_variant_index == 0);
    CHECK(targets[1].resolve_core_index(0) == 1);
    CHECK(targets[1].resolve_core_index(1) == 2);
    CHECK(targets[1].subtitle(0) == "VICE x64");
    CHECK(targets[1].subtitle(1) == "VICE x64sc");
    CHECK(targets[1].menu_core_name(0) == "C64 (VICE x64)");
    CHECK(targets[1].menu_core_name(1) == "C64 (VICE x64sc)");

    const int fm7_idx = find_target_index_by_id(targets, "fm7");
    CHECK(fm7_idx == 6);
    CHECK(targets[6].icon_key == "fm7");
    CHECK(targets[6].has_variants());
    CHECK(targets[6].variant_option_label == "Model");
    CHECK(targets[6].variant_count() == 3);
    CHECK(targets[6].default_variant_index == 2);  // fm77av40
    CHECK(targets[6].resolve_core_index(0) == 7);
    CHECK(targets[6].resolve_core_index(1) == 8);
    CHECK(targets[6].resolve_core_index(2) == 9);
    CHECK(targets[6].variant_id(0) == "fm7");
    CHECK(targets[6].variant_id(1) == "fm77av");
    CHECK(targets[6].variant_id(2) == "fm77av40");
    CHECK(targets[6].variant_label(0) == "FM-7");
    CHECK(targets[6].variant_label(1) == "FM77AV");
    CHECK(targets[6].variant_label(2) == "FM77AV40");
    CHECK(targets[6].subtitle(2) == "eFM77AV40");
    CHECK(targets[6].menu_core_name(2) == "FM77AV40 (eFM77AV40)");

    int vidx = -1;
    CHECK(find_target_index_for_core_index(targets, 1, &vidx) == 1);
    CHECK(vidx == 0);
    CHECK(find_target_index_for_core_index(targets, 2, &vidx) == 1);
    CHECK(vidx == 1);
    CHECK(find_target_index_for_core_index(targets, 8, &vidx) == 6);
    CHECK(vidx == 1);
    CHECK(find_target_index_for_core_index(targets, 9, &vidx) == 6);
    CHECK(vidx == 2);
    CHECK(find_target_index_for_core_index(targets, 10, &vidx) == 7);
    CHECK(vidx == 0);

    // Single VICE core (x64sc only) should produce C64 without variant toggle.
    czx::core::CoreRegistry reg_single;
    reg_single.register_core(&apis[2]);
    const auto single_targets = build_machine_targets(reg_single);
    CHECK(single_targets.size() == 1u);
    CHECK(single_targets[0].id == "c64");
    CHECK(single_targets[0].title == "COMMODORE 64");
    CHECK(!single_targets[0].has_variants());
    CHECK(single_targets[0].resolve_core_index(0) == 0);
    CHECK(single_targets[0].resolve_core_index(1) == 0);
    CHECK(single_targets[0].subtitle(0) == "VICE x64sc");

    // Partial FM-7 (efm7 + efm77av only) defaults to FM77AV and toggles 2 models.
    czx::core::CoreRegistry reg_fm7_partial;
    reg_fm7_partial.register_core(&apis[7]);
    reg_fm7_partial.register_core(&apis[8]);
    const auto partial_fm7 = build_machine_targets(reg_fm7_partial);
    CHECK(partial_fm7.size() == 1u);
    CHECK(partial_fm7[0].has_variants());
    CHECK(partial_fm7[0].default_variant_index == 1);
    CHECK(partial_fm7[0].variant_label(partial_fm7[0].default_variant_index) == "FM77AV");
}

void test_launcher_state_persistence() {
    using namespace czx::app;
    const fs::path tmp_dir =
        fs::temp_directory_path() / ("czx_test_launcher_state_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    const fs::path ini_path = tmp_dir / "saves" / "launcher_state.ini";

    // Missing file returns empty default state safely.
    const LauncherState missing = load_launcher_state(ini_path.string());
    CHECK(missing.last_target_id.empty());
    CHECK(missing.targets.empty());

    LauncherState st{};
    st.last_target_id = "c64";

    TargetSavedState c64{};
    c64.media1 = "/home/user/czx/media/c64/Giana.d64";
    c64.media1_label = "Giana.d64";
    c64.last_media_dir = "/home/user/czx/media/c64";
    c64.variant_id = "accurate";
    c64.region_id = "ntsc";
    c64.has_display_prefs = true;
    c64.display_mode = czx::video::DisplayMode::AspectFit;
    c64.desktop_native = true;
    c64.desktop_scale = 3;
    c64.aspect_policy = czx::video::AspectPolicy::Crt4x3;
    c64.show_borders = true;
    c64.margin_fill = czx::video::MarginFill::Black;
    c64.input_mode = 2;
    st.targets["c64"] = c64;

    TargetSavedState pc88{};
    pc88.media1 = "/tmp/cache/disk0.d88";
    pc88.media2 = "/tmp/cache/disk1.d88";
    pc88.media1_label = "Sorcerian [1/3]";
    pc88.media2_label = "Sorcerian [2/3]";
    pc88.d88_source1 = "/home/user/czx/media/pc88/Sorcerian.d88";
    pc88.d88_index1 = 0;
    pc88.d88_source2 = "/home/user/czx/media/pc88/Sorcerian.d88";
    pc88.d88_index2 = 1;
    pc88.preset_index = 2;
    st.targets["quasi88"] = pc88;

    TargetSavedState fm7{};
    fm7.variant_id = "fm77av";
    fm7.media1 = "/home/user/czx/media/fm7/Thexder.d77";
    fm7.media1_label = "Thexder.d77";
    fm7.has_display_prefs = true;
    fm7.aspect_policy = czx::video::AspectPolicy::FullStretch;
    st.targets["fm7"] = fm7;

    CHECK(save_launcher_state(ini_path.string(), st));
    CHECK(fs::is_regular_file(ini_path));
    CHECK(!fs::exists(ini_path.string() + ".tmp"));

    const LauncherState loaded = load_launcher_state(ini_path.string());
    CHECK(loaded.last_target_id == "c64");
    CHECK(loaded.targets.size() == 3u);

    const auto& lc64 = loaded.targets.at("c64");
    CHECK(lc64.media1 == c64.media1);
    CHECK(lc64.media1_label == c64.media1_label);
    CHECK(lc64.last_media_dir == c64.last_media_dir);
    CHECK(lc64.variant_id == "accurate");
    CHECK(lc64.region_id == "ntsc");
    CHECK(lc64.has_display_prefs);
    CHECK(lc64.display_mode == czx::video::DisplayMode::AspectFit);
    CHECK(lc64.desktop_native);
    CHECK(lc64.desktop_scale == 3);
    CHECK(lc64.aspect_policy == czx::video::AspectPolicy::Crt4x3);
    CHECK(lc64.show_borders);
    CHECK(lc64.margin_fill == czx::video::MarginFill::Black);
    CHECK(lc64.input_mode == 2);

    const auto& lfm7 = loaded.targets.at("fm7");
    CHECK(lfm7.variant_id == "fm77av");
    CHECK(lfm7.media1 == fm7.media1);
    CHECK(lfm7.aspect_policy == czx::video::AspectPolicy::FullStretch);

    const auto& lpc88 = loaded.targets.at("quasi88");
    CHECK(lpc88.variant_id.empty());
    CHECK(lpc88.media1 == pc88.media1);
    CHECK(lpc88.media2 == pc88.media2);
    CHECK(lpc88.media1_label == pc88.media1_label);
    CHECK(lpc88.media2_label == pc88.media2_label);
    CHECK(lpc88.d88_source1 == pc88.d88_source1);
    CHECK(lpc88.d88_index1 == 0);
    CHECK(lpc88.d88_source2 == pc88.d88_source2);
    CHECK(lpc88.d88_index2 == 1);
    CHECK(lpc88.preset_index == 2);

    // Legacy `cycle_accurate` INI migration for `c64` (and ignored for other targets).
    {
        std::ofstream ofs(ini_path, std::ios::trunc);
        ofs << "[target.c64]\n"
            << "cycle_accurate = true\n"
            << "[target.quasi88]\n"
            << "cycle_accurate = false\n";
    }
    const LauncherState legacy = load_launcher_state(ini_path.string());
    CHECK(legacy.targets.at("c64").variant_id == "accurate");
    CHECK(legacy.targets.at("quasi88").variant_id.empty());

    // Malformed INI content should be tolerated cleanly without crashing.
    {
        std::ofstream ofs(ini_path, std::ios::trunc);
        ofs << "stray_line_without_section\n"
            << "[unknown]\n"
            << "foo = bar\n"
            << "[target.g800]\n"
            << "desktop_scale = 99\n"
            << "input_mode = -5\n"
            << "d88_index1 = not_a_number\n"
            << "preset_index = -1\n"
            << "no_equals_line\n";
    }
    const LauncherState corrupt = load_launcher_state(ini_path.string());
    CHECK(corrupt.targets.count("g800") == 1u);
    CHECK(corrupt.targets.at("g800").desktop_scale == 3);
    CHECK(corrupt.targets.at("g800").input_mode == 0);
    CHECK(corrupt.targets.at("g800").d88_index1 == -1);
    CHECK(corrupt.targets.at("g800").preset_index == 0u);

    // Alias parsing for FullStretch ("full", "stretch", "2").
    {
        std::ofstream ofs(ini_path, std::ios::trunc);
        ofs << "[target.msx68k]\n"
            << "aspect_policy = full\n"
            << "[target.applewin]\n"
            << "aspect_policy = stretch\n"
            << "[target.bluemsx]\n"
            << "aspect_policy = 2\n";
    }
    const LauncherState aliases = load_launcher_state(ini_path.string());
    CHECK(aliases.targets.at("msx68k").aspect_policy == czx::video::AspectPolicy::FullStretch);
    CHECK(aliases.targets.at("applewin").aspect_policy == czx::video::AspectPolicy::FullStretch);
    CHECK(aliases.targets.at("bluemsx").aspect_policy == czx::video::AspectPolicy::FullStretch);

    fs::remove_all(tmp_dir, ec);
}

int count_nonzero(const std::vector<uint16_t>& fb) {
    int count = 0;
    for (uint16_t px : fb) {
        if (px != 0u) ++count;
    }
    return count;
}

void test_launcher_view() {
    using namespace czx::video;
    LauncherView lv;

    std::vector<LauncherItemView> items9 = {
        {"APPLE II", "AppleWin  |  Ready", "applewin"},
        {"COMMODORE 64", "VICE x64  |  Giana.d64", "c64"},
        {"PC-8801", "QUASI88  |  Sorcerian.d88", "quasi88"},
        {"PC-9801", "NP2kai  |  Ready", "np2kai"},
        {"MSX", "blueMSX  |  Ready", "bluemsx"},
        {"MZ-700", "EmuZ-700  |  Ready", "emuz700"},
        {"X68000", "msx68k  |  Ready", "msx68k"},
        {"PC-G850", "g800  |  Ready", "g800"},
        {"TEST", "Test Pattern  |  Apple II 40-col", "test_pattern"},
    };

    lv.set_selected(0, static_cast<int>(items9.size()), false);
    CHECK(lv.selected() == 0);
    CHECK(!lv.is_animating());

    // Wrapping left from 0 goes to 8 and starts slide animation.
    CHECK(lv.handle_key(make_key(CZX_KEY_LEFT), static_cast<int>(items9.size())) ==
          LauncherAction::SelectionChanged);
    CHECK(lv.selected() == 8);
    CHECK(lv.is_animating());

    // Wrapping right via C key returns to 0.
    CHECK(lv.handle_key(make_key(CZX_KEY_C), static_cast<int>(items9.size())) ==
          LauncherAction::SelectionChanged);
    CHECK(lv.selected() == 0);

    // Repeating Z key goes to 8.
    CHECK(lv.handle_key(make_key(CZX_KEY_Z), static_cast<int>(items9.size())) ==
          LauncherAction::SelectionChanged);
    CHECK(lv.selected() == 8);

    // Tick animation converges cleanly to static state.
    int ticks = 0;
    while (lv.is_animating() && ticks < 120) {
        lv.tick(0.016);
        ++ticks;
    }
    CHECK(!lv.is_animating());
    CHECK(ticks > 0 && ticks < 60);

    // Boot keys (repeat ignored).
    CHECK(lv.handle_key(make_key(CZX_KEY_ENTER), 9) == LauncherAction::BootSelected);
    CHECK(lv.handle_key(make_key(CZX_KEY_SPACE), 9) == LauncherAction::BootSelected);
    CHECK(lv.handle_key(make_key(CZX_KEY_ENTER, true, true), 9) == LauncherAction::None);

    // Menu keys (repeat ignored).
    CHECK(lv.handle_key(make_key(CZX_KEY_UP), 9) == LauncherAction::OpenMenu);
    CHECK(lv.handle_key(make_key(CZX_KEY_DOWN), 9) == LauncherAction::OpenMenu);
    CHECK(lv.handle_key(make_key(CZX_KEY_F), 9) == LauncherAction::OpenMenu);
    CHECK(lv.handle_key(make_key(CZX_KEY_X), 9) == LauncherAction::OpenMenu);
    CHECK(lv.handle_key(make_key(CZX_KEY_DOWN, true, true), 9) == LauncherAction::None);

    // Release ignored.
    CHECK(lv.handle_key(make_key(CZX_KEY_RIGHT, false), 9) == LauncherAction::None);

    // Render with 0, 1, 2, and 9 items.
    std::vector<uint16_t> fb(static_cast<size_t>(kLcdWidth * kLcdHeight), 0u);
    std::vector<LauncherItemView> items0;
    lv.render(fb.data(), items0);
    CHECK(count_nonzero(fb) > 5000);

    std::vector<LauncherItemView> items1 = {items9[0]};
    lv.set_selected(0, 1, false);
    std::fill(fb.begin(), fb.end(), 0u);
    lv.render(fb.data(), items1);
    CHECK(count_nonzero(fb) > 10000);

    std::vector<LauncherItemView> items2 = {items9[0], items9[1]};
    lv.set_selected(1, 2, false);
    std::fill(fb.begin(), fb.end(), 0u);
    lv.render(fb.data(), items2);
    CHECK(count_nonzero(fb) > 10000);

    lv.set_selected(2, 9, true, 1);
    std::fill(fb.begin(), fb.end(), 0u);
    lv.render(fb.data(), items9);
    CHECK(count_nonzero(fb) > 10000);
}

}  // namespace

int main() {
    test_core_icons();
    test_build_machine_targets();
    test_launcher_state_persistence();
    test_launcher_view();
    std::printf("test_launcher: all checks passed\n");
    return 0;
}
