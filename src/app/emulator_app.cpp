#include "app/emulator_app.hpp"

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
    scaler_cfg_.mode = cfg_.display_mode;
    scaler_cfg_.aspect_policy = cfg_.aspect_policy;
    scaler_cfg_.show_borders = cfg_.show_borders;
    scaler_cfg_.margin_fill = cfg_.margin_fill;
    show_activity_indicator_ = cfg_.show_activity_indicator;
    tape_warp_enabled_ = cfg_.tape_warp_enabled;
    load_app_config();
    desktop_scale_ = std::clamp(cfg_.desktop_scale, 1, 3);
    hud_.desktop_scale = desktop_scale_;
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
    fs::create_directories(root / "media" / "apple2", ec);
    fs::create_directories(root / "media" / "c64", ec);
    fs::create_directories(root / "media" / "pc88", ec);
    fs::create_directories(root / "media" / "pc98", ec);
    fs::create_directories(root / "media" / "msx", ec);
    fs::create_directories(root / "media" / "g800", ec);
    fs::create_directories(root / "media" / "mz700", ec);
    fs::create_directories(root / "saves", ec);
    fs::create_directories(root / "config", ec);
}

void EmulatorApp::load_app_config() {
    if (cfg_.activity_indicator_override && cfg_.tape_warp_override) return;
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
    }
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

std::string EmulatorApp::default_media_dir_for_core() const {
    const czx_core_desc_t* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
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
    return media_root.string();
}

void EmulatorApp::destroy_active_core() {
    if (core_api_ && core_inst_ && core_api_->destroy) {
        core_api_->destroy(core_inst_);
    }
    core_inst_ = nullptr;
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
    pending_drive_ = 0;
    core_error_.clear();
    scaler_cfg_.pan_x = 0;
    scaler_cfg_.pan_y = 0;
    last_src_w_ = 0;
    last_src_h_ = 0;
    last_layout_ = {};
    native_w_ = 0;
    native_h_ = 0;
    native_par_num_ = 1;
    native_par_den_ = 1;
    native_margin_xrgb_ = 0;
    last_win_req_w_ = 0;
    last_win_req_h_ = 0;
    ++native_seq_;
    force_present_ = true;

    czx_host_t host{};
    host.user_data = this;
    host.video_frame = cb_video_frame;
    host.audio_samples = cb_audio_samples;
    host.log = cb_log;
    host.initial_media = initial_media.empty() ? nullptr : initial_media.c_str();
    host.disable_tape_warp = !tape_warp_enabled_;
    host.initial_media2 = initial_media2.empty() ? nullptr : initial_media2.c_str();

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

void EmulatorApp::set_display_mode(czx::video::DisplayMode mode) {
    desktop_native_mode_ = false;
    hud_.desktop_native = false;
    sync_desktop_window_size(false);
    force_present_ = true;
    scaler_cfg_.mode = mode;
    hud_.display_mode = mode;
    router_.set_native_pan_enabled(mode == czx::video::DisplayMode::NativePan);
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
    sync_desktop_window_size(true);
    force_present_ = true;
    char msg[32];
    std::snprintf(msg, sizeof(msg), "DESKTOP %dX", desktop_scale_);
    show_toast(msg);
}

void EmulatorApp::sync_desktop_window_size(bool force) {
    if (!platform_.display || !platform_.display->supports_native_present() || cfg_.headless_frames > 0) {
        return;
    }
    int want_w = 0;
    int want_h = 0;
    if (!desktop_native_mode_) {
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
    if (registry_.count() <= 1) return;
    if (activate_core(core_index_ + delta)) {
        const auto* desc = core_api_->get_desc(core_api_);
        if (desc && desc->name) show_toast(desc->name);
    } else {
        show_toast("ROM REQUIRED");
    }
}

void EmulatorApp::cycle_preset(int delta) {
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
}

void EmulatorApp::cycle_display_mode(int delta) {
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
    const auto* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
    vm.core_name = (desc && desc->name) ? desc->name : "Core";
    const char* exts = (core_api_ && core_inst_ && core_api_->get_extensions)
                           ? core_api_->get_extensions(core_inst_)
                           : nullptr;
    vm.has_media_browser = (exts != nullptr);
    vm.can_eject = (core_api_ && core_inst_ && core_api_->eject_media != nullptr);
    if (vm.has_media_browser) {
        const char* lbl = (core_api_->get_media_label)
                              ? core_api_->get_media_label(core_inst_)
                              : nullptr;
        vm.preset_or_media_label = lbl ? lbl : "None";
        vm.is_tape_media = is_tape_media_label(vm.preset_or_media_label);
    } else if (core_api_ && core_inst_ && core_api_->get_preset_name) {
        vm.preset_or_media_label = core_api_->get_preset_name(core_inst_, preset_index_);
    } else {
        vm.preset_or_media_label = "Default";
    }
    vm.drive_count = (core_api_ && core_inst_ && core_api_->get_drive_count)
                         ? core_api_->get_drive_count(core_inst_)
                         : 1u;
    if (vm.drive_count >= 2u && vm.has_media_browser) {
        if (core_api_->get_drive_name) {
            if (const char* n0 = core_api_->get_drive_name(core_inst_, 0)) vm.drive1_name = n0;
            if (const char* n1 = core_api_->get_drive_name(core_inst_, 1)) vm.drive2_name = n1;
        }
        const char* l0 = (core_api_->get_drive_media_label)
                             ? core_api_->get_drive_media_label(core_inst_, 0)
                             : nullptr;
        const char* l1 = (core_api_->get_drive_media_label)
                             ? core_api_->get_drive_media_label(core_inst_, 1)
                             : nullptr;
        vm.drive1_label = (l0 && l0[0] != '\0') ? l0 : "Empty";
        vm.drive2_label = (l1 && l1[0] != '\0') ? l1 : "Empty";
    }
    vm.src_width = last_src_w_;
    vm.src_height = last_src_h_;
    vm.desktop_native_active = desktop_native_mode_;
    vm.desktop_scale = desktop_scale_;
    vm.display_mode = scaler_cfg_.mode;
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
    const auto* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
    const std::string core_id = (desc && desc->id) ? desc->id : "";
    std::string start_dir = default_media_dir_for_core();
    if (!core_id.empty()) {
        const auto it = last_media_dirs_.find(core_id);
        std::error_code ec;
        if (it != last_media_dirs_.end() && fs::is_directory(it->second, ec)) {
            start_dir = it->second;
        }
    }
    const char* exts = (core_api_ && core_inst_ && core_api_->get_extensions)
                           ? core_api_->get_extensions(core_inst_)
                           : nullptr;
    const uint32_t drive_count = (core_api_ && core_inst_ && core_api_->get_drive_count)
                                     ? core_api_->get_drive_count(core_inst_)
                                     : 1u;
    std::string title = "MEDIA";
    if (drive_count >= 2u && core_api_ && core_inst_ && core_api_->get_drive_name) {
        if (const char* dname = core_api_->get_drive_name(core_inst_, drive)) {
            title = dname;
        }
    }
    const std::string d88_cache_dir = (fs::path(cfg_.data_dir) / "saves" / "d88_cache").string();
    const bool allow_auto_dual = (drive_count >= 2u && drive == 0u);
    browser_.open(start_dir, exts ? exts : "", title, d88_cache_dir, allow_auto_dual);
}

void EmulatorApp::eject_drive_slot(uint32_t drive) {
    if (!core_api_ || !core_inst_) return;
    if (core_api_->eject_drive && core_api_->eject_drive(core_inst_, drive)) {
        const char* dname =
            (core_api_->get_drive_name) ? core_api_->get_drive_name(core_inst_, drive) : nullptr;
        show_toast((std::string(dname ? dname : "Drive") + ": EJECTED").c_str());
    } else {
        show_toast("EJECT FAILED");
    }
}

void EmulatorApp::handle_host_command(czx::input::HostCommand cmd) {
    switch (cmd) {
        case czx::input::HostCommand::SetModeNativePan:
            if (desktop_native_mode_) {
                set_desktop_scale(1);
            } else {
                set_display_mode(czx::video::DisplayMode::NativePan);
            }
            break;
        case czx::input::HostCommand::SetModeOverflowFit:
            if (desktop_native_mode_) {
                set_desktop_scale(2);
            } else {
                set_display_mode(czx::video::DisplayMode::OverflowFit);
            }
            break;
        case czx::input::HostCommand::SetModeAspectFit:
            if (desktop_native_mode_) {
                set_desktop_scale(3);
            } else {
                set_display_mode(czx::video::DisplayMode::AspectFit);
            }
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
            break;
        }
        case czx::input::HostCommand::ToggleMenu:
            if (browser_.is_open()) {
                browser_.close();
                menu_.set_open(false);
            } else {
                menu_.toggle();
            }
            router_.set_menu_open(menu_.is_open());
            hud_.menu_open = menu_.is_open();
            break;
        case czx::input::HostCommand::None:
            break;
    }
}

void EmulatorApp::handle_menu_key(const czx_key_event_t& ev) {
    if (browser_.is_open()) {
        const auto ba = browser_.handle_key(ev);
        if (ba == czx::video::FileBrowserAction::Selected) {
            const auto* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
            if (desc && desc->id && !browser_.current_dir().empty()) {
                last_media_dirs_[desc->id] = browser_.current_dir();
            }
            menu_.set_open(false);
            router_.set_menu_open(false);
            hud_.menu_open = false;
            const std::string& path = browser_.selected_path();
            const std::string& label = browser_.selected_label();
            const std::string& path2 = browser_.selected_drive2_path();
            const std::string& label2 = browser_.selected_drive2_label();
            const std::string d1_disp = label.empty() ? fs::path(path).filename().string() : label;
            bool ok = false;
            if (!path2.empty() && core_api_ && core_inst_ && core_api_->load_dual_media) {
                const std::string d2_disp =
                    label2.empty() ? fs::path(path2).filename().string() : label2;
                ok = core_api_->load_dual_media(core_inst_, path.c_str(), d1_disp.c_str(),
                                                path2.c_str(), d2_disp.c_str());
                if (ok) {
                    show_toast(("LOADED D1+D2: " + d1_disp).c_str());
                }
            } else if (core_api_ && core_inst_ && core_api_->load_media_in_drive) {
                ok = core_api_->load_media_in_drive(core_inst_, pending_drive_, path.c_str(),
                                                    d1_disp.c_str());
                if (ok) {
                    const uint32_t drive_count =
                        core_api_->get_drive_count ? core_api_->get_drive_count(core_inst_) : 1u;
                    if (drive_count >= 2u && core_api_->get_drive_name) {
                        const char* dname = core_api_->get_drive_name(core_inst_, pending_drive_);
                        show_toast((std::string(dname ? dname : "Drive") + ": " + d1_disp).c_str());
                    } else {
                        show_toast(("LOADED: " + d1_disp).c_str());
                    }
                }
            } else if (core_api_ && core_inst_ && core_api_->load_media) {
                ok = core_api_->load_media(core_inst_, path.c_str());
                if (ok) {
                    show_toast(("LOADED: " + d1_disp).c_str());
                }
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
        router_.set_menu_open(false);
    }

    switch (action) {
        case czx::video::MenuAction::CycleCorePrev:
            cycle_core(-1);
            break;
        case czx::video::MenuAction::CycleCoreNext:
            cycle_core(1);
            break;
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
            cycle_preset(-1);
            break;
        case czx::video::MenuAction::CyclePresetNext:
            cycle_preset(1);
            break;
        case czx::video::MenuAction::EjectMedia:
            if (core_api_ && core_inst_ && core_api_->eject_media &&
                core_api_->eject_media(core_inst_)) {
                if (vm.is_tape_media) {
                    menu_.set_open(false);
                    router_.set_menu_open(false);
                    hud_.menu_open = false;
                    show_toast("TAPE PLAY");
                } else {
                    const char* lbl = (core_api_->get_media_label)
                                          ? core_api_->get_media_label(core_inst_)
                                          : "EJECTED";
                    show_toast(lbl);
                }
            }
            break;
        case czx::video::MenuAction::CycleDisplayModePrev:
            cycle_display_mode(-1);
            break;
        case czx::video::MenuAction::CycleDisplayModeNext:
            cycle_display_mode(1);
            break;
        case czx::video::MenuAction::ToggleAspectPolicy:
            scaler_cfg_.aspect_policy =
                (scaler_cfg_.aspect_policy == czx::video::AspectPolicy::ParAware)
                    ? czx::video::AspectPolicy::Crt4x3
                    : czx::video::AspectPolicy::ParAware;
            if (desktop_native_mode_) {
                sync_desktop_window_size(false);
            }
            force_present_ = true;
            break;
        case czx::video::MenuAction::ToggleBorders:
            scaler_cfg_.show_borders = !scaler_cfg_.show_borders;
            scaler_cfg_.pan_x = 0;
            scaler_cfg_.pan_y = 0;
            force_present_ = true;
            break;
        case czx::video::MenuAction::ToggleMarginFill:
            scaler_cfg_.margin_fill =
                (scaler_cfg_.margin_fill == czx::video::MarginFill::Auto)
                    ? czx::video::MarginFill::Black
                    : czx::video::MarginFill::Auto;
            force_present_ = true;
            show_toast(scaler_cfg_.margin_fill == czx::video::MarginFill::Auto ? "MARGIN: AUTO"
                                                                               : "MARGIN: BLACK");
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
            if (core_api_ && core_inst_ && core_api_->reset) {
                clear_activity_state();
                core_api_->reset(core_inst_, true);
                show_toast("CORE RESET");
            }
            break;
        case czx::video::MenuAction::Quit:
            running_ = false;
            break;
        case czx::video::MenuAction::Close:
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
        std::fill(ready_fb_.begin(), ready_fb_.end(), 0);
        ++ready_seq_;
    }
    force_present_ = true;
}

void EmulatorApp::scale_worker_loop() {
    while (true) {
        czx_frame_t work_frame{};
        czx::video::ScalerConfig work_cfg{};
        {
            std::unique_lock<std::mutex> lock(scale_mtx_);
            scale_cv_.wait(lock, [this]() { return scale_stop_ || has_pending_raw_; });
            if (scale_stop_) break;
            work_raw_.swap(pending_raw_);
            work_frame = pending_frame_;
            work_cfg = pending_cfg_;
            has_pending_raw_ = false;
        }
        work_frame.data = work_raw_.data();
        scaler_.render(work_frame, work_cfg, work_fb_.data());
        {
            std::lock_guard<std::mutex> lock(scale_mtx_);
            ready_fb_.swap(work_fb_);
            ++ready_seq_;
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
    const czx_core_desc_t* desc = (core_api_ && core_api_->get_desc) ? core_api_->get_desc(core_api_) : nullptr;
    const char* name = (desc && desc->name) ? desc->name : "czx";
    char buf[160];
    if (!core_inst_) {
        std::snprintf(buf, sizeof(buf), "czx - %s [ROM Required]", name);
    } else if (desktop_native_mode_) {
        const int w = (native_w_ > 0) ? native_w_ : last_src_w_;
        const int h = (native_h_ > 0) ? native_h_ : last_src_h_;
        if (w > 0 && h > 0) {
            std::snprintf(buf, sizeof(buf), "czx - %s [%dx%d %dx]", name, w, h, desktop_scale_);
        } else {
            std::snprintf(buf, sizeof(buf), "czx - %s [Desktop %dx]", name, desktop_scale_);
        }
    } else {
        std::snprintf(buf, sizeof(buf), "czx - %s [LCD 320x170]", name);
    }
    if (last_window_title_ != buf) {
        last_window_title_ = buf;
        platform_.display->set_window_title(buf);
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

    if (self->desktop_native_mode_ && self->cfg_.headless_frames == 0) {
        if (!frame->data || frame->width == 0 || frame->height == 0) {
            return;
        }
        int ax = 0;
        int ay = 0;
        int aw = frame->width;
        int ah = frame->height;
        if (!self->scaler_cfg_.show_borders && frame->crop_w > 0 && frame->crop_h > 0 &&
            static_cast<int>(frame->crop_x) + static_cast<int>(frame->crop_w) <= static_cast<int>(frame->width) &&
            static_cast<int>(frame->crop_y) + static_cast<int>(frame->crop_h) <= static_cast<int>(frame->height)) {
            ax = frame->crop_x;
            ay = frame->crop_y;
            aw = frame->crop_w;
            ah = frame->crop_h;
        }

        uint32_t margin_xrgb = 0;
        const auto* base = static_cast<const uint8_t*>(frame->data);
        if (self->scaler_cfg_.margin_fill == czx::video::MarginFill::Auto && aw > 0 && ah > 0) {
            const bool sample_outer =
                (frame->flags & CZX_FRAME_HAS_BORDER) != 0 &&
                frame->crop_w > 0 && frame->crop_h > 0 &&
                (frame->crop_w < frame->width || frame->crop_h < frame->height);
            const uint32_t x0 = sample_outer ? 0u : static_cast<uint32_t>(ax);
            const uint32_t x1 = sample_outer ? (frame->width - 1u) : static_cast<uint32_t>(ax + aw - 1);
            const uint32_t y0 = sample_outer ? 0u : static_cast<uint32_t>(ay);
            const uint32_t y1 = sample_outer ? (frame->height - 1u) : static_cast<uint32_t>(ay + ah - 1);
            const uint8_t* r0 = base + static_cast<size_t>(y0) * frame->pitch_bytes;
            const uint8_t* r1 = base + static_cast<size_t>(y1) * frame->pitch_bytes;
            const uint8_t* rows[4] = {r0, r0, r1, r1};
            const uint32_t cols[4] = {x0, x1, x0, x1};
            uint32_t sr = 0, sg = 0, sb = 0;
            for (int i = 0; i < 4; ++i) {
                uint8_t r = 0, g = 0, b = 0;
                czx::video::read_pixel_rgb888(rows[i], cols[i], frame->format, r, g, b);
                sr += r;
                sg += g;
                sb += b;
            }
            margin_xrgb = (((sr + 2u) / 4u) << 16) | (((sg + 2u) / 4u) << 8) | ((sb + 2u) / 4u);
        }

        self->native_xrgb_.resize(static_cast<size_t>(aw) * static_cast<size_t>(ah));
        switch (frame->format) {
            case CZX_PIXEL_RGB565:
                for (int y = 0; y < ah; ++y) {
                    const auto* src16 = reinterpret_cast<const uint16_t*>(
                        base + static_cast<size_t>(ay + y) * frame->pitch_bytes) + ax;
                    uint32_t* dst = self->native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
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
                        base + static_cast<size_t>(ay + y) * frame->pitch_bytes) + ax;
                    uint32_t* dst = self->native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                    std::copy_n(src32, aw, dst);
                }
                break;
            case CZX_PIXEL_ABGR8888:
                for (int y = 0; y < ah; ++y) {
                    const auto* src32 = reinterpret_cast<const uint32_t*>(
                        base + static_cast<size_t>(ay + y) * frame->pitch_bytes) + ax;
                    uint32_t* dst = self->native_xrgb_.data() + static_cast<size_t>(y) * static_cast<size_t>(aw);
                    for (int x = 0; x < aw; ++x) {
                        const uint32_t p = src32[x];
                        dst[x] = ((p & 0x000000FFu) << 16) |
                                 (p & 0x0000FF00u) |
                                 ((p >> 16) & 0x000000FFu);
                    }
                }
                break;
        }

        self->native_w_ = aw;
        self->native_h_ = ah;
        self->native_par_num_ = frame->par_num ? frame->par_num : 1;
        self->native_par_den_ = frame->par_den ? frame->par_den : 1;
        self->native_margin_xrgb_ = margin_xrgb;
        ++self->native_seq_;
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

    int initial_idx = 0;
    if (!cfg_.core_spec.empty()) {
        const czx_core_api_t* found = registry_.find_by_id(cfg_.core_spec);
        if (!found && cfg_.core_spec.size() > 3 &&
            cfg_.core_spec.compare(cfg_.core_spec.size() - 3, 3, ".so") == 0) {
            if (registry_.load_libretro_core(cfg_.core_spec)) {
                found = registry_.get(registry_.count() - 1);
            }
        }
        if (!found) {
            std::fprintf(stderr, "czx: requested core '%s' not found\n", cfg_.core_spec.c_str());
            return 2;
        }
        initial_idx = registry_.index_of(found);
    } else if (!cfg_.media_path.empty()) {
        const czx_core_api_t* detected = registry_.find_for_media(cfg_.media_path);
        if (detected) {
            initial_idx = registry_.index_of(detected);
            const czx_core_desc_t* d = detected->get_desc(detected);
            if (d && d->id) {
                std::fprintf(stderr, "czx: auto-selected core '%s' for '%s'\n",
                             d->id, cfg_.media_path.c_str());
            }
        }
    }

    cfg_.platform.headless = (cfg_.headless_frames > 0);
    cfg_.platform.desktop_native =
        !cfg_.platform.headless && !cfg_.force_lcd_sim &&
        (cfg_.force_desktop || !cfg_.display_mode_explicit);
    platform_ = czx::platform::create_platform_bundle(cfg_.platform);
    desktop_native_mode_ = !cfg_.platform.headless && platform_.display &&
                           platform_.display->supports_native_present() &&
                           cfg_.platform.desktop_native;
    hud_.desktop_native = desktop_native_mode_;
    hud_.desktop_scale = desktop_scale_;

    if (cfg_.headless_frames == 0) {
        start_scale_worker();
    }

    if (!activate_core(initial_idx, cfg_.media_path, cfg_.media2_path)) {
        if (cfg_.headless_frames > 0) {
            return 1;
        }
        show_toast("ROM REQUIRED");
    }
    sync_desktop_window_size(false);
    update_window_title();

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

    auto get_frame_period = [&]() {
        const czx_core_desc_t* d = core_api_->get_desc(core_api_);
        const double fps = (d && d->fps_num > 0 && d->fps_den > 0)
                               ? (static_cast<double>(d->fps_num) / static_cast<double>(d->fps_den))
                               : 60.0;
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / fps));
    };

    auto last_tick = std::chrono::steady_clock::now();
    auto next_frame = last_tick + get_frame_period();
    auto fps_window_start = last_tick;
    int fps_window_frames = 0;
    bool fps_initialized = false;
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
        if (!browser_.is_open() && !menu_.is_open()) {
            for (const auto& m : raw_mouse) {
                cb_guest_mouse(this, m.dx, m.dy, m.buttons);
            }
        }
        router_.tick(dt_sec);

        if (hud_.pan_hint_frames > 0) --hud_.pan_hint_frames;
        if (hud_.toast_frames > 0) --hud_.toast_frames;
        hud_.alt_held = router_.alt_held();
        hud_.panning_active = router_.panning_held();
        hud_.input_mode = static_cast<int>(router_.input_mode());
        hud_.menu_open = menu_.is_open();

        const bool interactive_overlay = browser_.is_open() || menu_.is_open() || hud_.alt_held;
        const bool do_warp = (cfg_.headless_frames == 0) && tape_warp_enabled_ && core_inst_ &&
                             live_warp_requested_ && !interactive_overlay;
        if (!do_warp) {
            if (core_inst_ && core_api_->set_fast_forward) {
                core_api_->set_fast_forward(core_inst_, false, false);
            }
            if (core_inst_) {
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

        if (desktop_native_mode_) {
            sync_desktop_window_size(false);
            const bool activity_overlay = hud_.show_activity_indicator && hud_.activity_flags != 0u &&
                                          !browser_.is_open() && !menu_.is_open();
            const bool overlay_active = (!core_inst_) || browser_.is_open() || menu_.is_open() ||
                                        hud_.alt_held || hud_.pan_hint_frames > 0 || hud_.toast_frames > 0 ||
                                        activity_overlay;
            const bool redraw_req = platform_.display && platform_.display->consume_redraw_request();
            const bool has_new_frame = (native_seq_ != presented_native_seq_);
            const bool need_present =
                has_new_frame || overlay_active || prev_overlay_active_ || force_present_ || redraw_req;
            if (need_present) {
                if (!core_inst_) {
                    render_core_error_screen();
                    if (browser_.is_open()) {
                        browser_.render(lcd_fb_.data());
                    } else if (menu_.is_open()) {
                        menu_.render(lcd_fb_.data(), build_menu_vm());
                    }
                    czx::video::render_hud(lcd_fb_.data(), hud_, last_layout_);
                    if (platform_.display) {
                        platform_.display->present(lcd_fb_.data());
                    }
                } else {
                    presented_native_seq_ = native_seq_;
                    czx::platform::NativeFrameView view{};
                    view.xrgb = native_xrgb_.empty() ? nullptr : native_xrgb_.data();
                    view.width = native_w_;
                    view.height = native_h_;
                    view.par_num = native_par_num_;
                    view.par_den = native_par_den_;
                    view.crt_4x3 = (scaler_cfg_.aspect_policy == czx::video::AspectPolicy::Crt4x3);
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
            const bool activity_overlay = hud_.show_activity_indicator && hud_.activity_flags != 0u &&
                                          !browser_.is_open() && !menu_.is_open();
            const bool overlay_active = (!core_inst_) || browser_.is_open() || menu_.is_open() ||
                                        hud_.alt_held || hud_.pan_hint_frames > 0 || hud_.toast_frames > 0 ||
                                        activity_overlay;
            uint64_t latest_seq = 0;
            {
                std::lock_guard<std::mutex> lock(scale_mtx_);
                latest_seq = ready_seq_;
            }
            const bool redraw_req = platform_.display && platform_.display->consume_redraw_request();
            const bool has_new_frame = (latest_seq != presented_seq_);
            const bool need_present =
                has_new_frame || overlay_active || prev_overlay_active_ || force_present_ || redraw_req;
            if (need_present) {
                if (core_inst_) {
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
            if (!core_inst_) {
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
        ++fps_window_frames;
        if (cfg_.headless_frames > 0 && frames_done >= cfg_.headless_frames) {
            break;
        }

        if (cfg_.headless_frames == 0) {
            if (do_warp || (tape_warp_enabled_ && live_warp_requested_ && !interactive_overlay)) {
                next_frame = std::chrono::steady_clock::now() + get_frame_period();
            } else {
                const auto frame_period = get_frame_period();
                const auto after = std::chrono::steady_clock::now();
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

    if (cfg_.verify_nonblank && !saw_nonblank_frame_) {
        std::fprintf(stderr, "czx: verify-nonblank failed: output stayed blank\n");
        return 3;
    }
    return 0;
}

}  // namespace czx::app
