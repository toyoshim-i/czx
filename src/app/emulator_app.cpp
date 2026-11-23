#include "app/emulator_app.hpp"

#include "core/d88_image.hpp"
#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace czx::app {

namespace {

volatile sig_atomic_t g_signal_quit = 0;

void on_signal(int) {
    g_signal_quit = 1;
}

std::string normalize_abs_path(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    const fs::path abs = fs::absolute(fs::path(path), ec);
    if (ec) return path;
    return abs.lexically_normal().string();
}

std::string resolve_data_dir(const std::string& explicit_dir) {
    if (!explicit_dir.empty()) return explicit_dir;
    if (const char* env = std::getenv("CZX_DATA_DIR"); env && env[0] != '\0') return env;
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return (fs::path(home) / "czx").string();
    }
    return "/tmp/czx";
}

bool iequals_ascii(const std::string& a, const char* b) {
    const size_t blen = std::strlen(b);
    if (a.size() != blen) return false;
    for (size_t i = 0; i < blen; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

bool has_file_icase(const fs::path& dir, const char* filename) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
        if (iequals_ascii(entry.path().filename().string(), filename) && entry.file_size(ec) > 0) {
            return true;
        }
    }
    return false;
}

bool is_tape_media_label(const std::string& label) {
    std::string ext = fs::path(label).extension().string();
    for (char& ch : ext) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return ext == ".tap" || ext == ".mzt" || ext == ".mzf" || ext == ".m12" ||
           ext == ".cas" || ext == ".wav" || ext == ".t77" || ext == ".cmt" || ext == ".t88";
}

bool is_combo_media_ext(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (char& ch : ext) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return ext == ".cmt" || ext == ".t88" || ext == ".m3u" || ext == ".zip" || ext == ".cmd";
}

bool resolve_saved_drive_media(const std::string& media_path,
                               const std::string& label,
                               const std::string& d88_source,
                               int d88_index,
                               const std::string& d88_cache_dir,
                               std::string& out_path,
                               std::string& out_label,
                               std::string& out_d88_src,
                               int& out_d88_idx) {
    std::error_code ec;
    if (!media_path.empty() && fs::is_regular_file(media_path, ec)) {
        out_path = media_path;
        out_label = !label.empty() ? label : fs::path(media_path).filename().string();
        if (!d88_source.empty() && d88_index >= 0 && fs::is_regular_file(d88_source, ec)) {
            out_d88_src = d88_source;
            out_d88_idx = d88_index;
        } else {
            out_d88_src.clear();
            out_d88_idx = -1;
        }
        return true;
    }

    if (!d88_source.empty() && d88_index >= 0 && !d88_cache_dir.empty() &&
        fs::is_regular_file(d88_source, ec) && czx::core::is_d88_ext(d88_source)) {
        std::vector<czx::core::D88SubDisk> disks;
        if (czx::core::inspect_d88(d88_source, disks) &&
            static_cast<size_t>(d88_index) < disks.size()) {
            const std::string ext_path = czx::core::extract_d88_subdisk(
                d88_source, disks[static_cast<size_t>(d88_index)], d88_cache_dir);
            if (!ext_path.empty() && fs::is_regular_file(ext_path, ec)) {
                out_path = ext_path;
                out_label = !label.empty() ? label : disks[static_cast<size_t>(d88_index)].name_utf8;
                out_d88_src = d88_source;
                out_d88_idx = d88_index;
                return true;
            }
        }
    }

    out_path.clear();
    out_label.clear();
    out_d88_src.clear();
    out_d88_idx = -1;
    return false;
}

std::string normalize_c64_region(std::string r) {
    for (char& ch : r) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (r == "pal" || r == "ntsc") {
        return r;
    }
    return "auto";
}

const char* region_display_label(const std::string& r) noexcept {
    if (r == "pal") return "PAL (50Hz)";
    if (r == "ntsc") return "NTSC (60Hz)";
    return "Auto";
}

const char* vice_model_for_region(const std::string& r) noexcept {
    if (r == "pal") return "C64 PAL";
    if (r == "ntsc") return "C64 NTSC";
    return "C64 PAL auto";
}

}  // namespace

EmulatorApp::EmulatorApp(AppConfig cfg)
    : cfg_(std::move(cfg)),
      lcd_fb_(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0),
      work_fb_(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0),
      ready_fb_(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0) {
    cfg_.data_dir = normalize_abs_path(resolve_data_dir(cfg_.data_dir));
    if (!cfg_.core_dir.empty()) cfg_.core_dir = normalize_abs_path(cfg_.core_dir);
    if (!cfg_.media_path.empty()) cfg_.media_path = normalize_abs_path(cfg_.media_path);
    if (!cfg_.media2_path.empty()) cfg_.media2_path = normalize_abs_path(cfg_.media2_path);
    setenv("CZX_DATA_DIR", cfg_.data_dir.c_str(), 1);
    ensure_data_dirs();

    registry_ = czx::core::CoreRegistry::create_default(cfg_.core_dir, cfg_.data_dir);
    targets_ = build_machine_targets(registry_);
    launcher_state_path_ = (fs::path(cfg_.data_dir) / "saves" / "launcher_state.ini").string();
    scaler_cfg_.mode = cfg_.display_mode;
    scaler_cfg_.aspect_policy = cfg_.aspect_policy;
    scaler_cfg_.show_borders = cfg_.show_borders;
    scaler_cfg_.margin_fill = cfg_.margin_fill;
    show_activity_indicator_ = cfg_.show_activity_indicator;
    tape_warp_enabled_ = cfg_.tape_warp_enabled;
    vsync_enabled_ = cfg_.vsync_enabled;
    load_app_config();
    desktop_scale_ = std::clamp(cfg_.desktop_scale, 1, 3);
    default_desktop_scale_ = desktop_scale_;
    hud_.desktop_scale = desktop_scale_;
    hud_.aspect_policy = scaler_cfg_.aspect_policy;
    hud_.show_activity_indicator = show_activity_indicator_;
    preset_index_ = cfg_.preset_index;
}

EmulatorApp::~EmulatorApp() {
    stop_scale_worker();
    destroy_active_core();
}

void EmulatorApp::ensure_data_dirs() {
    std::error_code ec;
    const fs::path root(cfg_.data_dir);
    fs::create_directories(root / "cores", ec);
    fs::create_directories(root / "roms" / "apple2", ec);
    fs::create_directories(root / "roms" / "vice" / "C64", ec);
    fs::create_directories(root / "roms" / "vice" / "DRIVES", ec);
    fs::create_directories(root / "roms" / "quasi88", ec);
    fs::create_directories(root / "roms" / "np2kai", ec);
    fs::create_directories(root / "roms" / "bluemsx" / "Machines" / "Shared Roms", ec);
    fs::create_directories(root / "roms" / "bluemsx" / "Databases", ec);
    fs::create_directories(root / "roms" / "g800", ec);
    fs::create_directories(root / "roms" / "mz700", ec);
    fs::create_directories(root / "roms" / "fm7", ec);
    fs::create_directories(root / "roms" / "x68000", ec);
    fs::create_directories(root / "media" / "apple2", ec);
    fs::create_directories(root / "media" / "c64", ec);
    fs::create_directories(root / "media" / "pc88", ec);
    fs::create_directories(root / "media" / "pc98", ec);
    fs::create_directories(root / "media" / "msx", ec);
    fs::create_directories(root / "media" / "g800", ec);
    fs::create_directories(root / "media" / "mz700", ec);
    fs::create_directories(root / "media" / "fm7", ec);
    fs::create_directories(root / "media" / "x68000", ec);
    fs::create_directories(root / "saves", ec);
    fs::create_directories(root / "config", ec);
}

void EmulatorApp::load_app_config() {
    if (cfg_.activity_indicator_override && cfg_.tape_warp_override && cfg_.vsync_override) return;
    const fs::path cfg_path = fs::path(cfg_.data_dir) / "config" / "czx.cfg";
    std::ifstream ifs(cfg_path);
    if (!ifs.is_open()) return;

    auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \t\r\n\"");
        if (a == std::string::npos) return std::string{};
        const auto b = s.find_last_not_of(" \t\r\n\"");
        return s.substr(a, b - a + 1);
    };

    std::string line;
    while (std::getline(ifs, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        for (char& ch : val) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        auto parse_bool = [&](bool& dst) {
            if (val == "off" || val == "0" || val == "false" || val == "disabled") {
                dst = false;
            } else if (val == "on" || val == "1" || val == "true" || val == "enabled") {
                dst = true;
            }
        };
        if (key == "access_led" && !cfg_.activity_indicator_override) {
            parse_bool(show_activity_indicator_);
        } else if (key == "tape_warp" && !cfg_.tape_warp_override) {
            parse_bool(tape_warp_enabled_);
        } else if (key == "vsync" && !cfg_.vsync_override) {
            parse_bool(vsync_enabled_);
        }
    }
}

void EmulatorApp::save_app_config() const {
    const fs::path cfg_path = fs::path(cfg_.data_dir) / "config" / "czx.cfg";
    std::error_code ec;
    fs::create_directories(cfg_path.parent_path(), ec);

    std::vector<std::string> lines;
    bool updated_led = false;
    bool updated_warp = false;
    bool updated_vsync = false;
    {
        std::ifstream ifs(cfg_path);
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                const auto a = key.find_first_not_of(" \t");
                const auto b = key.find_last_not_of(" \t");
                key = (a == std::string::npos) ? std::string{} : key.substr(a, b - a + 1);
                if (key == "access_led") {
                    line = std::string("access_led = ") + (show_activity_indicator_ ? "on" : "off");
                    updated_led = true;
                } else if (key == "tape_warp") {
                    line = std::string("tape_warp = ") + (tape_warp_enabled_ ? "on" : "off");
                    updated_warp = true;
                } else if (key == "vsync") {
                    line = std::string("vsync = ") + (vsync_enabled_ ? "on" : "off");
                    updated_vsync = true;
                }
            }
            lines.push_back(std::move(line));
        }
    }
    if (!updated_led) {
        lines.emplace_back(std::string("access_led = ") + (show_activity_indicator_ ? "on" : "off"));
    }
    if (!updated_warp) {
        lines.emplace_back(std::string("tape_warp = ") + (tape_warp_enabled_ ? "on" : "off"));
    }
    if (!updated_vsync) {
        lines.emplace_back(std::string("vsync = ") + (vsync_enabled_ ? "on" : "off"));
    }

    std::ofstream ofs(cfg_path, std::ios::trunc);
    if (!ofs.is_open()) return;
    for (const auto& l : lines) {
        ofs << l << '\n';
    }
}

void EmulatorApp::sync_core_warp_option() {
    if (!core_api_ || !core_inst_ || !core_api_->set_option || !core_api_->get_desc) return;
    const czx_core_desc_t* desc = core_api_->get_desc(core_api_);
    const std::string id = (desc && desc->id) ? desc->id : "";
    const char* val = tape_warp_enabled_ ? "enabled" : "disabled";
    if (id.rfind("vice", 0) == 0) {
        core_api_->set_option(core_inst_, "vice_autoloadwarp", val);
    } else if (id == "emuz700") {
        core_api_->set_option(core_inst_, "emuz700_tape_turbo", val);
    } else if (id.rfind("efm7", 0) == 0) {
        core_api_->set_option(core_inst_, "efm7_tape_turbo", val);
    }
}

std::string EmulatorApp::resolve_target_region_id(int target_index) const {
    if (target_index >= 0 && static_cast<size_t>(target_index) < targets_.size() &&
        targets_[static_cast<size_t>(target_index)].id == "c64") {
        const TargetSavedState* st = find_target_saved_state(target_index);
        if (st && !st->region_id.empty()) {
            return normalize_c64_region(st->region_id);
        }
        return "auto";
    }
    const auto it = launcher_state_.targets.find("c64");
    if (it != launcher_state_.targets.end() && !it->second.region_id.empty()) {
        return normalize_c64_region(it->second.region_id);
    }
    return "auto";
}

void EmulatorApp::sync_core_region_option() {
    if (!core_api_ || !core_inst_ || !core_api_->set_option || !core_api_->get_desc) return;
    const czx_core_desc_t* desc = core_api_->get_desc(core_api_);
    const std::string id = (desc && desc->id) ? desc->id : "";
    if (id.rfind("vice", 0) == 0) {
        core_api_->set_option(core_inst_, "vice_c64_model", vice_model_for_region(c64_region_));
    }
}

void EmulatorApp::sync_core_gpu_scale_option() {
    if (!core_api_ || !core_inst_ || !core_api_->set_option || !core_api_->get_desc) return;
    const czx_core_desc_t* desc = core_api_->get_desc(core_api_);
    const std::string id = (desc && desc->id) ? desc->id : "";
    if (id != "msx68k") return;

    const char* val = "off";
    if (!hw_render_active_ && !desktop_native_mode_ && scaler_cfg_.mode != czx::video::DisplayMode::NativePan) {
        if (scaler_cfg_.mode == czx::video::DisplayMode::OverflowFit) {
            val = "axis";
        } else {
            switch (scaler_cfg_.aspect_policy) {
                case czx::video::AspectPolicy::ParAware:
                    val = "fit_par";
                    break;
                case czx::video::AspectPolicy::Crt4x3:
                    val = "fit_4x3";
                    break;
                case czx::video::AspectPolicy::FullStretch:
                    val = "off";
                    break;
            }
        }
    }
    core_api_->set_option(core_inst_, "msx68k_gpu_scale", val);
}

void EmulatorApp::clear_activity_state() noexcept {
    live_warp_requested_ = false;
    activity_flags_ = 0u;
    activity_tape_percent_ = -1;
    activity_drive_mask_ = 0u;
    activity_hold_frames_ = 0u;
    hud_.activity_flags = 0u;
    hud_.activity_tape_percent = -1;
    hud_.activity_drive_mask = 0u;
}

void EmulatorApp::poll_core_activity() {
    czx_activity_t act{};
    act.tape_percent = -1;
    if (core_api_ && core_inst_ && core_api_->get_activity) {
        (void)core_api_->get_activity(core_inst_, &act);
    }
    live_warp_requested_ = (act.flags & CZX_ACTIVITY_FASTFORWARD) != 0u;
    if (!tape_warp_enabled_) {
        act.flags &= ~static_cast<uint32_t>(CZX_ACTIVITY_FASTFORWARD);
    }

    if (act.flags != 0u) {
        activity_flags_ = act.flags;
        activity_tape_percent_ = act.tape_percent;
        activity_drive_mask_ = act.drive_mask;
        activity_hold_frames_ = 12u;
    } else if (activity_hold_frames_ > 0u) {
        --activity_hold_frames_;
        if (!live_warp_requested_) {
            activity_flags_ &= ~static_cast<uint32_t>(CZX_ACTIVITY_FASTFORWARD);
        }
        if (activity_hold_frames_ == 0u || activity_flags_ == 0u) {
            activity_flags_ = 0u;
            activity_tape_percent_ = -1;
            activity_drive_mask_ = 0u;
        }
    } else {
        activity_flags_ = 0u;
        activity_tape_percent_ = -1;
        activity_drive_mask_ = 0u;
    }

    hud_.show_activity_indicator = show_activity_indicator_;
    hud_.activity_flags = activity_flags_;
    hud_.activity_tape_percent = activity_tape_percent_;
    hud_.activity_drive_mask = activity_drive_mask_;
}

void EmulatorApp::sync_router_menu_open() {
    router_.set_menu_open(in_launcher_ || menu_.is_open() || browser_.is_open());
}

const czx_core_api_t* EmulatorApp::target_core_api(int target_index, int variant_index) const noexcept {
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return nullptr;
    }
    const int cidx = targets_[static_cast<size_t>(target_index)].resolve_core_index(variant_index);
    if (cidx < 0 || static_cast<size_t>(cidx) >= registry_.count()) {
        return nullptr;
    }
    return registry_.get(static_cast<size_t>(cidx));
}

int EmulatorApp::resolve_target_variant_index(int target_index) const noexcept {
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return 0;
    }
    const auto& t = targets_[static_cast<size_t>(target_index)];
    const TargetSavedState* st = find_target_saved_state(target_index);
    if (st && !st->variant_id.empty()) {
        const int saved_idx = t.find_variant_index(st->variant_id);
        if (saved_idx >= 0) {
            return saved_idx;
        }
    }
    if (t.id == "fm7") {
        const fs::path fm7_dir = fs::path(cfg_.data_dir) / "roms" / "fm7";
        const bool has_initiate = has_file_icase(fm7_dir, "INITIATE.ROM");
        const bool has_fm7_boot = has_file_icase(fm7_dir, "BOOT_BAS.ROM") ||
                                  has_file_icase(fm7_dir, "BOOT_TL11_11.ROM");
        if (!has_initiate && has_fm7_boot) {
            const int fm7_idx = t.find_variant_index("fm7");
            if (fm7_idx >= 0) {
                return fm7_idx;
            }
        }
    }
    return t.clamp_variant_index(t.default_variant_index);
}

const TargetSavedState* EmulatorApp::find_target_saved_state(int target_index) const noexcept {
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return nullptr;
    }
    const auto it = launcher_state_.targets.find(targets_[static_cast<size_t>(target_index)].id);
    return (it != launcher_state_.targets.end()) ? &it->second : nullptr;
}

TargetSavedState& EmulatorApp::target_saved_state(int target_index) {
    static TargetSavedState dummy{};
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return dummy;
    }
    return launcher_state_.targets[targets_[static_cast<size_t>(target_index)].id];
}

void EmulatorApp::apply_target_prefs(int target_index) {
    const TargetSavedState* st = find_target_saved_state(target_index);
    const bool can_native =
        !cfg_.platform.headless && !cfg_.force_lcd_sim &&
        platform_.display && platform_.display->supports_native_present();
    const bool force_native = can_native && platform_.display->is_fullscreen();

    if (st && st->has_display_prefs) {
        desktop_native_mode_ = force_native ? true : (can_native && st->desktop_native);
        desktop_scale_ = std::clamp(st->desktop_scale, 1, 3);
        scaler_cfg_.mode = st->display_mode;
        scaler_cfg_.aspect_policy = st->aspect_policy;
        scaler_cfg_.show_borders = st->show_borders;
        scaler_cfg_.margin_fill = st->margin_fill;
        router_.set_input_mode(static_cast<czx::input::InputMode>(std::clamp(st->input_mode, 0, 3)));
    } else {
        desktop_native_mode_ = force_native ? true : (can_native && default_desktop_native_);
        desktop_scale_ = std::clamp(default_desktop_scale_, 1, 3);
        scaler_cfg_.mode = cfg_.display_mode;
        scaler_cfg_.aspect_policy = cfg_.aspect_policy;
        scaler_cfg_.show_borders = cfg_.show_borders;
        scaler_cfg_.margin_fill = cfg_.margin_fill;
        router_.set_input_mode(czx::input::InputMode::Keyboard);
    }

    preset_index_ = st ? st->preset_index : cfg_.preset_index;
    if (target_index >= 0 && static_cast<size_t>(target_index) < targets_.size() &&
        targets_[static_cast<size_t>(target_index)].id == "c64") {
        c64_region_ = resolve_target_region_id(target_index);
    }
    scaler_cfg_.pan_x = 0;
    scaler_cfg_.pan_y = 0;
    hud_.fullscreen_native = is_fullscreen_display();
    hud_.desktop_native = desktop_native_mode_;
    hud_.desktop_scale = desktop_scale_;
    hud_.display_mode = scaler_cfg_.mode;
    hud_.aspect_policy = scaler_cfg_.aspect_policy;
    hud_.input_mode = static_cast<int>(router_.input_mode());
    router_.set_native_pan_enabled(!desktop_native_mode_ &&
                                   scaler_cfg_.mode == czx::video::DisplayMode::NativePan);
}

void EmulatorApp::capture_current_prefs_to_target(int target_index, bool include_active_media) {
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return;
    }
    TargetSavedState& st = target_saved_state(target_index);
    const bool had_prefs = st.has_display_prefs;
    st.has_display_prefs = true;
    st.display_mode = scaler_cfg_.mode;
    if (platform_.display && platform_.display->supports_native_present() &&
        !platform_.display->is_fullscreen()) {
        st.desktop_native = desktop_native_mode_;
    } else if (!had_prefs) {
        st.desktop_native = true;
    }
    st.desktop_scale = std::clamp(desktop_scale_, 1, 3);
    st.aspect_policy = scaler_cfg_.aspect_policy;
    st.show_borders = scaler_cfg_.show_borders;
    st.margin_fill = scaler_cfg_.margin_fill;
    st.input_mode = std::clamp(static_cast<int>(router_.input_mode()), 0, 3);
    st.preset_index = preset_index_;
    if (targets_[static_cast<size_t>(target_index)].id == "c64") {
        st.region_id = normalize_c64_region(c64_region_);
    }
    if (include_active_media) {
        const auto& t = targets_[static_cast<size_t>(target_index)];
        const czx_core_api_t* api = target_core_api(target_index, active_variant_index_);
        const uint32_t drive_count = (api && api->get_drive_count) ? api->get_drive_count(api) : 1u;
        st.variant_id = t.variant_id(active_variant_index_);
        st.media1 = active_media1_;
        st.media1_label = active_media1_label_;
        if (drive_count >= 2u) {
            st.media2 = active_media2_;
            st.media2_label = active_media2_label_;
        } else {
            st.media2.clear();
            st.media2_label.clear();
        }
        st.d88_source1 = drive_d88_[0].source_path;
        st.d88_index1 = drive_d88_[0].disk_index;
        st.d88_source2 = (drive_count >= 2u) ? drive_d88_[1].source_path : std::string();
        st.d88_index2 = (drive_count >= 2u) ? drive_d88_[1].disk_index : -1;
        if (api && api->get_desc) {
            if (const auto* desc = api->get_desc(api); desc && desc->id) {
                const auto it = last_media_dirs_.find(desc->id);
                if (it != last_media_dirs_.end() && !it->second.empty()) {
                    st.last_media_dir = it->second;
                }
            }
        }
        if (st.last_media_dir.empty()) {
            const auto it = last_media_dirs_.find(t.id);
            if (it != last_media_dirs_.end() && !it->second.empty()) {
                st.last_media_dir = it->second;
            }
        }
    }
}

void EmulatorApp::persist_launcher_state() const {
    if (cfg_.headless_frames > 0 || launcher_state_path_.empty()) {
        return;
    }
    (void)save_launcher_state(launcher_state_path_, launcher_state_);
}

std::vector<czx::video::LauncherItemView> EmulatorApp::build_launcher_items() const {
    std::vector<czx::video::LauncherItemView> items;
    items.reserve(targets_.size());
    for (size_t i = 0; i < targets_.size(); ++i) {
        const auto& t = targets_[i];
        const TargetSavedState* st = find_target_saved_state(static_cast<int>(i));
        const int vidx = resolve_target_variant_index(static_cast<int>(i));
        const czx_core_api_t* api = target_core_api(static_cast<int>(i), vidx);
        const char* exts = (api && api->get_extensions) ? api->get_extensions(api) : nullptr;
        const bool has_media = (exts != nullptr);

        std::string right;
        if (has_media) {
            if (st && !st->media1_label.empty()) {
                right = st->media1_label;
            } else if (st && !st->media1.empty()) {
                right = fs::path(st->media1).filename().string();
            } else if (st && !st->media2_label.empty()) {
                right = "D2: " + st->media2_label;
            } else if (st && !st->media2.empty()) {
                right = "D2: " + fs::path(st->media2).filename().string();
            } else {
                right = "Ready";
            }
        } else {
            if (api && api->get_preset_name) {
                const uint32_t pidx = st ? st->preset_index : cfg_.preset_index;
                const uint32_t pcnt = api->get_preset_count ? api->get_preset_count(nullptr) : 0u;
                if (pcnt > 0u) {
                    if (const char* pn = api->get_preset_name(nullptr, pidx % pcnt)) {
                        right = pn;
                    }
                }
            }
            if (right.empty()) {
                right = "Built-in";
            }
        }

        items.push_back(czx::video::LauncherItemView{
            t.title,
            t.subtitle(vidx) + "  |  " + right,
            t.icon_key,
        });
    }
    return items;
}

bool EmulatorApp::launch_target(int target_index,
                                bool use_saved_media,
                                const std::string& override_m1,
                                const std::string& override_m2,
                                const std::string& override_disp_label) {
    if (target_index < 0 || static_cast<size_t>(target_index) >= targets_.size()) {
        return false;
    }

    const auto& t = targets_[static_cast<size_t>(target_index)];
    apply_target_prefs(target_index);

    const TargetSavedState* existing_st = find_target_saved_state(target_index);
    const int vidx = resolve_target_variant_index(target_index);
    active_variant_index_ = vidx;
    pending_variant_index_ = vidx;
    const int core_idx = t.resolve_core_index(vidx);
    if (core_idx < 0) {
        return false;
    }

    std::string m1 = override_m1;
    std::string m2 = override_m2;
    std::string lbl1 = !m1.empty() ? override_disp_label : std::string();
    std::string lbl2 = (m1.empty() && !m2.empty()) ? override_disp_label : std::string();
    DriveD88State restored_d88_0{};
    DriveD88State restored_d88_1{};
    bool dropped_missing = false;

    if (use_saved_media && existing_st) {
        const std::string d88_cache_dir = (fs::path(cfg_.data_dir) / "saves" / "d88_cache").string();
        if (!existing_st->media1.empty() || (!existing_st->d88_source1.empty() && existing_st->d88_index1 >= 0)) {
            if (!resolve_saved_drive_media(existing_st->media1,
                                           existing_st->media1_label,
                                           existing_st->d88_source1,
                                           existing_st->d88_index1,
                                           d88_cache_dir,
                                           m1,
                                           lbl1,
                                           restored_d88_0.source_path,
                                           restored_d88_0.disk_index)) {
                dropped_missing = true;
            }
        }
        const czx_core_api_t* api = target_core_api(target_index, active_variant_index_);
        const uint32_t drive_count = (api && api->get_drive_count) ? api->get_drive_count(api) : 1u;
        if (drive_count >= 2u &&
            (!existing_st->media2.empty() || (!existing_st->d88_source2.empty() && existing_st->d88_index2 >= 0))) {
            if (!resolve_saved_drive_media(existing_st->media2,
                                           existing_st->media2_label,
                                           existing_st->d88_source2,
                                           existing_st->d88_index2,
                                           d88_cache_dir,
                                           m2,
                                           lbl2,
                                           restored_d88_1.source_path,
                                           restored_d88_1.disk_index)) {
                dropped_missing = true;
            }
        }
    }

    if (lbl1.empty() && !m1.empty()) {
        lbl1 = fs::path(m1).filename().string();
    }
    if (lbl2.empty() && !m2.empty()) {
        lbl2 = fs::path(m2).filename().string();
    }

    std::string disp_label = override_disp_label;
    if (disp_label.empty()) {
        disp_label = !lbl1.empty() ? lbl1 : lbl2;
    }

    in_launcher_ = false;
    menu_from_launcher_ = false;
    menu_.set_open(false);
    browser_.close();
    hud_.menu_open = false;
    hud_.force_alt_bar = false;
    sync_router_menu_open();

    const bool ok = activate_core_with_loading(core_idx, m1, m2, disp_label);
    if (ok) {
        if (!restored_d88_0.source_path.empty() && restored_d88_0.disk_index >= 0 &&
            drive_d88_[0].source_path.empty()) {
            drive_d88_[0] = restored_d88_0;
        }
        if (!restored_d88_1.source_path.empty() && restored_d88_1.disk_index >= 0 &&
            drive_d88_[1].source_path.empty()) {
            drive_d88_[1] = restored_d88_1;
        }
        if (lbl1.empty() && core_api_ && core_inst_ && core_api_->get_drive_media_label) {
            if (const char* cl0 = core_api_->get_drive_media_label(core_inst_, 0)) {
                if (cl0[0] != '\0') lbl1 = cl0;
            }
        }
        if (lbl2.empty() && core_api_ && core_inst_ && core_api_->get_drive_media_label) {
            if (const char* cl1 = core_api_->get_drive_media_label(core_inst_, 1)) {
                if (cl1[0] != '\0') lbl2 = cl1;
            }
        }
    }

    active_media1_ = m1;
    active_media2_ = m2;
    active_media1_label_ = lbl1;
    active_media2_label_ = lbl2;
    active_target_index_ = target_index;
    pending_target_index_ = target_index;
    launcher_index_ = target_index;
    launcher_.set_selected(target_index, static_cast<int>(targets_.size()), false);

    if (cfg_.headless_frames == 0) {
        launcher_state_.last_target_id = t.id;
        capture_current_prefs_to_target(target_index, ok || dropped_missing || !use_saved_media);
        persist_launcher_state();
    }

    if (!ok) {
        show_toast("ROM REQUIRED");
    } else if (!disp_label.empty()) {
        show_toast((t.title + ": " + disp_label).c_str());
    } else {
        show_toast(t.title.c_str());
    }
    sync_desktop_window_size(true);
    update_window_title();
    force_present_ = true;
    return ok;
}

void EmulatorApp::enter_launcher(int focus_target_index) {
    if (cfg_.headless_frames == 0 && active_target_index_ >= 0) {
        capture_current_prefs_to_target(active_target_index_, core_inst_ != nullptr);
    }

    destroy_active_core();
    core_api_ = nullptr;
    core_index_ = -1;
    pending_core_index_ = -1;
    active_target_index_ = -1;
    active_media1_.clear();
    active_media2_.clear();
    active_media1_label_.clear();
    active_media2_label_.clear();
    drive_d88_ = {};
    ++core_epoch_;
    reset_scaled_frame();
    clear_activity_state();
    hud_.fps_x10 = 0;
    core_error_.clear();

    in_launcher_ = true;
    menu_from_launcher_ = false;
    menu_.set_open(false);
    browser_.close();
    hud_.menu_open = false;

    const int n = static_cast<int>(targets_.size());
    if (n > 0) {
        launcher_index_ = ((focus_target_index >= 0) ? focus_target_index : 0) % n;
        launcher_.set_selected(launcher_index_, n, false);
        pending_target_index_ = launcher_index_;
        apply_target_prefs(launcher_index_);
        const auto& t = targets_[static_cast<size_t>(launcher_index_)];
        pending_variant_index_ = resolve_target_variant_index(launcher_index_);
        if (cfg_.headless_frames == 0) {
            launcher_state_.last_target_id = t.id;
            persist_launcher_state();
        }
    } else {
        launcher_index_ = 0;
        pending_target_index_ = -1;
        launcher_.set_selected(0, 0, false);
    }

    sync_router_menu_open();
    sync_desktop_window_size(true);
    update_window_title();
    force_present_ = true;
}

void EmulatorApp::handle_launcher_key(const czx_key_event_t& ev) {
    if (!in_launcher_ || targets_.empty()) {
        return;
    }
    const auto action = launcher_.handle_key(ev, static_cast<int>(targets_.size()));
    switch (action) {
        case czx::video::LauncherAction::SelectionChanged:
            launcher_index_ = launcher_.selected();
            pending_target_index_ = launcher_index_;
            launcher_state_.last_target_id = targets_[static_cast<size_t>(launcher_index_)].id;
            apply_target_prefs(launcher_index_);
            force_present_ = true;
            break;
        case czx::video::LauncherAction::BootSelected:
            (void)launch_target(launcher_index_, true);
            break;
        case czx::video::LauncherAction::OpenMenu: {
            menu_from_launcher_ = true;
            pending_target_index_ = launcher_index_;
            pending_variant_index_ = resolve_target_variant_index(launcher_index_);
            menu_.reset_cursor();
            menu_.set_open(true);
            hud_.menu_open = true;
            sync_router_menu_open();
            force_present_ = true;
            break;
        }
        case czx::video::LauncherAction::None:
            break;
    }
}

const czx_core_api_t* EmulatorApp::displayed_menu_core_api() const noexcept {
    if (!targets_.empty()) {
        if (pending_target_index_ < 0) {
            return nullptr;
        }
        return target_core_api(pending_target_index_, pending_variant_index_);
    }
    if (registry_.count() > 0 && pending_core_index_ >= 0 &&
        static_cast<size_t>(pending_core_index_) < registry_.count()) {
        return registry_.get(static_cast<size_t>(pending_core_index_));
    }
    return core_api_;
}

bool EmulatorApp::has_pending_core_change() const noexcept {
    if (!targets_.empty()) {
        if (in_launcher_ && menu_from_launcher_) {
            return false;
        }
        return (pending_target_index_ != active_target_index_) ||
               (pending_target_index_ >= 0 && pending_variant_index_ != active_variant_index_);
    }
    return registry_.count() > 0 && pending_core_index_ != core_index_;
}

std::string EmulatorApp::default_media_dir_for_core(const czx_core_api_t* api) const {
    const czx_core_api_t* target = api ? api : core_api_;
    const czx_core_desc_t* desc = (target && target->get_desc) ? target->get_desc(target) : nullptr;
    const std::string id = (desc && desc->id) ? desc->id : "";
    const fs::path media_root = fs::path(cfg_.data_dir) / "media";
    std::error_code ec;
    if (id.find("apple") != std::string::npos && fs::is_directory(media_root / "apple2", ec)) {
        return (media_root / "apple2").string();
    }
    if (id.find("vice") != std::string::npos && fs::is_directory(media_root / "c64", ec)) {
        return (media_root / "c64").string();
    }
    if (id.find("quasi88") != std::string::npos && fs::is_directory(media_root / "pc88", ec)) {
        return (media_root / "pc88").string();
    }
    if (id.find("np2kai") != std::string::npos && fs::is_directory(media_root / "pc98", ec)) {
        return (media_root / "pc98").string();
    }
    if (id.find("bluemsx") != std::string::npos && fs::is_directory(media_root / "msx", ec)) {
        return (media_root / "msx").string();
    }
    if (id == "g800" && fs::is_directory(media_root / "g800", ec)) {
        return (media_root / "g800").string();
    }
    if (id.find("emuz700") != std::string::npos && fs::is_directory(media_root / "mz700", ec)) {
        return (media_root / "mz700").string();
    }
    if (id.find("msx68k") != std::string::npos && fs::is_directory(media_root / "x68000", ec)) {
        return (media_root / "x68000").string();
    }
    if (id.rfind("efm7", 0) == 0 && fs::is_directory(media_root / "fm7", ec)) {
        return (media_root / "fm7").string();
    }
    return media_root.string();
}

void EmulatorApp::present_loading_screen(const char* core_name, const std::string& media_label) {
    if (!platform_.display || cfg_.headless_frames > 0) return;

    using namespace czx::video;
    if (!desktop_native_mode_ && scale_thread_running_ && core_inst_) {
        {
            std::lock_guard<std::mutex> lock(scale_mtx_);
            std::copy(ready_fb_.begin(), ready_fb_.end(), lcd_fb_.begin());
        }
        Rgb565Canvas bg(lcd_fb_.data(), kLcdWidth, kLcdHeight);
        bg.fill_rect_alpha(0, 0, kLcdWidth, kLcdHeight, pack_rgb565(0, 0, 0), 175);
    } else {
        std::fill(lcd_fb_.begin(), lcd_fb_.end(), pack_rgb565(14, 18, 28));
    }

    Rgb565Canvas canvas(lcd_fb_.data(), kLcdWidth, kLcdHeight);
    constexpr int kBoxX = 28;
    constexpr int kBoxY = 54;
    constexpr int kBoxW = 264;
    constexpr int kBoxH = 62;

    canvas.fill_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(18, 24, 34));
    canvas.draw_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(88, 210, 255));
    canvas.fill_rect(kBoxX + 1, kBoxY + 1, kBoxW - 2, 15, pack_rgb565(26, 44, 68));
    canvas.draw_text(kBoxX + 8, kBoxY + 5, "LOADING CORE...", pack_rgb565(255, 225, 110));

    const std::string cname = truncate_tail_px(
        (core_name && core_name[0] != '\0') ? core_name : "Core", kBoxW - 16, 1);
    canvas.draw_text(kBoxX + 8, 75, cname.c_str(), pack_rgb565(255, 255, 255));

    if (!media_label.empty()) {
        const std::string mname = truncate_tail_px(media_label, kBoxW - 16, 1);
        canvas.draw_text(kBoxX + 8, 91, mname.c_str(), pack_rgb565(150, 210, 245));
    }

    platform_.display->present(lcd_fb_.data());
}

bool EmulatorApp::activate_core_with_loading(int index,
                                             const std::string& initial_media,
                                             const std::string& initial_media2,
                                             const std::string& display_media_label) {
    if (registry_.count() == 0) return false;
    const int n = static_cast<int>(registry_.count());
    const int wrapped = ((index % n) + n) % n;
    const czx_core_api_t* next_api = registry_.get(static_cast<size_t>(wrapped));
    const auto* desc = (next_api && next_api->get_desc) ? next_api->get_desc(next_api) : nullptr;
    const char* core_name = (desc && desc->name) ? desc->name : "Core";
    std::string shown_media = display_media_label;
    if (shown_media.empty()) {
        const std::string& p = !initial_media.empty() ? initial_media : initial_media2;
        if (!p.empty()) {
            shown_media = fs::path(p).filename().string();
        }
    }
    present_loading_screen(core_name, shown_media);
    return activate_core(wrapped, initial_media, initial_media2);
}

void EmulatorApp::commit_pending_core(bool force_reload) {
    if (!force_reload && !has_pending_core_change()) {
        return;
    }
    if (!targets_.empty()) {
        if (pending_target_index_ < 0) {
            enter_launcher(active_target_index_ >= 0 ? active_target_index_ : 0);
            return;
        }
        if (pending_target_index_ == active_target_index_) {
            if (pending_variant_index_ != active_variant_index_) {
                if (active_target_index_ >= 0) {
                    capture_current_prefs_to_target(active_target_index_, true);
                }
                const auto& t = targets_[static_cast<size_t>(pending_target_index_)];
                target_saved_state(pending_target_index_).variant_id = t.variant_id(pending_variant_index_);
                persist_launcher_state();
                (void)launch_target(pending_target_index_, false, active_media1_, active_media2_, active_media1_label_);
                return;
            }
            if (force_reload) {
                (void)launch_target(active_target_index_, false, active_media1_, active_media2_, active_media1_label_);
                return;
            }
            return;
        }
        if (active_target_index_ >= 0) {
            capture_current_prefs_to_target(active_target_index_, true);
        }
        const auto& pt = targets_[static_cast<size_t>(pending_target_index_)];
        target_saved_state(pending_target_index_).variant_id = pt.variant_id(pending_variant_index_);
        (void)launch_target(pending_target_index_, true);
        return;
    }

    if (activate_core_with_loading(pending_core_index_)) {
        const auto* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
        if (desc && desc->name) {
            show_toast(desc->name);
        }
    } else {
        show_toast("ROM REQUIRED");
    }
}

void EmulatorApp::destroy_active_core() {
    if (core_api_ && core_inst_ && core_api_->destroy) {
        core_api_->destroy(core_inst_);
    }
    core_inst_ = nullptr;
    hw_render_active_ = false;
    hw_bottom_left_ = false;
    if (platform_.display) {
        platform_.display->hw_release_fbo();
    }
}

bool EmulatorApp::activate_core(int index, const std::string& initial_media, const std::string& initial_media2) {
    if (registry_.count() == 0) return false;
    const int wrapped = (index % static_cast<int>(registry_.count()) + static_cast<int>(registry_.count())) %
                        static_cast<int>(registry_.count());
    const czx_core_api_t* next_api = registry_.get(static_cast<size_t>(wrapped));
    if (!next_api) return false;

    destroy_active_core();
    reset_scaled_frame();
    clear_activity_state();
    hud_.fps_x10 = 0;
    core_api_ = next_api;
    core_index_ = wrapped;
    pending_core_index_ = wrapped;
    ++core_epoch_;
    pending_drive_ = 0;
    drive_d88_ = {};
    core_error_.clear();
    scaler_cfg_.pan_x = 0;
    scaler_cfg_.pan_y = 0;
    last_src_w_ = 0;
    last_src_h_ = 0;
    last_layout_ = {};
    last_frame_meta_ = {};
    has_last_frame_meta_ = false;
    desktop_has_last_raw_ = false;
    desktop_last_raw_bytes_.clear();
    desktop_last_raw_frame_ = {};
    native_w_ = 0;
    native_h_ = 0;
    native_par_num_ = 1;
    native_par_den_ = 1;
    native_margin_xrgb_ = 0;
    hw_render_active_ = false;
    hw_bottom_left_ = false;
    last_win_req_w_ = 0;
    last_win_req_h_ = 0;
    ++native_seq_;
    force_present_ = true;

    const czx_core_desc_t* pre_desc = next_api->get_desc ? next_api->get_desc(next_api) : nullptr;
    const std::string pre_id = (pre_desc && pre_desc->id) ? pre_desc->id : "";
    if (pre_id.rfind("vice", 0) == 0) {
        c64_region_ = resolve_target_region_id(find_target_index_by_id(targets_, "c64"));
    }

    czx_host_t host{};
    host.user_data = this;
    host.video_frame = cb_video_frame;
    host.audio_samples = cb_audio_samples;
    host.log = cb_log;
    host.initial_media = initial_media.empty() ? nullptr : initial_media.c_str();
    host.disable_tape_warp = !tape_warp_enabled_;
    host.initial_media2 = initial_media2.empty() ? nullptr : initial_media2.c_str();
    host.initial_region = (pre_id.rfind("vice", 0) == 0) ? c64_region_.c_str() : nullptr;
    if (cfg_.headless_frames == 0 && platform_.display &&
        platform_.display->supports_hw_gles2() &&
        (desktop_native_mode_ || platform_.display->is_fullscreen())) {
        host.hw_gles2_enable = cb_hw_gles2_enable;
        host.hw_make_current = cb_hw_make_current;
        host.hw_get_proc_address = cb_hw_get_proc_address;
        host.hw_ensure_fbo = cb_hw_ensure_fbo;
        host.hw_get_current_framebuffer = cb_hw_get_current_framebuffer;
    }

    core_inst_ = core_api_->create(core_api_, &host);
    if (!core_inst_) {
        const char* err = core_api_->get_last_error ? core_api_->get_last_error(core_api_) : nullptr;
        core_error_ = (err && err[0] != '\0') ? err : "Core failed to initialize";
        std::fprintf(stderr, "czx: %s\n", core_error_.c_str());
        return false;
    }

    if (core_api_->set_data_dir) {
        core_api_->set_data_dir(core_inst_, cfg_.data_dir.c_str());
    }
    sync_core_warp_option();
    sync_core_region_option();
    sync_core_gpu_scale_option();

    if (core_api_->select_preset && core_api_->get_preset_count) {
        const uint32_t count = core_api_->get_preset_count(core_inst_);
        if (count > 0) {
            preset_index_ %= count;
            core_api_->select_preset(core_inst_, preset_index_);
        }
    }

    const czx_core_desc_t* desc = core_api_->get_desc(core_api_);
    if (platform_.audio && desc && desc->audio_sample_rate > 0) {
        platform_.audio->init(desc->audio_sample_rate);
    }

    const uint32_t dcount = (core_api_ && core_api_->get_drive_count) ? core_api_->get_drive_count(core_api_) : 1u;
    if (dcount >= 2u) {
        if (!initial_media.empty() && initial_media2.empty() && czx::core::is_d88_ext(initial_media)) {
            std::vector<czx::core::D88SubDisk> disks;
            if (czx::core::inspect_d88(initial_media, disks) && disks.size() >= 2u) {
                drive_d88_[0] = {initial_media, 0};
                drive_d88_[1] = {initial_media, 1};
            }
        } else {
            if (!initial_media.empty() && czx::core::is_d88_ext(initial_media)) {
                std::vector<czx::core::D88SubDisk> disks;
                if (czx::core::inspect_d88(initial_media, disks) && disks.size() >= 2u) {
                    drive_d88_[0] = {initial_media, 0};
                }
            }
            if (!initial_media2.empty() && czx::core::is_d88_ext(initial_media2)) {
                std::vector<czx::core::D88SubDisk> disks;
                if (czx::core::inspect_d88(initial_media2, disks) && disks.size() >= 2u) {
                    drive_d88_[1] = {initial_media2, 0};
                }
            }
        }
    }
    return true;
}

void EmulatorApp::render_core_error_screen() {
    using namespace czx::video;
    std::fill(lcd_fb_.begin(), lcd_fb_.end(), pack_rgb565(18, 22, 32));
    Rgb565Canvas canvas(lcd_fb_.data(), kLcdWidth, kLcdHeight);

    constexpr int kBoxX = 8;
    constexpr int kBoxY = 8;
    constexpr int kBoxW = 304;
    constexpr int kBoxH = 154;
    canvas.fill_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(22, 28, 42));
    canvas.draw_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(245, 166, 35));
    canvas.fill_rect(kBoxX + 1, kBoxY + 1, kBoxW - 2, 16, pack_rgb565(72, 46, 16));
    canvas.draw_text(kBoxX + 7, kBoxY + 5, "ROM FILES REQUIRED", pack_rgb565(255, 210, 90));

    const auto* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
    const char* core_name = (desc && desc->name) ? desc->name : "Core";
    canvas.draw_text(kBoxX + 7, kBoxY + 21, core_name, pack_rgb565(120, 220, 255));
    canvas.draw_hline(kBoxX + 6, kBoxY + 31, kBoxW - 12, pack_rgb565(56, 72, 98));

    std::vector<std::string> lines;
    const std::string& msg = core_error_.empty() ? std::string("External ROM files not found.") : core_error_;
    constexpr size_t kMaxCols = 48;
    size_t start = 0;
    while (start <= msg.size()) {
        const size_t nl = msg.find('\n', start);
        const std::string raw = (nl == std::string::npos) ? msg.substr(start) : msg.substr(start, nl - start);
        if (raw.empty()) {
            lines.emplace_back();
        } else {
            size_t pos = 0;
            while (pos < raw.size()) {
                if (raw.size() - pos <= kMaxCols) {
                    lines.push_back(raw.substr(pos));
                    break;
                }
                size_t split = raw.find_last_of(" /", pos + kMaxCols);
                if (split == std::string::npos || split <= pos) {
                    split = pos + kMaxCols;
                } else {
                    ++split;
                }
                lines.push_back(raw.substr(pos, split - pos));
                pos = split;
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }

    int y = kBoxY + 36;
    for (const auto& line : lines) {
        if (y + 8 > kBoxY + kBoxH - 18) break;
        if (!line.empty()) {
            canvas.draw_text(kBoxX + 7, y, line.c_str(), pack_rgb565(225, 232, 240));
        }
        y += 10;
    }

    canvas.draw_hline(kBoxX + 6, kBoxY + kBoxH - 18, kBoxW - 12, pack_rgb565(56, 72, 98));
    canvas.draw_text(kBoxX + 7, kBoxY + kBoxH - 13, "Alt+8: System Menu / Switch Core", pack_rgb565(180, 200, 225));
}

void EmulatorApp::show_toast(const char* text) {
    hud_.toast_text = text ? text : "";
    hud_.toast_frames = 75;
}

bool EmulatorApp::is_fullscreen_display() const noexcept {
    return platform_.display && platform_.display->is_fullscreen();
}

void EmulatorApp::set_aspect_policy(czx::video::AspectPolicy policy, bool show_osd) {
    scaler_cfg_.aspect_policy = policy;
    hud_.aspect_policy = policy;
    sync_core_gpu_scale_option();
    if (!in_launcher_ && desktop_native_mode_) {
        sync_desktop_window_size(false);
    }
    refresh_paused_preview();
    force_present_ = true;
    if (show_osd) {
        switch (policy) {
            case czx::video::AspectPolicy::ParAware:
                show_toast("ASPECT: PAR");
                break;
            case czx::video::AspectPolicy::Crt4x3:
                show_toast("ASPECT: CRT 4:3");
                break;
            case czx::video::AspectPolicy::FullStretch:
                show_toast("ASPECT: FULL");
                break;
        }
    }
}

void EmulatorApp::cycle_aspect_policy(int delta) {
    const int cur = static_cast<int>(scaler_cfg_.aspect_policy);
    const int next = ((cur + delta) % 3 + 3) % 3;
    set_aspect_policy(static_cast<czx::video::AspectPolicy>(next), true);
}

void EmulatorApp::set_display_mode(czx::video::DisplayMode mode) {
    if (platform_.display && platform_.display->is_fullscreen()) {
        desktop_native_mode_ = true;
        hud_.desktop_native = true;
        sync_core_gpu_scale_option();
        force_present_ = true;
        show_toast("HDMI NATIVE");
        return;
    }
    desktop_native_mode_ = false;
    hud_.desktop_native = false;
    sync_desktop_window_size(false);
    force_present_ = true;
    scaler_cfg_.mode = mode;
    hud_.display_mode = mode;
    router_.set_native_pan_enabled(mode == czx::video::DisplayMode::NativePan);
    sync_core_gpu_scale_option();
    switch (mode) {
        case czx::video::DisplayMode::NativePan:
            show_toast("MODE: 1:1 PAN");
            hud_.pan_hint_frames = 90;
            break;
        case czx::video::DisplayMode::OverflowFit:
            show_toast("MODE: AXIS FIT");
            break;
        case czx::video::DisplayMode::AspectFit:
            show_toast("MODE: ASPECT FIT");
            break;
    }
}

void EmulatorApp::set_desktop_scale(int scale) {
    desktop_native_mode_ = true;
    desktop_scale_ = std::clamp(scale, 1, 3);
    hud_.desktop_native = true;
    hud_.desktop_scale = desktop_scale_;
    router_.set_native_pan_enabled(false);
    sync_core_gpu_scale_option();
    sync_desktop_window_size(true);
    force_present_ = true;
    char msg[32];
    std::snprintf(msg, sizeof(msg), "DESKTOP %dX", desktop_scale_);
    show_toast(msg);
}

void EmulatorApp::sync_desktop_window_size(bool force) {
    if (!platform_.display || !platform_.display->supports_native_present() ||
        platform_.display->is_fullscreen() || cfg_.headless_frames > 0) {
        return;
    }
    int want_w = 0;
    int want_h = 0;
    if (in_launcher_) {
        const int s = default_desktop_native_ ? std::clamp(default_desktop_scale_, 2, 3) : 3;
        want_w = czx::video::kLcdWidth * s;
        want_h = czx::video::kLcdHeight * s;
    } else if (!desktop_native_mode_) {
        want_w = czx::video::kLcdWidth * 3;
        want_h = czx::video::kLcdHeight * 3;
    } else if (!core_inst_) {
        const int s = std::max(2, desktop_scale_);
        want_w = czx::video::kLcdWidth * s;
        want_h = czx::video::kLcdHeight * s;
    } else if (native_w_ > 0 && native_h_ > 0) {
        int max_w = 0;
        int max_h = 0;
        platform_.display->get_desktop_bounds(max_w, max_h);
        const auto ws = czx::video::compute_desktop_window_size(
            native_w_, native_h_, native_par_num_, native_par_den_,
            scaler_cfg_.aspect_policy, desktop_scale_, max_w, max_h);
        want_w = ws.width;
        want_h = ws.height;
    } else {
        return;
    }

    if (want_w <= 0 || want_h <= 0) {
        return;
    }
    if (force || want_w != last_win_req_w_ || want_h != last_win_req_h_) {
        last_win_req_w_ = want_w;
        last_win_req_h_ = want_h;
        platform_.display->resize_window(want_w, want_h);
        force_present_ = true;
    }
}

void EmulatorApp::apply_pan_delta(int dx, int dy) {
    scaler_cfg_.pan_x = std::clamp(scaler_cfg_.pan_x + dx, 0, std::max(0, last_layout_.max_pan_x));
    scaler_cfg_.pan_y = std::clamp(scaler_cfg_.pan_y + dy, 0, std::max(0, last_layout_.max_pan_y));
    hud_.pan_hint_frames = 45;
}

void EmulatorApp::cycle_core(int delta) {
    if (!targets_.empty()) {
        const int n = static_cast<int>(targets_.size());
        if (in_launcher_ && menu_from_launcher_) {
            if (n <= 1) return;
            capture_current_prefs_to_target(pending_target_index_, false);
            pending_target_index_ = ((pending_target_index_ + delta) % n + n) % n;
            launcher_index_ = pending_target_index_;
            launcher_.set_selected(launcher_index_, n, false);
            apply_target_prefs(launcher_index_);
            const auto& t = targets_[static_cast<size_t>(launcher_index_)];
            pending_variant_index_ = resolve_target_variant_index(launcher_index_);
            pending_core_index_ = t.resolve_core_index(pending_variant_index_);
            if (cfg_.headless_frames == 0) {
                launcher_state_.last_target_id = t.id;
                persist_launcher_state();
            }
            force_present_ = true;
            return;
        }

        const int total = n + 1;  // -1 = Launcher, 0..n-1 = machine targets
        const int cur_pos = (pending_target_index_ < 0) ? 0 : (pending_target_index_ + 1);
        const int next_pos = ((cur_pos + delta) % total + total) % total;
        pending_target_index_ = (next_pos == 0) ? -1 : (next_pos - 1);
        if (pending_target_index_ < 0) {
            pending_variant_index_ = 0;
            pending_core_index_ = -1;
        } else if (pending_target_index_ == active_target_index_) {
            pending_variant_index_ = active_variant_index_;
            pending_core_index_ = targets_[static_cast<size_t>(pending_target_index_)]
                                      .resolve_core_index(pending_variant_index_);
        } else {
            const auto& t = targets_[static_cast<size_t>(pending_target_index_)];
            pending_variant_index_ = resolve_target_variant_index(pending_target_index_);
            pending_core_index_ = t.resolve_core_index(pending_variant_index_);
        }
        force_present_ = true;
        return;
    }

    if (registry_.count() <= 1) return;
    const int n = static_cast<int>(registry_.count());
    pending_core_index_ = ((pending_core_index_ + delta) % n + n) % n;
    force_present_ = true;
}

void EmulatorApp::cycle_preset(int delta) {
    if (in_launcher_ && menu_from_launcher_) {
        const czx_core_api_t* menu_api = displayed_menu_core_api();
        if (!menu_api || !menu_api->get_preset_count) return;
        const uint32_t count = menu_api->get_preset_count(nullptr);
        if (count == 0) return;
        preset_index_ = static_cast<uint32_t>(
            (static_cast<int>(preset_index_) + delta + static_cast<int>(count)) % static_cast<int>(count));
        if (pending_target_index_ >= 0) {
            target_saved_state(pending_target_index_).preset_index = preset_index_;
            persist_launcher_state();
        }
        force_present_ = true;
        return;
    }
    if (!core_api_ || !core_inst_ || !core_api_->get_preset_count || !core_api_->select_preset) {
        return;
    }
    const uint32_t count = core_api_->get_preset_count(core_inst_);
    if (count == 0) return;
    preset_index_ = static_cast<uint32_t>(
        (static_cast<int>(preset_index_) + delta + static_cast<int>(count)) % static_cast<int>(count));
    core_api_->select_preset(core_inst_, preset_index_);
    scaler_cfg_.pan_x = 0;
    scaler_cfg_.pan_y = 0;
    if (active_target_index_ >= 0) {
        capture_current_prefs_to_target(active_target_index_, false);
        persist_launcher_state();
    }
}

void EmulatorApp::cycle_display_mode(int delta) {
    if (platform_.display && platform_.display->is_fullscreen()) {
        desktop_native_mode_ = true;
        hud_.desktop_native = true;
        sync_core_gpu_scale_option();
        force_present_ = true;
        show_toast("HDMI NATIVE");
        return;
    }
    const bool allow_desktop = platform_.display && platform_.display->supports_native_present();
    if (allow_desktop) {
        const int cur = desktop_native_mode_ ? (2 + desktop_scale_) : static_cast<int>(scaler_cfg_.mode);
        const int next = (cur + delta + 6) % 6;
        if (next >= 3) {
            set_desktop_scale(next - 2);
            return;
        }
        set_display_mode(static_cast<czx::video::DisplayMode>(next));
        return;
    }
    const int cur = static_cast<int>(scaler_cfg_.mode);
    const int next = (cur + delta + 3) % 3;
    set_display_mode(static_cast<czx::video::DisplayMode>(next));
}

czx::video::MenuViewModel EmulatorApp::build_menu_vm() const {
    czx::video::MenuViewModel vm{};
    vm.from_launcher = (in_launcher_ && menu_from_launcher_);

    if (!targets_.empty() && pending_target_index_ < 0 && !vm.from_launcher) {
        vm.core_name = "Launcher";
        vm.core_is_launcher = true;
        vm.core_pending = true;
        vm.has_variant_option = false;
        vm.variant_option_label.clear();
        vm.variant_value_label.clear();
        vm.has_media_browser = false;
        vm.can_eject = false;
        vm.preset_or_media_label = "(Launcher)";
        vm.drive_count = 1u;
        vm.src_width = last_src_w_;
        vm.src_height = last_src_h_;
        vm.fullscreen_native = is_fullscreen_display();
        vm.desktop_native_active = desktop_native_mode_;
        vm.desktop_scale = desktop_scale_;
        vm.display_mode = scaler_cfg_.mode;
        vm.has_vsync_option = platform_.display && platform_.display->supports_vsync_toggle();
        vm.vsync_enabled = vsync_enabled_;
        vm.aspect_policy = scaler_cfg_.aspect_policy;
        vm.show_borders = scaler_cfg_.show_borders;
        vm.margin_fill = scaler_cfg_.margin_fill;
        vm.show_activity_indicator = show_activity_indicator_;
        vm.tape_warp_enabled = tape_warp_enabled_;
        vm.input_mode = static_cast<int>(router_.input_mode());
        return vm;
    }

    const czx_core_api_t* menu_api = displayed_menu_core_api();
    const bool core_pending = has_pending_core_change();
    if (!targets_.empty() && pending_target_index_ >= 0 &&
        static_cast<size_t>(pending_target_index_) < targets_.size()) {
        const auto& t = targets_[static_cast<size_t>(pending_target_index_)];
        vm.core_name = t.menu_core_name(pending_variant_index_);
        vm.has_variant_option = t.has_variants();
        if (vm.has_variant_option) {
            vm.variant_option_label = t.variant_option_label;
            vm.variant_value_label = t.variant_label(pending_variant_index_);
        }
    } else {
        const auto* desc = (menu_api && menu_api->get_desc) ? menu_api->get_desc(menu_api) : nullptr;
        vm.core_name = (desc && desc->name) ? desc->name : "Core";
    }
    vm.core_pending = core_pending;

    bool is_c64 = false;
    if (!targets_.empty() && pending_target_index_ >= 0 &&
        static_cast<size_t>(pending_target_index_) < targets_.size()) {
        is_c64 = (targets_[static_cast<size_t>(pending_target_index_)].id == "c64");
    } else if (menu_api && menu_api->get_desc) {
        const auto* desc = menu_api->get_desc(menu_api);
        if (desc && desc->id) {
            is_c64 = (std::string(desc->id).rfind("vice", 0) == 0);
        }
    }
    if (is_c64) {
        vm.has_region_option = true;
        const std::string reg =
            (vm.from_launcher || core_pending)
                ? resolve_target_region_id(pending_target_index_)
                : normalize_c64_region(c64_region_);
        vm.region_value_label = region_display_label(reg);
    }

    const char* exts = (menu_api && menu_api->get_extensions) ? menu_api->get_extensions(menu_api) : nullptr;
    vm.has_media_browser = (exts != nullptr);
    vm.drive_count = (menu_api && menu_api->get_drive_count) ? menu_api->get_drive_count(menu_api) : 1u;

    if (vm.from_launcher) {
        const TargetSavedState* st = find_target_saved_state(pending_target_index_);
        if (vm.has_media_browser) {
            if (st && !st->media1_label.empty()) {
                vm.preset_or_media_label = st->media1_label;
            } else if (st && !st->media1.empty()) {
                vm.preset_or_media_label = fs::path(st->media1).filename().string();
            } else {
                vm.preset_or_media_label = "None";
            }
            vm.can_eject = (st && (!st->media1.empty() || !st->media2.empty()));
            vm.is_tape_media = false;
        } else if (menu_api && menu_api->get_preset_name) {
            const uint32_t pcnt = menu_api->get_preset_count ? menu_api->get_preset_count(nullptr) : 0u;
            const uint32_t idx = (pcnt > 0u) ? (preset_index_ % pcnt) : 0u;
            const char* pname = (pcnt > 0u) ? menu_api->get_preset_name(nullptr, idx) : nullptr;
            vm.preset_or_media_label = pname ? pname : "Default";
            vm.can_eject = false;
        } else {
            vm.preset_or_media_label = "Default";
            vm.can_eject = false;
        }
        if (vm.drive_count >= 2u && vm.has_media_browser) {
            if (menu_api->get_drive_name) {
                if (const char* n0 = menu_api->get_drive_name(menu_api, 0)) vm.drive1_name = n0;
                if (const char* n1 = menu_api->get_drive_name(menu_api, 1)) vm.drive2_name = n1;
            }
            if (st && !st->media1_label.empty()) {
                vm.drive1_label = st->media1_label;
            } else if (st && !st->media1.empty()) {
                vm.drive1_label = fs::path(st->media1).filename().string();
            } else {
                vm.drive1_label = "Empty";
            }
            if (st && !st->media2_label.empty()) {
                vm.drive2_label = st->media2_label;
            } else if (st && !st->media2.empty()) {
                vm.drive2_label = fs::path(st->media2).filename().string();
            } else {
                vm.drive2_label = "Empty";
            }
        }
        vm.src_width = 0;
        vm.src_height = 0;
    } else {
        const bool live_core = (!core_pending && core_inst_ != nullptr);
        vm.can_eject = (live_core && core_api_ && core_api_->eject_media != nullptr);
        if (vm.has_media_browser) {
            const char* lbl = (live_core && core_api_ && core_api_->get_media_label)
                                  ? core_api_->get_media_label(core_inst_)
                                  : nullptr;
            vm.preset_or_media_label = lbl ? lbl : "None";
            vm.is_tape_media = live_core && is_tape_media_label(vm.preset_or_media_label);
        } else if (live_core && core_api_ && core_api_->get_preset_name) {
            vm.preset_or_media_label = core_api_->get_preset_name(core_inst_, preset_index_);
        } else {
            vm.preset_or_media_label = core_pending ? "(Pending)" : "Default";
        }
        if (vm.drive_count >= 2u && vm.has_media_browser) {
            if (menu_api->get_drive_name) {
                if (const char* n0 = menu_api->get_drive_name(menu_api, 0)) vm.drive1_name = n0;
                if (const char* n1 = menu_api->get_drive_name(menu_api, 1)) vm.drive2_name = n1;
            }
            const char* l0 = (live_core && core_api_ && core_api_->get_drive_media_label)
                                 ? core_api_->get_drive_media_label(core_inst_, 0)
                                 : nullptr;
            const char* l1 = (live_core && core_api_ && core_api_->get_drive_media_label)
                                 ? core_api_->get_drive_media_label(core_inst_, 1)
                                 : nullptr;
            vm.drive1_label = (l0 && l0[0] != '\0') ? l0 : "Empty";
            vm.drive2_label = (l1 && l1[0] != '\0') ? l1 : "Empty";
        }
        vm.src_width = last_src_w_;
        vm.src_height = last_src_h_;
    }

    vm.fullscreen_native = is_fullscreen_display();
    vm.desktop_native_active = desktop_native_mode_;
    vm.desktop_scale = desktop_scale_;
    vm.display_mode = scaler_cfg_.mode;
    vm.has_vsync_option = platform_.display && platform_.display->supports_vsync_toggle();
    vm.vsync_enabled = vsync_enabled_;
    vm.aspect_policy = scaler_cfg_.aspect_policy;
    vm.show_borders = scaler_cfg_.show_borders;
    vm.margin_fill = scaler_cfg_.margin_fill;
    vm.show_activity_indicator = show_activity_indicator_;
    vm.tape_warp_enabled = tape_warp_enabled_;
    vm.input_mode = static_cast<int>(router_.input_mode());
    return vm;
}

void EmulatorApp::open_browser_for_drive(uint32_t drive) {
    pending_drive_ = drive;
    const czx_core_api_t* menu_api = displayed_menu_core_api();
    if (!menu_api) return;
    const auto* desc = (menu_api->get_desc) ? menu_api->get_desc(menu_api) : nullptr;
    const std::string core_id = (desc && desc->id) ? desc->id : "";
    std::string start_dir = default_media_dir_for_core(menu_api);
    std::error_code ec;
    const TargetSavedState* st = find_target_saved_state(pending_target_index_);
    if (st && !st->last_media_dir.empty() && fs::is_directory(st->last_media_dir, ec)) {
        start_dir = st->last_media_dir;
    } else if (!core_id.empty()) {
        const auto it = last_media_dirs_.find(core_id);
        if (it != last_media_dirs_.end() && fs::is_directory(it->second, ec)) {
            start_dir = it->second;
        }
    }
    const char* exts = (menu_api->get_extensions) ? menu_api->get_extensions(menu_api) : nullptr;
    const uint32_t drive_count =
        (menu_api->get_drive_count) ? menu_api->get_drive_count(menu_api) : 1u;
    std::string title = "MEDIA";
    if (drive_count >= 2u && menu_api->get_drive_name) {
        if (const char* dname = menu_api->get_drive_name(menu_api, drive)) {
            title = dname;
        }
    }
    const std::string d88_cache_dir = (fs::path(cfg_.data_dir) / "saves" / "d88_cache").string();
    const bool allow_auto_dual = (drive_count >= 2u && drive == 0u);
    czx::video::D88PickerHint d88_hint{};
    if (in_launcher_ && menu_from_launcher_ && st && drive < 2u) {
        const std::string& src_self = (drive == 0u) ? st->d88_source1 : st->d88_source2;
        const std::string& src_other = (drive == 0u) ? st->d88_source2 : st->d88_source1;
        const std::string& source = !src_self.empty() ? src_self : ((drive_count >= 2u) ? src_other : std::string());
        if (!source.empty()) {
            d88_hint.source_path = source;
            d88_hint.drive1_disk_index = (st->d88_source1 == source) ? st->d88_index1 : -1;
            d88_hint.drive2_disk_index = (st->d88_source2 == source) ? st->d88_index2 : -1;
            d88_hint.focus_disk_index = (drive == 0u) ? d88_hint.drive1_disk_index : d88_hint.drive2_disk_index;
        }
    } else if (!has_pending_core_change() && core_inst_ != nullptr && drive < 2u) {
        std::string source = !drive_d88_[drive].source_path.empty()
                                 ? drive_d88_[drive].source_path
                                 : ((drive_count >= 2u) ? drive_d88_[drive ^ 1u].source_path : std::string());
        if (!source.empty()) {
            d88_hint.source_path = source;
            d88_hint.drive1_disk_index = (drive_d88_[0].source_path == source) ? drive_d88_[0].disk_index : -1;
            d88_hint.drive2_disk_index = (drive_d88_[1].source_path == source) ? drive_d88_[1].disk_index : -1;
            d88_hint.focus_disk_index = (drive == 0u) ? d88_hint.drive1_disk_index : d88_hint.drive2_disk_index;
        }
    }
    browser_.open(start_dir, exts ? exts : "", title, d88_cache_dir, allow_auto_dual, d88_hint);
    sync_router_menu_open();
}

void EmulatorApp::eject_drive_slot(uint32_t drive) {
    if (in_launcher_ && menu_from_launcher_) {
        if (pending_target_index_ < 0 || drive >= 2u) return;
        TargetSavedState& st = target_saved_state(pending_target_index_);
        if (drive == 0u) {
            st.media1.clear();
            st.media1_label.clear();
            st.d88_source1.clear();
            st.d88_index1 = -1;
        } else {
            st.media2.clear();
            st.media2_label.clear();
            st.d88_source2.clear();
            st.d88_index2 = -1;
        }
        persist_launcher_state();
        show_toast(drive == 0u ? "CLEARED D1" : "CLEARED D2");
        force_present_ = true;
        return;
    }
    if (has_pending_core_change() || !core_api_ || !core_inst_) return;
    if (core_api_->eject_drive && core_api_->eject_drive(core_inst_, drive)) {
        if (drive < 2u) {
            drive_d88_[drive] = {};
        }
        if (drive == 0u) {
            active_media1_.clear();
            active_media1_label_.clear();
        } else if (drive == 1u) {
            active_media2_.clear();
            active_media2_label_.clear();
        }
        if (active_target_index_ >= 0) {
            capture_current_prefs_to_target(active_target_index_, true);
            persist_launcher_state();
        }
        const char* dname =
            (core_api_->get_drive_name) ? core_api_->get_drive_name(core_api_, drive) : nullptr;
        show_toast((std::string(dname ? dname : "Drive") + ": EJECTED").c_str());
    } else {
        show_toast("EJECT FAILED");
    }
}

void EmulatorApp::handle_host_command(czx::input::HostCommand cmd) {
    auto save_display_or_input_pref = [&]() {
        if (in_launcher_) {
            const int tidx = menu_.is_open() ? pending_target_index_ : launcher_index_;
            capture_current_prefs_to_target(tidx, false);
            persist_launcher_state();
        } else if (active_target_index_ >= 0) {
            capture_current_prefs_to_target(active_target_index_, true);
            persist_launcher_state();
        }
    };

    switch (cmd) {
        case czx::input::HostCommand::SetModeNativePan:
            if (is_fullscreen_display()) {
                set_aspect_policy(czx::video::AspectPolicy::ParAware, true);
            } else if (desktop_native_mode_) {
                set_desktop_scale(1);
            } else {
                set_display_mode(czx::video::DisplayMode::NativePan);
            }
            save_display_or_input_pref();
            break;
        case czx::input::HostCommand::SetModeOverflowFit:
            if (is_fullscreen_display()) {
                set_aspect_policy(czx::video::AspectPolicy::Crt4x3, true);
            } else if (desktop_native_mode_) {
                set_desktop_scale(2);
            } else {
                set_display_mode(czx::video::DisplayMode::OverflowFit);
            }
            save_display_or_input_pref();
            break;
        case czx::input::HostCommand::SetModeAspectFit:
            if (is_fullscreen_display()) {
                set_aspect_policy(czx::video::AspectPolicy::FullStretch, true);
            } else if (desktop_native_mode_) {
                set_desktop_scale(3);
            } else {
                set_display_mode(czx::video::DisplayMode::AspectFit);
            }
            save_display_or_input_pref();
            break;
        case czx::input::HostCommand::ToggleInputMode: {
            const int next_i = (static_cast<int>(router_.input_mode()) + 1) % 4;
            const auto next = static_cast<czx::input::InputMode>(next_i);
            router_.set_input_mode(next);
            hud_.input_mode = next_i;
            show_toast(next_i == 3 ? "INPUT: MOUSE"
                                   : (next_i == 2 ? "INPUT: JOYSTICK P2"
                                                  : (next_i == 1 ? "INPUT: JOYSTICK P1"
                                                                 : "INPUT: KEYBOARD")));
            save_display_or_input_pref();
            break;
        }
        case czx::input::HostCommand::ToggleMenu: {
            if (in_launcher_) {
                const bool was_open = menu_.is_open() || browser_.is_open();
                if (was_open) {
                    browser_.close();
                    menu_.set_open(false);
                    menu_from_launcher_ = false;
                    capture_current_prefs_to_target(launcher_index_, false);
                    persist_launcher_state();
                } else {
                    menu_from_launcher_ = true;
                    pending_target_index_ = launcher_index_;
                    pending_variant_index_ =
                        (launcher_index_ >= 0 && static_cast<size_t>(launcher_index_) < targets_.size())
                            ? resolve_target_variant_index(launcher_index_)
                            : 0;
                    menu_.reset_cursor();
                    menu_.set_open(true);
                }
                sync_router_menu_open();
                hud_.menu_open = menu_.is_open();
                force_present_ = true;
                break;
            }

            const bool was_open = menu_.is_open() || browser_.is_open();
            if (browser_.is_open()) {
                browser_.close();
                menu_.set_open(false);
            } else {
                menu_.toggle();
            }
            menu_from_launcher_ = false;
            sync_router_menu_open();
            hud_.menu_open = menu_.is_open();
            if (!was_open && menu_.is_open()) {
                pending_core_index_ = core_index_;
                pending_target_index_ = active_target_index_;
                pending_variant_index_ = active_variant_index_;
                force_present_ = true;
            } else if (was_open && !menu_.is_open()) {
                commit_pending_core();
                force_present_ = true;
            }
            break;
        }
        case czx::input::HostCommand::VolumeDown:
        case czx::input::HostCommand::VolumeUp:
        case czx::input::HostCommand::VolumeMute: {
            czx::platform::SystemVolumeState st{};
            bool ok = false;
            if (platform_.audio) {
                if (cmd == czx::input::HostCommand::VolumeMute) {
                    ok = platform_.audio->toggle_system_mute(st);
                } else {
                    const int delta = (cmd == czx::input::HostCommand::VolumeDown) ? -5 : 5;
                    ok = platform_.audio->adjust_system_volume(delta, st);
                }
            }
            if (ok && st.percent >= 0) {
                hud_.volume_percent = st.percent;
                hud_.volume_muted = st.muted;
                hud_.volume_osd_frames = 75;
            } else {
                show_toast("VOL: N/A");
            }
            force_present_ = true;
            break;
        }
        case czx::input::HostCommand::None:
            break;
    }
}

void EmulatorApp::handle_menu_key(const czx_key_event_t& ev) {
    if (in_launcher_ && !browser_.is_open() && !menu_.is_open()) {
        handle_launcher_key(ev);
        return;
    }

    if (browser_.is_open()) {
        const auto ba = browser_.handle_key(ev);
        if (ba == czx::video::FileBrowserAction::Cancelled) {
            sync_router_menu_open();
            force_present_ = true;
            return;
        }
        if (ba == czx::video::FileBrowserAction::Selected) {
            const czx_core_api_t* menu_api = displayed_menu_core_api();
            const auto* desc = (menu_api && menu_api->get_desc) ? menu_api->get_desc(menu_api) : nullptr;
            if (desc && desc->id && !browser_.current_dir().empty()) {
                last_media_dirs_[desc->id] = browser_.current_dir();
            }
            const std::string& path = browser_.selected_path();
            const std::string& label = browser_.selected_label();
            const std::string& path2 = browser_.selected_drive2_path();
            const std::string& label2 = browser_.selected_drive2_label();
            const std::string d88_src = browser_.selected_d88_source_path();
            const int d88_idx1 = browser_.selected_d88_disk_index();
            const int d88_idx2 = browser_.selected_drive2_d88_disk_index();
            const std::string d1_disp = label.empty() ? fs::path(path).filename().string() : label;
            const std::string d2_disp = label2.empty() ? fs::path(path2).filename().string() : label2;

            if (in_launcher_ && menu_from_launcher_) {
                const uint32_t drive_count =
                    (menu_api && menu_api->get_drive_count) ? menu_api->get_drive_count(menu_api) : 1u;
                TargetSavedState& st = target_saved_state(pending_target_index_);
                if (!browser_.current_dir().empty()) {
                    st.last_media_dir = browser_.current_dir();
                }
                if (!path2.empty()) {
                    st.media1 = path;
                    st.media1_label = d1_disp;
                    st.media2 = path2;
                    st.media2_label = d2_disp;
                    st.d88_source1 = d88_src;
                    st.d88_index1 = d88_idx1;
                    st.d88_source2 = d88_src;
                    st.d88_index2 = d88_idx2;
                    show_toast(("D1+D2 SET: " + d1_disp).c_str());
                } else if (drive_count >= 2u && pending_drive_ == 1u) {
                    st.media2 = path;
                    st.media2_label = d1_disp;
                    st.d88_source2 = d88_src;
                    st.d88_index2 = d88_idx1;
                    show_toast(("D2 SET: " + d1_disp).c_str());
                } else {
                    st.media1 = path;
                    st.media1_label = d1_disp;
                    st.d88_source1 = d88_src;
                    st.d88_index1 = d88_idx1;
                    if (drive_count < 2u || is_combo_media_ext(path)) {
                        st.media2.clear();
                        st.media2_label.clear();
                        st.d88_source2.clear();
                        st.d88_index2 = -1;
                    }
                    show_toast(("SET: " + d1_disp).c_str());
                }
                capture_current_prefs_to_target(pending_target_index_, false);
                persist_launcher_state();
                menu_.set_open(true);
                hud_.menu_open = true;
                sync_router_menu_open();
                force_present_ = true;
                return;
            }

            menu_.set_open(false);
            sync_router_menu_open();
            hud_.menu_open = false;
            auto record_drive_d88 = [&](bool ok) {
                if (!ok) return;
                if (!path2.empty()) {
                    drive_d88_[0] = {d88_src, d88_idx1};
                    drive_d88_[1] = {d88_src, d88_idx2};
                } else if (pending_drive_ < 2u) {
                    if (is_combo_media_ext(path)) {
                        drive_d88_ = {};
                    } else {
                        drive_d88_[pending_drive_] = {d88_src, d88_idx1};
                    }
                }
            };
            if (has_pending_core_change() || !core_inst_) {
                const uint32_t drive_count =
                    (menu_api && menu_api->get_drive_count) ? menu_api->get_drive_count(menu_api) : 1u;
                std::string m1;
                std::string m2;
                if (!path2.empty()) {
                    m1 = path;
                    m2 = path2;
                } else if (drive_count >= 2u && pending_drive_ == 1u) {
                    m2 = path;
                } else {
                    m1 = path;
                }
                if (active_target_index_ >= 0 && pending_target_index_ != active_target_index_) {
                    capture_current_prefs_to_target(active_target_index_, true);
                }
                bool ok = false;
                if (!targets_.empty() && pending_target_index_ >= 0) {
                    const auto& t = targets_[static_cast<size_t>(pending_target_index_)];
                    target_saved_state(pending_target_index_).variant_id = t.variant_id(pending_variant_index_);
                    ok = launch_target(pending_target_index_, false, m1, m2, d1_disp);
                } else {
                    ok = activate_core_with_loading(pending_core_index_, m1, m2, d1_disp);
                }
                record_drive_d88(ok);
                if (ok) {
                    if (!path2.empty()) {
                        active_media1_ = path;
                        active_media1_label_ = d1_disp;
                        active_media2_ = path2;
                        active_media2_label_ = d2_disp;
                        show_toast(("LOADED D1+D2: " + d1_disp).c_str());
                    } else if (drive_count >= 2u && menu_api && menu_api->get_drive_name) {
                        if (pending_drive_ == 1u) {
                            active_media2_ = path;
                            active_media2_label_ = d1_disp;
                        } else {
                            active_media1_ = path;
                            active_media1_label_ = d1_disp;
                            if (is_combo_media_ext(path)) {
                                active_media2_.clear();
                                active_media2_label_.clear();
                            }
                        }
                        const char* dname = menu_api->get_drive_name(menu_api, pending_drive_);
                        show_toast((std::string(dname ? dname : "Drive") + ": " + d1_disp).c_str());
                    } else {
                        active_media1_ = path;
                        active_media1_label_ = d1_disp;
                        active_media2_.clear();
                        active_media2_label_.clear();
                        show_toast(("LOADED: " + d1_disp).c_str());
                    }
                    if (active_target_index_ >= 0) {
                        capture_current_prefs_to_target(active_target_index_, true);
                        persist_launcher_state();
                    }
                } else if (!core_inst_) {
                    show_toast("ROM REQUIRED");
                } else {
                    show_toast("LOAD FAILED");
                }
                return;
            }

            bool ok = false;
            const uint32_t drive_count =
                (core_api_ && core_api_->get_drive_count) ? core_api_->get_drive_count(core_api_) : 1u;
            if (!path2.empty() && core_api_ && core_inst_ && core_api_->load_dual_media) {
                ok = core_api_->load_dual_media(core_inst_, path.c_str(), d1_disp.c_str(),
                                                path2.c_str(), d2_disp.c_str());
                if (ok) {
                    active_media1_ = path;
                    active_media1_label_ = d1_disp;
                    active_media2_ = path2;
                    active_media2_label_ = d2_disp;
                    show_toast(("LOADED D1+D2: " + d1_disp).c_str());
                }
            } else if (core_api_ && core_inst_ && core_api_->load_media_in_drive) {
                ok = core_api_->load_media_in_drive(core_inst_, pending_drive_, path.c_str(),
                                                    d1_disp.c_str());
                if (ok) {
                    if (drive_count >= 2u && pending_drive_ == 1u) {
                        active_media2_ = path;
                        active_media2_label_ = d1_disp;
                    } else {
                        active_media1_ = path;
                        active_media1_label_ = d1_disp;
                        if (drive_count < 2u || is_combo_media_ext(path)) {
                            active_media2_.clear();
                            active_media2_label_.clear();
                        }
                    }
                    if (drive_count >= 2u && core_api_->get_drive_name) {
                        const char* dname = core_api_->get_drive_name(core_api_, pending_drive_);
                        show_toast((std::string(dname ? dname : "Drive") + ": " + d1_disp).c_str());
                    } else {
                        show_toast(("LOADED: " + d1_disp).c_str());
                    }
                }
            } else if (core_api_ && core_inst_ && core_api_->load_media) {
                ok = core_api_->load_media(core_inst_, path.c_str());
                if (ok) {
                    active_media1_ = path;
                    active_media1_label_ = d1_disp;
                    active_media2_.clear();
                    active_media2_label_.clear();
                    show_toast(("LOADED: " + d1_disp).c_str());
                }
            }
            record_drive_d88(ok);
            if (ok && active_target_index_ >= 0) {
                capture_current_prefs_to_target(active_target_index_, true);
                persist_launcher_state();
            }
            if (!ok) {
                show_toast("LOAD FAILED");
            }
        }
        return;
    }

    const auto vm = build_menu_vm();
    const auto action = menu_.handle_key(ev, vm);
    hud_.menu_open = menu_.is_open();
    if (!menu_.is_open()) {
        sync_router_menu_open();
    }

    auto save_target_prefs_now = [&]() {
        if (in_launcher_ && menu_from_launcher_) {
            capture_current_prefs_to_target(pending_target_index_, false);
            persist_launcher_state();
        } else if (!in_launcher_ && active_target_index_ >= 0) {
            capture_current_prefs_to_target(active_target_index_, true);
            persist_launcher_state();
        }
    };

    switch (action) {
        case czx::video::MenuAction::CycleCorePrev:
            cycle_core(-1);
            break;
        case czx::video::MenuAction::CycleCoreNext:
            cycle_core(1);
            break;
        case czx::video::MenuAction::ApplyCore:
            if (in_launcher_ && menu_from_launcher_) {
                capture_current_prefs_to_target(pending_target_index_, false);
                menu_from_launcher_ = false;
                (void)launch_target(pending_target_index_, true);
            } else {
                commit_pending_core();
            }
            break;
        case czx::video::MenuAction::CycleVariantPrev:
        case czx::video::MenuAction::CycleVariantNext:
            if (pending_target_index_ >= 0 &&
                static_cast<size_t>(pending_target_index_) < targets_.size()) {
                const auto& t = targets_[static_cast<size_t>(pending_target_index_)];
                const int nvar = t.variant_count();
                if (nvar > 1) {
                    const int delta = (action == czx::video::MenuAction::CycleVariantPrev) ? -1 : 1;
                    pending_variant_index_ = ((pending_variant_index_ + delta) % nvar + nvar) % nvar;
                    target_saved_state(pending_target_index_).variant_id = t.variant_id(pending_variant_index_);
                    pending_core_index_ = t.resolve_core_index(pending_variant_index_);
                    persist_launcher_state();
                    auto to_upper_str = [](std::string s) {
                        for (char& ch : s) {
                            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
                        }
                        return s;
                    };
                    const std::string opt = to_upper_str(
                        !t.variant_option_label.empty() ? t.variant_option_label : std::string("VARIANT"));
                    const std::string val = to_upper_str(t.variant_label(pending_variant_index_));
                    show_toast((opt + ": " + val).c_str());
                    force_present_ = true;
                }
            }
            break;
        case czx::video::MenuAction::CycleRegionPrev:
        case czx::video::MenuAction::CycleRegionNext: {
            static constexpr const char* kRegions[3] = {"auto", "pal", "ntsc"};
            const bool is_pending = has_pending_core_change();
            const std::string cur_reg =
                ((in_launcher_ && menu_from_launcher_) || is_pending)
                    ? resolve_target_region_id(pending_target_index_)
                    : normalize_c64_region(c64_region_);
            int cur_idx = 0;
            for (int i = 0; i < 3; ++i) {
                if (cur_reg == kRegions[i]) {
                    cur_idx = i;
                    break;
                }
            }
            const int delta = (action == czx::video::MenuAction::CycleRegionPrev) ? -1 : 1;
            const std::string next_reg = kRegions[((cur_idx + delta) % 3 + 3) % 3];
            if (pending_target_index_ >= 0 &&
                static_cast<size_t>(pending_target_index_) < targets_.size()) {
                target_saved_state(pending_target_index_).region_id = next_reg;
                persist_launcher_state();
            }
            if ((in_launcher_ && menu_from_launcher_) || !is_pending) {
                c64_region_ = next_reg;
            }
            if (!in_launcher_ && !is_pending) {
                sync_core_region_option();
            }
            std::string lbl = region_display_label(next_reg);
            for (char& ch : lbl) {
                if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            }
            show_toast(("REGION: " + lbl).c_str());
            force_present_ = true;
            break;
        }
        case czx::video::MenuAction::OpenMediaBrowser:
        case czx::video::MenuAction::OpenDrive1Browser:
            open_browser_for_drive(0);
            break;
        case czx::video::MenuAction::OpenDrive2Browser:
            open_browser_for_drive(1);
            break;
        case czx::video::MenuAction::EjectDrive1:
            eject_drive_slot(0);
            break;
        case czx::video::MenuAction::EjectDrive2:
            eject_drive_slot(1);
            break;
        case czx::video::MenuAction::CyclePresetPrev:
            if ((in_launcher_ && menu_from_launcher_) || !has_pending_core_change()) {
                cycle_preset(-1);
            }
            break;
        case czx::video::MenuAction::CyclePresetNext:
            if ((in_launcher_ && menu_from_launcher_) || !has_pending_core_change()) {
                cycle_preset(1);
            }
            break;
        case czx::video::MenuAction::EjectMedia:
            if (in_launcher_ && menu_from_launcher_) {
                if (pending_target_index_ >= 0) {
                    TargetSavedState& st = target_saved_state(pending_target_index_);
                    st.media1.clear();
                    st.media1_label.clear();
                    st.media2.clear();
                    st.media2_label.clear();
                    st.d88_source1.clear();
                    st.d88_index1 = -1;
                    st.d88_source2.clear();
                    st.d88_index2 = -1;
                    persist_launcher_state();
                    show_toast("CLEARED");
                    force_present_ = true;
                }
            } else if (!has_pending_core_change() && core_api_ && core_inst_ && core_api_->eject_media &&
                       core_api_->eject_media(core_inst_)) {
                if (vm.is_tape_media) {
                    menu_.set_open(false);
                    sync_router_menu_open();
                    hud_.menu_open = false;
                    show_toast("TAPE PLAY");
                } else {
                    active_media1_.clear();
                    active_media1_label_.clear();
                    active_media2_.clear();
                    active_media2_label_.clear();
                    drive_d88_ = {};
                    if (active_target_index_ >= 0) {
                        capture_current_prefs_to_target(active_target_index_, true);
                        persist_launcher_state();
                    }
                    const char* lbl = (core_api_->get_media_label)
                                          ? core_api_->get_media_label(core_inst_)
                                          : "EJECTED";
                    show_toast(lbl);
                }
            }
            break;
        case czx::video::MenuAction::CycleDisplayModePrev:
            cycle_display_mode(-1);
            refresh_paused_preview();
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::CycleDisplayModeNext:
            cycle_display_mode(1);
            refresh_paused_preview();
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::ToggleVsync:
            if (platform_.display && platform_.display->supports_vsync_toggle()) {
                const bool next = !vsync_enabled_;
                if (platform_.display->set_vsync_enabled(next)) {
                    vsync_enabled_ = next;
                    save_app_config();
                    show_toast(vsync_enabled_ ? "VSYNC: ON" : "VSYNC: OFF");
                    force_present_ = true;
                } else {
                    show_toast("VSYNC: FAILED");
                }
            }
            break;
        case czx::video::MenuAction::CycleAspectPolicyPrev:
            cycle_aspect_policy(-1);
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::CycleAspectPolicyNext:
            cycle_aspect_policy(1);
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::ToggleBorders:
            scaler_cfg_.show_borders = !scaler_cfg_.show_borders;
            scaler_cfg_.pan_x = 0;
            scaler_cfg_.pan_y = 0;
            refresh_paused_preview();
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::ToggleMarginFill:
            scaler_cfg_.margin_fill =
                (scaler_cfg_.margin_fill == czx::video::MarginFill::Auto)
                    ? czx::video::MarginFill::Black
                    : czx::video::MarginFill::Auto;
            refresh_paused_preview();
            show_toast(scaler_cfg_.margin_fill == czx::video::MarginFill::Auto ? "MARGIN: AUTO"
                                                                               : "MARGIN: BLACK");
            save_target_prefs_now();
            break;
        case czx::video::MenuAction::ToggleActivityIndicator:
            show_activity_indicator_ = !show_activity_indicator_;
            hud_.show_activity_indicator = show_activity_indicator_;
            save_app_config();
            force_present_ = true;
            show_toast(show_activity_indicator_ ? "ACCESS LED: ON" : "ACCESS LED: OFF");
            break;
        case czx::video::MenuAction::ToggleTapeWarp:
            tape_warp_enabled_ = !tape_warp_enabled_;
            save_app_config();
            sync_core_warp_option();
            if (!tape_warp_enabled_ && core_inst_ && core_api_ && core_api_->set_fast_forward) {
                core_api_->set_fast_forward(core_inst_, false, false);
            }
            force_present_ = true;
            show_toast(tape_warp_enabled_ ? "TAPE WARP: ON" : "TAPE WARP: OFF");
            break;
        case czx::video::MenuAction::ToggleInputMode:
            handle_host_command(czx::input::HostCommand::ToggleInputMode);
            break;
        case czx::video::MenuAction::ResetCore:
            if (in_launcher_ && menu_from_launcher_) {
                capture_current_prefs_to_target(pending_target_index_, false);
                menu_from_launcher_ = false;
                (void)launch_target(pending_target_index_, true);
            } else if (has_pending_core_change() || !core_inst_) {
                commit_pending_core(true);
            } else if (core_api_ && core_inst_ && core_api_->reset) {
                clear_activity_state();
                core_api_->reset(core_inst_, true);
                show_toast("CORE RESET");
            }
            break;
        case czx::video::MenuAction::Quit:
            running_ = false;
            break;
        case czx::video::MenuAction::Close:
            if (in_launcher_ && menu_from_launcher_) {
                capture_current_prefs_to_target(pending_target_index_, false);
                persist_launcher_state();
                menu_from_launcher_ = false;
                sync_router_menu_open();
                force_present_ = true;
            } else {
                commit_pending_core();
            }
            break;
        case czx::video::MenuAction::None:
            break;
    }
}

void EmulatorApp::start_scale_worker() {
    if (scale_thread_running_) return;
    {
        std::lock_guard<std::mutex> lock(scale_mtx_);
        scale_stop_ = false;
        has_pending_raw_ = false;
        has_rescale_request_ = false;
        clear_work_frame_ = false;
        std::fill(ready_fb_.begin(), ready_fb_.end(), 0);
        ready_seq_ = 0;
        presented_seq_ = 0;
        force_present_ = true;
        prev_overlay_active_ = false;
    }
    scale_thread_ = std::thread(&EmulatorApp::scale_worker_loop, this);
    scale_thread_running_ = true;
}

void EmulatorApp::stop_scale_worker() {
    if (!scale_thread_running_) return;
    {
        std::lock_guard<std::mutex> lock(scale_mtx_);
        scale_stop_ = true;
    }
    scale_cv_.notify_one();
    if (scale_thread_.joinable()) {
        scale_thread_.join();
    }
    scale_thread_running_ = false;
}

void EmulatorApp::reset_scaled_frame() {
    {
        std::lock_guard<std::mutex> lock(scale_mtx_);
        has_pending_raw_ = false;
        has_rescale_request_ = false;
        clear_work_frame_ = true;
        std::fill(ready_fb_.begin(), ready_fb_.end(), 0);
        ++ready_seq_;
    }
    scale_cv_.notify_one();
    force_present_ = true;
}

void EmulatorApp::scale_worker_loop() {
    czx_frame_t last_work_frame{};
    bool has_work_frame = false;
    while (true) {
        czx_frame_t work_frame{};
        czx::video::ScalerConfig work_cfg{};
        bool do_render = false;
        {
            std::unique_lock<std::mutex> lock(scale_mtx_);
            scale_cv_.wait(lock, [this]() {
                return scale_stop_ || has_pending_raw_ || has_rescale_request_ || clear_work_frame_;
            });
            if (scale_stop_) break;
            if (clear_work_frame_) {
                work_raw_.clear();
                last_work_frame = {};
                has_work_frame = false;
                clear_work_frame_ = false;
            }
            if (has_pending_raw_) {
                work_raw_.swap(pending_raw_);
                last_work_frame = pending_frame_;
                has_work_frame = !work_raw_.empty();
                work_frame = last_work_frame;
                work_cfg = pending_cfg_;
                has_pending_raw_ = false;
                has_rescale_request_ = false;
                do_render = has_work_frame;
            } else if (has_rescale_request_) {
                has_rescale_request_ = false;
                if (has_work_frame) {
                    work_frame = last_work_frame;
                    work_cfg = rescale_cfg_;
                    do_render = true;
                }
            }
        }
        if (!do_render) {
            continue;
        }
        work_frame.data = work_raw_.data();
        scaler_.render(work_frame, work_cfg, work_fb_.data());
        {
            std::lock_guard<std::mutex> lock(scale_mtx_);
            if (!clear_work_frame_) {
                ready_fb_.swap(work_fb_);
                ++ready_seq_;
            }
        }
    }
}

void EmulatorApp::build_overlay_argb() {
    constexpr size_t kPix =
        static_cast<size_t>(czx::video::kLcdWidth) * static_cast<size_t>(czx::video::kLcdHeight);
    if (ov_black_.size() != kPix) {
        ov_black_.resize(kPix);
        ov_white_.resize(kPix);
        overlay_argb_.resize(kPix);
    }
    std::fill(ov_black_.begin(), ov_black_.end(), uint16_t{0x0000u});
    std::fill(ov_white_.begin(), ov_white_.end(), uint16_t{0xFFFFu});

    czx::video::HudState hud_copy = hud_;
    hud_copy.desktop_native = true;
    hud_copy.desktop_scale = desktop_scale_;
    hud_copy.display_mode = czx::video::DisplayMode::OverflowFit;
    hud_copy.pan_hint_frames = 0;
    const czx::video::ViewportLayout dummy_layout{};

    if (browser_.is_open()) {
        browser_.render(ov_black_.data());
        browser_.render(ov_white_.data());
    } else if (menu_.is_open()) {
        const auto vm = build_menu_vm();
        menu_.render(ov_black_.data(), vm);
        menu_.render(ov_white_.data(), vm);
    }
    czx::video::render_hud(ov_black_.data(), hud_copy, dummy_layout);
    czx::video::render_hud(ov_white_.data(), hud_copy, dummy_layout);

    for (size_t i = 0; i < kPix; ++i) {
        const uint16_t b = ov_black_[i];
        const uint16_t w = ov_white_[i];
        if (b == 0x0000u && w == 0xFFFFu) {
            overlay_argb_[i] = 0u;
            continue;
        }
        uint8_t r0 = 0, g0 = 0, b0 = 0;
        uint8_t r1 = 0, g1 = 0, b1 = 0;
        czx::video::unpack_rgb565(b, r0, g0, b0);
        czx::video::unpack_rgb565(w, r1, g1, b1);
        if (b == w) {
            overlay_argb_[i] = 0xFF000000u | (static_cast<uint32_t>(r0) << 16) |
                               (static_cast<uint32_t>(g0) << 8) | static_cast<uint32_t>(b0);
            continue;
        }
        const int inv_a = std::clamp(static_cast<int>(g1) - static_cast<int>(g0), 0, 255);
        const int a = 255 - inv_a;
        if (a <= 0) {
            overlay_argb_[i] = 0u;
            continue;
        }
        const uint32_t sr = static_cast<uint32_t>(std::clamp((static_cast<int>(r0) * 255 + a / 2) / a, 0, 255));
        const uint32_t sg = static_cast<uint32_t>(std::clamp((static_cast<int>(g0) * 255 + a / 2) / a, 0, 255));
        const uint32_t sb = static_cast<uint32_t>(std::clamp((static_cast<int>(b0) * 255 + a / 2) / a, 0, 255));
        overlay_argb_[i] = (static_cast<uint32_t>(a) << 24) | (sr << 16) | (sg << 8) | sb;
    }
}

void EmulatorApp::update_window_title() {
    if (!platform_.display) return;
    char buf[160];
    if (in_launcher_) {
        std::snprintf(buf, sizeof(buf), "czx - Launcher");
    } else {
        const czx_core_desc_t* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
        const char* name = (desc && desc->name) ? desc->name : "czx";
        if (!core_inst_) {
            std::snprintf(buf, sizeof(buf), "czx - %s [ROM Required]", name);
        } else if (platform_.display->is_fullscreen()) {
            const int w = (native_w_ > 0) ? native_w_ : last_src_w_;
            const int h = (native_h_ > 0) ? native_h_ : last_src_h_;
            const char* asp = czx::video::aspect_policy_short(scaler_cfg_.aspect_policy);
            if (w > 0 && h > 0) {
                std::snprintf(buf, sizeof(buf), "czx - %s [HDMI %dx%d %s%s]", name, w, h, asp,
                              hw_render_active_ ? " GLES2" : "");
            } else {
                std::snprintf(buf, sizeof(buf), "czx - %s [HDMI %s%s]", name, asp,
                              hw_render_active_ ? " GLES2" : "");
            }
        } else if (desktop_native_mode_ || hw_render_active_) {
            const int w = (native_w_ > 0) ? native_w_ : last_src_w_;
            const int h = (native_h_ > 0) ? native_h_ : last_src_h_;
            if (w > 0 && h > 0) {
                std::snprintf(buf, sizeof(buf), "czx - %s [%dx%d %dx%s]", name, w, h, desktop_scale_,
                              hw_render_active_ ? " GLES2" : "");
            } else {
                std::snprintf(buf, sizeof(buf), "czx - %s [Desktop %dx%s]", name, desktop_scale_,
                              hw_render_active_ ? " GLES2" : "");
            }
        } else {
            std::snprintf(buf, sizeof(buf), "czx - %s [LCD 320x170]", name);
        }
    }
    if (last_window_title_ != buf) {
        last_window_title_ = buf;
        platform_.display->set_window_title(buf);
    }
}

void EmulatorApp::update_native_from_frame(const czx_frame_t& frame) {
    if (!frame.data || frame.width == 0 || frame.height == 0) {
        return;
    }
    int ax = 0;
    int ay = 0;
    int aw = frame.width;
    int ah = frame.height;
    if (!scaler_cfg_.show_borders && frame.crop_w > 0 && frame.crop_h > 0 &&
        static_cast<int>(frame.crop_x) + static_cast<int>(frame.crop_w) <= static_cast<int>(frame.width) &&
        static_cast<int>(frame.crop_y) + static_cast<int>(frame.crop_h) <= static_cast<int>(frame.height)) {
        ax = frame.crop_x;
        ay = frame.crop_y;
        aw = frame.crop_w;
        ah = frame.crop_h;
    }

    uint32_t margin_xrgb = 0;
    const auto* base = static_cast<const uint8_t*>(frame.data);
    if (scaler_cfg_.margin_fill == czx::video::MarginFill::Auto && aw > 0 && ah > 0) {
        const bool sample_outer =
            (frame.flags & CZX_FRAME_HAS_BORDER) != 0 &&
            frame.crop_w > 0 && frame.crop_h > 0 &&
            (frame.crop_w < frame.width || frame.crop_h < frame.height);
        const uint32_t x0 = sample_outer ? 0u : static_cast<uint32_t>(ax);
        const uint32_t x1 = sample_outer ? (frame.width - 1u) : static_cast<uint32_t>(ax + aw - 1);
        const uint32_t y0 = sample_outer ? 0u : static_cast<uint32_t>(ay);
        const uint32_t y1 = sample_outer ? (frame.height - 1u) : static_cast<uint32_t>(ay + ah - 1);
        const uint8_t* r0 = base + static_cast<size_t>(y0) * frame.pitch_bytes;
        const uint8_t* r1 = base + static_cast<size_t>(y1) * frame.pitch_bytes;
        const uint8_t* rows[4] = {r0, r0, r1, r1};
        const uint32_t cols[4] = {x0, x1, x0, x1};
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t r = 0, g = 0, b = 0;
            czx::video::read_pixel_rgb888(rows[i], cols[i], frame.format, r, g, b);
            sr += r;
            sg += g;
            sb += b;
        }
        margin_xrgb = (((sr + 2u) / 4u) << 16) | (((sg + 2u) / 4u) << 8) | ((sb + 2u) / 4u);
    }

    native_xrgb_.resize(static_cast<size_t>(aw) * static_cast<size_t>(ah));
    switch (frame.format) {
        case CZX_PIXEL_RGB565:
            for (int y = 0; y < ah; ++y) {
                const auto* src16 = reinterpret_cast<const uint16_t*>(
                    base + static_cast<size_t>(ay + y) * frame.pitch_bytes) + ax;
                uint32_t* dst = native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                for (int x = 0; x < aw; ++x) {
                    uint8_t r = 0, g = 0, b = 0;
                    czx::video::unpack_rgb565(src16[x], r, g, b);
                    dst[x] = (static_cast<uint32_t>(r) << 16) |
                             (static_cast<uint32_t>(g) << 8) |
                             static_cast<uint32_t>(b);
                }
            }
            break;
        case CZX_PIXEL_XRGB8888:
            for (int y = 0; y < ah; ++y) {
                const auto* src32 = reinterpret_cast<const uint32_t*>(
                    base + static_cast<size_t>(ay + y) * frame.pitch_bytes) + ax;
                uint32_t* dst = native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                std::copy_n(src32, aw, dst);
            }
            break;
        case CZX_PIXEL_ABGR8888:
            for (int y = 0; y < ah; ++y) {
                const auto* src32 = reinterpret_cast<const uint32_t*>(
                    base + static_cast<size_t>(ay + y) * frame.pitch_bytes) + ax;
                uint32_t* dst = native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                for (int x = 0; x < aw; ++x) {
                    const uint32_t p = src32[x];
                    dst[x] = ((p & 0x000000FFu) << 16) |
                             (p & 0x0000FF00u) |
                             ((p >> 16) & 0x000000FFu);
                }
            }
            break;
    }

    native_w_ = aw;
    native_h_ = ah;
    native_par_num_ = frame.par_num ? frame.par_num : 1;
    native_par_den_ = frame.par_den ? frame.par_den : 1;
    native_margin_xrgb_ = margin_xrgb;
    ++native_seq_;
}

void EmulatorApp::refresh_paused_preview() {
    force_present_ = true;
    if (!core_inst_ || cfg_.headless_frames > 0) {
        return;
    }
    if (hw_render_active_) {
        sync_desktop_window_size(false);
        return;
    }
    if (desktop_native_mode_) {
        if (desktop_has_last_raw_ && !desktop_last_raw_bytes_.empty()) {
            czx_frame_t f = desktop_last_raw_frame_;
            f.data = desktop_last_raw_bytes_.data();
            update_native_from_frame(f);
            sync_desktop_window_size(false);
        }
        return;
    }

    if (has_last_frame_meta_) {
        last_layout_ = czx::video::Scaler::compute_layout(last_frame_meta_, scaler_cfg_);
        scaler_cfg_.pan_x = last_layout_.pan_x;
        scaler_cfg_.pan_y = last_layout_.pan_y;
    }
    if (!scale_thread_running_) {
        return;
    }
    if (desktop_has_last_raw_ && !desktop_last_raw_bytes_.empty()) {
        std::vector<uint8_t> raw_copy = desktop_last_raw_bytes_;
        {
            std::lock_guard<std::mutex> lock(scale_mtx_);
            pending_raw_.swap(raw_copy);
            pending_frame_ = desktop_last_raw_frame_;
            pending_cfg_ = scaler_cfg_;
            has_pending_raw_ = true;
        }
        scale_cv_.notify_one();
    } else {
        {
            std::lock_guard<std::mutex> lock(scale_mtx_);
            rescale_cfg_ = scaler_cfg_;
            has_rescale_request_ = true;
        }
        scale_cv_.notify_one();
    }
}

void EmulatorApp::cb_video_frame(void* user_data, const czx_frame_t* frame) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !frame) return;
    self->last_src_w_ = frame->width;
    self->last_src_h_ = frame->height;
    self->last_layout_ = czx::video::Scaler::compute_layout(*frame, self->scaler_cfg_);
    self->scaler_cfg_.pan_x = self->last_layout_.pan_x;
    self->scaler_cfg_.pan_y = self->last_layout_.pan_y;

    self->last_frame_meta_ = *frame;
    self->last_frame_meta_.data = nullptr;
    self->has_last_frame_meta_ = (frame->data != nullptr && frame->width > 0 && frame->height > 0);

    if ((frame->flags & CZX_FRAME_HW_RENDER) != 0u) {
        self->hw_render_active_ = true;
        self->hw_bottom_left_ = ((frame->flags & CZX_FRAME_BOTTOM_LEFT) != 0u);
        self->native_w_ = frame->crop_w ? frame->crop_w : frame->width;
        self->native_h_ = frame->crop_h ? frame->crop_h : frame->height;
        self->native_par_num_ = frame->par_num ? frame->par_num : 1;
        self->native_par_den_ = frame->par_den ? frame->par_den : 1;
        self->native_margin_xrgb_ = 0u;
        self->saw_nonblank_frame_ = true;
        ++self->native_seq_;
        return;
    }

    self->hw_render_active_ = false;
    self->hw_bottom_left_ = false;

    if (self->has_last_frame_meta_ && self->cfg_.headless_frames == 0 &&
        self->platform_.display && self->platform_.display->supports_native_present()) {
        const size_t bpp = (frame->format == CZX_PIXEL_RGB565) ? 2u : 4u;
        const size_t row_bytes = static_cast<size_t>(frame->width) * bpp;
        const size_t total_bytes = row_bytes * static_cast<size_t>(frame->height);
        self->desktop_last_raw_bytes_.resize(total_bytes);
        if (frame->pitch_bytes == row_bytes) {
            std::memcpy(self->desktop_last_raw_bytes_.data(), frame->data, total_bytes);
        } else {
            const auto* src = static_cast<const uint8_t*>(frame->data);
            for (uint16_t y = 0; y < frame->height; ++y) {
                std::memcpy(self->desktop_last_raw_bytes_.data() + static_cast<size_t>(y) * row_bytes,
                            src + static_cast<size_t>(y) * frame->pitch_bytes,
                            row_bytes);
            }
        }
        self->desktop_last_raw_frame_ = *frame;
        self->desktop_last_raw_frame_.data = nullptr;
        self->desktop_last_raw_frame_.pitch_bytes = static_cast<uint32_t>(row_bytes);
        self->desktop_has_last_raw_ = true;
    }

    if (self->desktop_native_mode_ && self->cfg_.headless_frames == 0) {
        self->update_native_from_frame(*frame);
        return;
    }

    if (!self->scale_thread_running_) {
        self->scaler_.render(*frame, self->scaler_cfg_, self->lcd_fb_.data());
        if (!self->saw_nonblank_frame_) {
            for (uint16_t px : self->lcd_fb_) {
                if (px != 0) {
                    self->saw_nonblank_frame_ = true;
                    break;
                }
            }
        }
        return;
    }

    if (!frame->data || frame->width == 0 || frame->height == 0) {
        return;
    }

    const size_t bpp = (frame->format == CZX_PIXEL_RGB565) ? 2u : 4u;
    const size_t row_bytes = static_cast<size_t>(frame->width) * bpp;
    const size_t total_bytes = row_bytes * static_cast<size_t>(frame->height);
    self->stage_raw_.resize(total_bytes);
    if (frame->pitch_bytes == row_bytes) {
        std::memcpy(self->stage_raw_.data(), frame->data, total_bytes);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame->data);
        for (uint16_t y = 0; y < frame->height; ++y) {
            std::memcpy(self->stage_raw_.data() + static_cast<size_t>(y) * row_bytes,
                        src + static_cast<size_t>(y) * frame->pitch_bytes,
                        row_bytes);
        }
    }

    czx_frame_t copy_meta = *frame;
    copy_meta.data = nullptr;
    copy_meta.pitch_bytes = static_cast<uint32_t>(row_bytes);
    {
        std::lock_guard<std::mutex> lock(self->scale_mtx_);
        self->pending_raw_.swap(self->stage_raw_);
        self->pending_frame_ = copy_meta;
        self->pending_cfg_ = self->scaler_cfg_;
        self->has_pending_raw_ = true;
    }
    self->scale_cv_.notify_one();
}

void EmulatorApp::cb_audio_samples(void* user_data, const int16_t* stereo_s16, size_t frame_count) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.audio) return;
    self->platform_.audio->write_samples(stereo_s16, frame_count);
}

void EmulatorApp::cb_log(void*, int, const char* msg) {
    if (msg) std::fprintf(stderr, "[core] %s\n", msg);
}

bool EmulatorApp::cb_hw_gles2_enable(void* user_data, bool /*depth*/, bool /*stencil*/, bool bottom_left_origin) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.display || !self->platform_.display->supports_hw_gles2() ||
        !self->platform_.display->hw_make_current()) {
        return false;
    }
    self->hw_render_active_ = true;
    self->hw_bottom_left_ = bottom_left_origin;
    return true;
}

bool EmulatorApp::cb_hw_make_current(void* user_data) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.display) return false;
    return self->platform_.display->hw_make_current();
}

void* EmulatorApp::cb_hw_get_proc_address(void* user_data, const char* sym) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.display) return nullptr;
    return self->platform_.display->hw_get_proc_address(sym);
}

bool EmulatorApp::cb_hw_ensure_fbo(void* user_data, uint32_t width, uint32_t height, bool depth, bool stencil) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.display) return false;
    return self->platform_.display->hw_ensure_fbo(
        static_cast<int>(width), static_cast<int>(height), depth, stencil);
}

uintptr_t EmulatorApp::cb_hw_get_current_framebuffer(void* user_data) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !self->platform_.display) return 0;
    return self->platform_.display->hw_get_current_framebuffer();
}

void EmulatorApp::cb_host_command(void* user_data, czx::input::HostCommand cmd) {
    static_cast<EmulatorApp*>(user_data)->handle_host_command(cmd);
}

void EmulatorApp::cb_pan(void* user_data, int dx, int dy) {
    static_cast<EmulatorApp*>(user_data)->apply_pan_delta(dx, dy);
}

void EmulatorApp::cb_menu_key(void* user_data, const czx_key_event_t& ev) {
    static_cast<EmulatorApp*>(user_data)->handle_menu_key(ev);
}

void EmulatorApp::cb_guest_key(void* user_data, const czx_key_event_t& ev) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (self->core_api_ && self->core_inst_ && self->core_api_->on_key) {
        self->core_api_->on_key(self->core_inst_, &ev);
    }
}

void EmulatorApp::cb_guest_joystick(void* user_data, uint32_t port, uint32_t buttons) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (self->core_api_ && self->core_inst_ && self->core_api_->on_joystick) {
        self->core_api_->on_joystick(self->core_inst_, port, buttons);
    }
}

void EmulatorApp::cb_guest_mouse(void* user_data, int32_t dx, int32_t dy, uint32_t buttons) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (self->core_api_ && self->core_inst_ && self->core_api_->on_mouse) {
        self->core_api_->on_mouse(self->core_inst_, dx, dy, buttons);
    }
}

int EmulatorApp::run() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    g_signal_quit = 0;

    if (cfg_.headless_frames == 0) {
        launcher_state_ = load_launcher_state(launcher_state_path_);
        for (const auto& [tid, ts] : launcher_state_.targets) {
            if (ts.last_media_dir.empty()) continue;
            last_media_dirs_[tid] = ts.last_media_dir;
            if (tid == "c64") {
                last_media_dirs_["vice_x64"] = ts.last_media_dir;
                last_media_dirs_["vice_x64sc"] = ts.last_media_dir;
            }
        }
    }

    const bool start_in_launcher =
        (cfg_.headless_frames == 0) && cfg_.core_spec.empty() &&
        cfg_.media_path.empty() && cfg_.media2_path.empty() && !targets_.empty();

    int initial_idx = 0;
    if (!start_in_launcher) {
        if (!cfg_.core_spec.empty()) {
            const czx_core_api_t* found = registry_.find_by_id(cfg_.core_spec);
            if (!found && cfg_.core_spec.size() > 3 &&
                cfg_.core_spec.compare(cfg_.core_spec.size() - 3, 3, ".so") == 0) {
                if (registry_.load_libretro_core(cfg_.core_spec)) {
                    found = registry_.get(registry_.count() - 1);
                    targets_ = build_machine_targets(registry_);
                }
            }
            if (found) {
                initial_idx = registry_.index_of(found);
            } else {
                const int tidx = find_target_index_by_id(targets_, cfg_.core_spec);
                if (tidx >= 0) {
                    const int vidx = (cfg_.headless_frames == 0)
                                         ? resolve_target_variant_index(tidx)
                                         : targets_[static_cast<size_t>(tidx)].default_variant_index;
                    initial_idx = targets_[static_cast<size_t>(tidx)].resolve_core_index(vidx);
                } else {
                    std::fprintf(stderr, "czx: requested core '%s' not found\n", cfg_.core_spec.c_str());
                    return 2;
                }
            }
        } else if (!cfg_.media_path.empty()) {
            const czx_core_api_t* detected = registry_.find_for_media(cfg_.media_path);
            if (detected) {
                initial_idx = registry_.index_of(detected);
                if (cfg_.headless_frames == 0) {
                    const int tidx = find_target_index_for_core_index(targets_, initial_idx);
                    if (tidx >= 0 && targets_[static_cast<size_t>(tidx)].has_variants()) {
                        const int vidx = resolve_target_variant_index(tidx);
                        const int preferred = targets_[static_cast<size_t>(tidx)].resolve_core_index(vidx);
                        if (preferred >= 0) {
                            initial_idx = preferred;
                        }
                    }
                }
                const czx_core_api_t* chosen = registry_.get(static_cast<size_t>(initial_idx));
                const czx_core_desc_t* d = (chosen && chosen->get_desc) ? chosen->get_desc(chosen) : nullptr;
                if (d && d->id) {
                    std::fprintf(stderr, "czx: auto-selected core '%s' for '%s'\n",
                                 d->id, cfg_.media_path.c_str());
                }
            }
        }
    }

    cfg_.platform.headless = (cfg_.headless_frames > 0);
    cfg_.platform.desktop_native =
        !cfg_.platform.headless && !cfg_.force_lcd_sim &&
        (cfg_.force_desktop || !cfg_.display_mode_explicit);
    cfg_.platform.vsync = vsync_enabled_;
    platform_ = czx::platform::create_platform_bundle(cfg_.platform);
    if (platform_.display && platform_.display->supports_vsync_toggle()) {
        vsync_enabled_ = platform_.display->is_vsync_enabled();
    }
    const bool hdmi_fullscreen = platform_.display && platform_.display->is_fullscreen();
    default_desktop_native_ = !cfg_.platform.headless && !cfg_.force_lcd_sim &&
                              platform_.display &&
                              platform_.display->supports_native_present() &&
                              (hdmi_fullscreen || cfg_.platform.desktop_native);
    default_desktop_scale_ = std::clamp(cfg_.desktop_scale, 1, 3);
    desktop_native_mode_ = default_desktop_native_;
    desktop_scale_ = default_desktop_scale_;
    hud_.fullscreen_native = hdmi_fullscreen;
    hud_.desktop_native = desktop_native_mode_;
    hud_.desktop_scale = desktop_scale_;
    hud_.aspect_policy = scaler_cfg_.aspect_policy;

    czx::input::RouterCallbacks rcb{};
    rcb.user_data = this;
    rcb.on_host_command = cb_host_command;
    rcb.on_pan = cb_pan;
    rcb.on_menu_key = cb_menu_key;
    rcb.on_guest_key = cb_guest_key;
    rcb.on_guest_joystick = cb_guest_joystick;
    rcb.on_guest_mouse = cb_guest_mouse;
    router_.set_callbacks(rcb);

    hud_.display_mode = scaler_cfg_.mode;
    router_.set_native_pan_enabled(scaler_cfg_.mode == czx::video::DisplayMode::NativePan);

    if (cfg_.headless_frames == 0) {
        start_scale_worker();
        if (platform_.audio) {
            platform_.audio->init(44100);
        }
    }

    if (start_in_launcher) {
        int focus_idx = find_target_index_by_id(targets_, launcher_state_.last_target_id);
        if (focus_idx < 0) focus_idx = 0;
        enter_launcher(focus_idx);
    } else {
        const bool activated = (cfg_.headless_frames == 0)
                                   ? activate_core_with_loading(initial_idx, cfg_.media_path, cfg_.media2_path)
                                   : activate_core(initial_idx, cfg_.media_path, cfg_.media2_path);
        if (!activated) {
            if (cfg_.headless_frames > 0) {
                return 1;
            }
            show_toast("ROM REQUIRED");
        }
        int cli_vidx = 0;
        const int tidx = find_target_index_for_core_index(targets_, core_index_, &cli_vidx);
        active_target_index_ = tidx;
        pending_target_index_ = tidx;
        active_variant_index_ = cli_vidx;
        pending_variant_index_ = cli_vidx;
        active_media1_ = cfg_.media_path;
        active_media2_ = cfg_.media2_path;
        if (!active_media1_.empty()) {
            active_media1_label_ = fs::path(active_media1_).filename().string();
        }
        if (!active_media2_.empty()) {
            active_media2_label_ = fs::path(active_media2_).filename().string();
        }
        if (core_api_ && core_inst_ && core_api_->get_drive_media_label) {
            if (const char* l0 = core_api_->get_drive_media_label(core_inst_, 0)) {
                if (l0[0] != '\0') active_media1_label_ = l0;
            }
            if (const char* l1 = core_api_->get_drive_media_label(core_inst_, 1)) {
                if (l1[0] != '\0') active_media2_label_ = l1;
            }
        }
        if (cfg_.headless_frames == 0 && active_target_index_ >= 0) {
            launcher_index_ = active_target_index_;
            launcher_.set_selected(launcher_index_, static_cast<int>(targets_.size()), false);
            launcher_state_.last_target_id = targets_[static_cast<size_t>(active_target_index_)].id;
            capture_current_prefs_to_target(active_target_index_, activated);
            persist_launcher_state();
        }
        sync_desktop_window_size(false);
        update_window_title();
    }

    auto get_target_fps = [&]() {
        const czx_core_desc_t* d = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
        const double fps = (d && d->fps_num > 0 && d->fps_den > 0)
                               ? (static_cast<double>(d->fps_num) / static_cast<double>(d->fps_den))
                               : 60.0;
        return std::clamp(fps, 10.0, 240.0);
    };
    auto frame_period_for_fps = [](double fps, double pace_adj) {
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>((1.0 / fps) * (1.0 + pace_adj)));
    };

    double last_target_fps = get_target_fps();
    double audio_pace_adj = 0.0;
    hud_.target_fps_x10 = std::clamp(static_cast<int>(std::lround(last_target_fps * 10.0)), 100, 2400);

    auto last_tick = std::chrono::steady_clock::now();
    auto next_frame = last_tick + frame_period_for_fps(last_target_fps, 0.0);
    auto fps_window_start = last_tick;
    int fps_window_frames = 0;
    bool fps_initialized = false;
    bool was_paused = false;
    uint64_t seen_core_epoch = core_epoch_;
    std::vector<czx::input::RawKeyEvent> raw_events;
    std::vector<czx::platform::RawMouseEvent> raw_mouse;
    int frames_done = 0;

    while (running_ && !g_signal_quit) {
        if (platform_.display && platform_.display->should_close()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const double dt_sec = std::chrono::duration<double>(now - last_tick).count();
        last_tick = now;

        const double cur_target_fps = get_target_fps();
        hud_.target_fps_x10 = std::clamp(static_cast<int>(std::lround(cur_target_fps * 10.0)), 100, 2400);
        if (std::abs(cur_target_fps - last_target_fps) > 0.05) {
            last_target_fps = cur_target_fps;
            audio_pace_adj = 0.0;
            next_frame = now + frame_period_for_fps(cur_target_fps, 0.0);
            fps_window_start = now;
            fps_window_frames = 0;
            fps_initialized = false;
        }

        const double fps_ms = std::chrono::duration<double, std::milli>(now - fps_window_start).count();
        const double fps_interval = fps_initialized ? 500.0 : 250.0;
        if (fps_window_frames > 0 && fps_ms >= fps_interval) {
            const double fps = static_cast<double>(fps_window_frames) * 1000.0 / fps_ms;
            hud_.fps_x10 = std::clamp(static_cast<int>(std::lround(fps * 10.0)), 1, 9999);
            fps_window_start = now;
            fps_window_frames = 0;
            fps_initialized = true;
        }

        raw_events.clear();
        raw_mouse.clear();
        if (platform_.input) {
            platform_.input->poll(raw_events);
            platform_.input->poll_mouse(raw_mouse);
        }
        for (const auto& ev : raw_events) {
            router_.handle_raw_key(ev);
        }
        if (!in_launcher_ && !browser_.is_open() && !menu_.is_open()) {
            for (const auto& m : raw_mouse) {
                cb_guest_mouse(this, m.dx, m.dy, m.buttons);
            }
        }
        router_.tick(dt_sec);

        const bool anim_dirty =
            in_launcher_ && !menu_.is_open() && !browser_.is_open() && launcher_.tick(dt_sec);

        const bool menu_paused =
            (cfg_.headless_frames == 0) && (in_launcher_ || browser_.is_open() || menu_.is_open());
        if ((was_paused && !menu_paused) || (seen_core_epoch != core_epoch_)) {
            seen_core_epoch = core_epoch_;
            const auto reset_now = std::chrono::steady_clock::now();
            last_target_fps = get_target_fps();
            audio_pace_adj = 0.0;
            last_tick = reset_now;
            next_frame = reset_now + frame_period_for_fps(last_target_fps, 0.0);
            fps_window_start = reset_now;
            fps_window_frames = 0;
            fps_initialized = false;
        }
        was_paused = menu_paused;

        if (hud_.pan_hint_frames > 0) --hud_.pan_hint_frames;
        if (hud_.toast_frames > 0) --hud_.toast_frames;
        if (hud_.volume_osd_frames > 0) --hud_.volume_osd_frames;
        hud_.alt_held = router_.alt_held();
        hud_.force_alt_bar = in_launcher_ && !menu_.is_open() && !browser_.is_open();
        hud_.panning_active = router_.panning_held();
        hud_.input_mode = static_cast<int>(router_.input_mode());
        hud_.menu_open = menu_.is_open();

        const bool interactive_overlay =
            in_launcher_ || browser_.is_open() || menu_.is_open() || hud_.alt_held;
        const bool do_warp = (cfg_.headless_frames == 0) && tape_warp_enabled_ && core_inst_ &&
                             live_warp_requested_ && !interactive_overlay;
        if (menu_paused) {
            if (core_inst_ && core_api_ && core_api_->set_fast_forward) {
                core_api_->set_fast_forward(core_inst_, false, false);
            }
            if (!in_launcher_ && core_inst_ && platform_.audio) {
                static constexpr int16_t kSilence[2048 * 2] = {};
                const czx_core_desc_t* d =
                    (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
                const uint32_t sr = (d && d->audio_sample_rate > 0u) ? d->audio_sample_rate : 48000u;
                const size_t nframes = std::clamp<size_t>(
                    static_cast<size_t>(std::llround(static_cast<double>(sr) / cur_target_fps)),
                    160u, 2048u);
                platform_.audio->write_samples(kSilence, nframes);
            }
        } else if (!do_warp) {
            if (core_inst_ && core_api_ && core_api_->set_fast_forward) {
                core_api_->set_fast_forward(core_inst_, false, false);
            }
            if (core_inst_ && core_api_ && core_api_->run_frame) {
                core_api_->run_frame(core_inst_);
            }
            poll_core_activity();
        } else {
            constexpr auto kWarpSlice = std::chrono::milliseconds(33);
            const auto slice_end = std::chrono::steady_clock::now() + kWarpSlice;
            for (;;) {
                if (core_api_->set_fast_forward) {
                    core_api_->set_fast_forward(core_inst_, true, true);
                }
                core_api_->run_frame(core_inst_);
                czx_activity_t live{};
                live.tape_percent = -1;
                const bool has_live =
                    core_api_->get_activity && core_api_->get_activity(core_inst_, &live);
                if (!has_live || (live.flags & CZX_ACTIVITY_FASTFORWARD) == 0u) {
                    if (core_api_->set_fast_forward) {
                        core_api_->set_fast_forward(core_inst_, false, false);
                    }
                    core_api_->run_frame(core_inst_);
                    poll_core_activity();
                    break;
                }
                if (std::chrono::steady_clock::now() >= slice_end) {
                    if (core_api_->set_fast_forward) {
                        core_api_->set_fast_forward(core_inst_, true, false);
                    }
                    core_api_->run_frame(core_inst_);
                    poll_core_activity();
                    break;
                }
            }
        }

        if (desktop_native_mode_ || (hw_render_active_ && !in_launcher_ && core_inst_)) {
            sync_desktop_window_size(false);
            const bool activity_overlay = !in_launcher_ && hud_.show_activity_indicator &&
                                          hud_.activity_flags != 0u && !browser_.is_open() &&
                                          !menu_.is_open();
            const bool overlay_active = in_launcher_ || (!core_inst_) || browser_.is_open() ||
                                        menu_.is_open() || hud_.alt_held || hud_.pan_hint_frames > 0 ||
                                        hud_.toast_frames > 0 || hud_.volume_osd_frames > 0 ||
                                        activity_overlay;
            const bool redraw_req = platform_.display && platform_.display->consume_redraw_request();
            const bool has_new_frame = (native_seq_ != presented_native_seq_);
            const bool need_present =
                has_new_frame || overlay_active || prev_overlay_active_ || force_present_ ||
                anim_dirty || redraw_req;
            if (need_present) {
                if (in_launcher_ || !core_inst_) {
                    if (in_launcher_) {
                        launcher_.render(lcd_fb_.data(), build_launcher_items());
                    } else {
                        render_core_error_screen();
                    }
                    if (browser_.is_open()) {
                        browser_.render(lcd_fb_.data());
                    } else if (menu_.is_open()) {
                        menu_.render(lcd_fb_.data(), build_menu_vm());
                    }
                    czx::video::render_hud(lcd_fb_.data(), hud_, last_layout_);
                    if (platform_.display) {
                        platform_.display->present(lcd_fb_.data());
                    }
                } else if (hw_render_active_) {
                    presented_native_seq_ = native_seq_;
                    czx::platform::NativeHwFrameView view{};
                    view.width = native_w_;
                    view.height = native_h_;
                    view.par_num = native_par_num_;
                    view.par_den = native_par_den_;
                    view.aspect_policy = scaler_cfg_.aspect_policy;
                    view.bottom_left_origin = hw_bottom_left_;
                    view.margin_xrgb = 0u;
                    if (overlay_active) {
                        build_overlay_argb();
                        view.overlay_argb = overlay_argb_.data();
                        view.dim_outside_overlay = browser_.is_open() || menu_.is_open();
                    }
                    if (platform_.display) {
                        platform_.display->present_hw(view);
                    }
                } else {
                    presented_native_seq_ = native_seq_;
                    czx::platform::NativeFrameView view{};
                    view.xrgb = native_xrgb_.empty() ? nullptr : native_xrgb_.data();
                    view.width = native_w_;
                    view.height = native_h_;
                    view.par_num = native_par_num_;
                    view.par_den = native_par_den_;
                    view.aspect_policy = scaler_cfg_.aspect_policy;
                    view.margin_xrgb =
                        (scaler_cfg_.margin_fill == czx::video::MarginFill::Auto) ? native_margin_xrgb_ : 0u;
                    if (overlay_active) {
                        build_overlay_argb();
                        view.overlay_argb = overlay_argb_.data();
                        view.dim_outside_overlay = browser_.is_open() || menu_.is_open();
                    }
                    if (platform_.display) {
                        platform_.display->present_native(view);
                    }
                }
                update_window_title();
                force_present_ = false;
            }
            prev_overlay_active_ = overlay_active;
        } else if (scale_thread_running_) {
            const bool activity_overlay = !in_launcher_ && hud_.show_activity_indicator &&
                                          hud_.activity_flags != 0u && !browser_.is_open() &&
                                          !menu_.is_open();
            const bool overlay_active = in_launcher_ || (!core_inst_) || browser_.is_open() ||
                                        menu_.is_open() || hud_.alt_held || hud_.pan_hint_frames > 0 ||
                                        hud_.toast_frames > 0 || hud_.volume_osd_frames > 0 ||
                                        activity_overlay;
            uint64_t latest_seq = 0;
            {
                std::lock_guard<std::mutex> lock(scale_mtx_);
                latest_seq = ready_seq_;
            }
            const bool redraw_req = platform_.display && platform_.display->consume_redraw_request();
            const bool has_new_frame = (latest_seq != presented_seq_);
            const bool need_present =
                has_new_frame || overlay_active || prev_overlay_active_ || force_present_ ||
                anim_dirty || redraw_req;
            if (need_present) {
                if (in_launcher_) {
                    launcher_.render(lcd_fb_.data(), build_launcher_items());
                } else if (core_inst_) {
                    std::lock_guard<std::mutex> lock(scale_mtx_);
                    std::copy(ready_fb_.begin(), ready_fb_.end(), lcd_fb_.begin());
                    presented_seq_ = ready_seq_;
                } else {
                    render_core_error_screen();
                }

                if (browser_.is_open()) {
                    browser_.render(lcd_fb_.data());
                } else if (menu_.is_open()) {
                    menu_.render(lcd_fb_.data(), build_menu_vm());
                }

                czx::video::render_hud(lcd_fb_.data(), hud_, last_layout_);

                if (platform_.display) {
                    platform_.display->present(lcd_fb_.data());
                }
                update_window_title();
                force_present_ = false;
            }
            prev_overlay_active_ = overlay_active;
        } else {
            if (in_launcher_) {
                launcher_.render(lcd_fb_.data(), build_launcher_items());
            } else if (!core_inst_) {
                render_core_error_screen();
            }

            if (browser_.is_open()) {
                browser_.render(lcd_fb_.data());
            } else if (menu_.is_open()) {
                menu_.render(lcd_fb_.data(), build_menu_vm());
            }

            czx::video::render_hud(lcd_fb_.data(), hud_, last_layout_);

            if (platform_.display) {
                platform_.display->present(lcd_fb_.data());
            }
        }

        ++frames_done;
        if (!menu_paused) {
            ++fps_window_frames;
        }
        if (cfg_.headless_frames > 0 && frames_done >= cfg_.headless_frames) {
            break;
        }

        if (cfg_.headless_frames == 0) {
            const double end_target_fps = get_target_fps();
            hud_.target_fps_x10 = std::clamp(static_cast<int>(std::lround(end_target_fps * 10.0)), 100, 2400);
            if (do_warp || (tape_warp_enabled_ && live_warp_requested_ && !interactive_overlay)) {
                audio_pace_adj = 0.0;
                last_target_fps = end_target_fps;
                next_frame = std::chrono::steady_clock::now() + frame_period_for_fps(end_target_fps, 0.0);
            } else {
                const auto after = std::chrono::steady_clock::now();
                if (std::abs(end_target_fps - last_target_fps) > 0.05) {
                    last_target_fps = end_target_fps;
                    audio_pace_adj = 0.0;
                    next_frame = after;
                    fps_window_start = after;
                    fps_window_frames = 0;
                    fps_initialized = false;
                } else if (!menu_paused && core_inst_ && platform_.audio &&
                           !(platform_.display && platform_.display->is_vsync_enabled())) {
                    const double fill = platform_.audio->get_buffer_fill_ratio();
                    if (fill >= 0.0) {
                        const double raw_adj = std::clamp((fill - 0.5) * 0.005, -0.0025, 0.0025);
                        audio_pace_adj = audio_pace_adj * 0.95 + raw_adj * 0.05;
                    }
                } else {
                    audio_pace_adj = 0.0;
                }
                const auto frame_period = frame_period_for_fps(end_target_fps, audio_pace_adj);
                if (after < next_frame) {
                    std::this_thread::sleep_until(next_frame);
                } else if (after - next_frame > frame_period * 3) {
                    next_frame = after;
                }
                next_frame += frame_period;
            }
        }
    }

    stop_scale_worker();

    if (cfg_.headless_frames == 0 && !targets_.empty()) {
        if (!in_launcher_ && active_target_index_ >= 0) {
            capture_current_prefs_to_target(active_target_index_, core_inst_ != nullptr);
        }
        persist_launcher_state();
    }

    if (cfg_.verify_nonblank && !saw_nonblank_frame_) {
        std::fprintf(stderr, "czx: verify-nonblank failed: output stayed blank\n");
        return 3;
    }
    return 0;
}

}  // namespace czx::app
