#include "app/emulator_app.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace czx::app {

namespace {

volatile sig_atomic_t g_signal_quit = 0;

void on_signal(int) {
    g_signal_quit = 1;
}

std::string resolve_data_dir(const std::string& explicit_dir) {
    if (!explicit_dir.empty()) return explicit_dir;
    if (const char* env = std::getenv("CZX_DATA_DIR"); env && env[0] != '\0') return env;
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return (fs::path(home) / "czx").string();
    }
    return "/tmp/czx";
}

}  // namespace

EmulatorApp::EmulatorApp(AppConfig cfg)
    : cfg_(std::move(cfg)),
      lcd_fb_(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0) {
    cfg_.data_dir = resolve_data_dir(cfg_.data_dir);
    setenv("CZX_DATA_DIR", cfg_.data_dir.c_str(), 1);
    ensure_data_dirs();

    registry_ = czx::core::CoreRegistry::create_default(cfg_.core_dir, cfg_.data_dir);
    scaler_cfg_.mode = cfg_.display_mode;
    scaler_cfg_.aspect_policy = cfg_.aspect_policy;
    scaler_cfg_.show_borders = cfg_.show_borders;
    scaler_cfg_.margin_fill = cfg_.margin_fill;
    preset_index_ = cfg_.preset_index;
}

EmulatorApp::~EmulatorApp() {
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
    fs::create_directories(root / "media" / "apple2", ec);
    fs::create_directories(root / "media" / "c64", ec);
    fs::create_directories(root / "media" / "pc88", ec);
    fs::create_directories(root / "media" / "pc98", ec);
    fs::create_directories(root / "saves", ec);
    fs::create_directories(root / "config", ec);
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
    return media_root.string();
}

void EmulatorApp::destroy_active_core() {
    if (core_api_ && core_inst_ && core_api_->destroy) {
        core_api_->destroy(core_inst_);
    }
    core_inst_ = nullptr;
}

bool EmulatorApp::activate_core(int index, const std::string& initial_media) {
    if (registry_.count() == 0) return false;
    const int wrapped = (index % static_cast<int>(registry_.count()) + static_cast<int>(registry_.count())) %
                        static_cast<int>(registry_.count());
    const czx_core_api_t* next_api = registry_.get(static_cast<size_t>(wrapped));
    if (!next_api) return false;

    destroy_active_core();
    core_api_ = next_api;
    core_index_ = wrapped;
    core_error_.clear();
    scaler_cfg_.pan_x = 0;
    scaler_cfg_.pan_y = 0;
    last_src_w_ = 0;
    last_src_h_ = 0;
    last_layout_ = {};

    czx_host_t host{};
    host.user_data = this;
    host.video_frame = cb_video_frame;
    host.audio_samples = cb_audio_samples;
    host.log = cb_log;
    host.initial_media = initial_media.empty() ? nullptr : initial_media.c_str();

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
        const auto& l = vm.preset_or_media_label;
        if (l.size() >= 4) {
            auto eq = [](char a, char b) {
                return ((a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a) == b;
            };
            const size_t n = l.size();
            vm.is_tape_media = eq(l[n - 4], '.') && eq(l[n - 3], 't') &&
                               eq(l[n - 2], 'a') && eq(l[n - 1], 'p');
        }
    } else if (core_api_ && core_inst_ && core_api_->get_preset_name) {
        vm.preset_or_media_label = core_api_->get_preset_name(core_inst_, preset_index_);
    } else {
        vm.preset_or_media_label = "Default";
    }
    vm.src_width = last_src_w_;
    vm.src_height = last_src_h_;
    vm.display_mode = scaler_cfg_.mode;
    vm.aspect_policy = scaler_cfg_.aspect_policy;
    vm.show_borders = scaler_cfg_.show_borders;
    vm.margin_fill = scaler_cfg_.margin_fill;
    vm.input_mode = static_cast<int>(router_.input_mode());
    return vm;
}

void EmulatorApp::handle_host_command(czx::input::HostCommand cmd) {
    switch (cmd) {
        case czx::input::HostCommand::SetModeNativePan:
            set_display_mode(czx::video::DisplayMode::NativePan);
            break;
        case czx::input::HostCommand::SetModeOverflowFit:
            set_display_mode(czx::video::DisplayMode::OverflowFit);
            break;
        case czx::input::HostCommand::SetModeAspectFit:
            set_display_mode(czx::video::DisplayMode::AspectFit);
            break;
        case czx::input::HostCommand::ToggleInputMode: {
            const int next_i = (static_cast<int>(router_.input_mode()) + 1) % 3;
            const auto next = static_cast<czx::input::InputMode>(next_i);
            router_.set_input_mode(next);
            hud_.input_mode = next_i;
            show_toast(next_i == 2 ? "INPUT: JOYSTICK P2"
                                   : (next_i == 1 ? "INPUT: JOYSTICK P1" : "INPUT: KEYBOARD"));
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
            menu_.set_open(false);
            router_.set_menu_open(false);
            hud_.menu_open = false;
            const std::string path = browser_.selected_path();
            if (core_api_ && core_inst_ && core_api_->load_media &&
                core_api_->load_media(core_inst_, path.c_str())) {
                const std::string base = fs::path(path).filename().string();
                show_toast(("LOADED: " + base).c_str());
            } else {
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
        case czx::video::MenuAction::OpenMediaBrowser: {
            const char* exts = (core_api_ && core_inst_ && core_api_->get_extensions)
                                   ? core_api_->get_extensions(core_inst_)
                                   : nullptr;
            browser_.open(default_media_dir_for_core(), exts ? exts : "");
            break;
        }
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
            break;
        case czx::video::MenuAction::ToggleBorders:
            scaler_cfg_.show_borders = !scaler_cfg_.show_borders;
            scaler_cfg_.pan_x = 0;
            scaler_cfg_.pan_y = 0;
            break;
        case czx::video::MenuAction::ToggleMarginFill:
            scaler_cfg_.margin_fill =
                (scaler_cfg_.margin_fill == czx::video::MarginFill::Auto)
                    ? czx::video::MarginFill::Black
                    : czx::video::MarginFill::Auto;
            show_toast(scaler_cfg_.margin_fill == czx::video::MarginFill::Auto ? "MARGIN: AUTO"
                                                                               : "MARGIN: BLACK");
            break;
        case czx::video::MenuAction::ToggleInputMode:
            handle_host_command(czx::input::HostCommand::ToggleInputMode);
            break;
        case czx::video::MenuAction::ResetCore:
            if (core_api_ && core_inst_ && core_api_->reset) {
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

void EmulatorApp::cb_video_frame(void* user_data, const czx_frame_t* frame) {
    auto* self = static_cast<EmulatorApp*>(user_data);
    if (!self || !frame) return;
    self->last_src_w_ = frame->width;
    self->last_src_h_ = frame->height;
    self->last_layout_ = self->scaler_.render(*frame, self->scaler_cfg_, self->lcd_fb_.data());
    self->scaler_cfg_.pan_x = self->last_layout_.pan_x;
    self->scaler_cfg_.pan_y = self->last_layout_.pan_y;

    if (!self->saw_nonblank_frame_) {
        for (uint16_t px : self->lcd_fb_) {
            if (px != 0) {
                self->saw_nonblank_frame_ = true;
                break;
            }
        }
    }
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
    }

    cfg_.platform.headless = (cfg_.headless_frames > 0);
    platform_ = czx::platform::create_platform_bundle(cfg_.platform);

    if (!activate_core(initial_idx, cfg_.media_path)) {
        if (cfg_.headless_frames > 0) {
            return 1;
        }
        show_toast("ROM REQUIRED");
    }

    czx::input::RouterCallbacks rcb{};
    rcb.user_data = this;
    rcb.on_host_command = cb_host_command;
    rcb.on_pan = cb_pan;
    rcb.on_menu_key = cb_menu_key;
    rcb.on_guest_key = cb_guest_key;
    rcb.on_guest_joystick = cb_guest_joystick;
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
    std::vector<czx::input::RawKeyEvent> raw_events;
    int frames_done = 0;

    while (running_ && !g_signal_quit) {
        if (platform_.display && platform_.display->should_close()) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const double dt_sec = std::chrono::duration<double>(now - last_tick).count();
        last_tick = now;

        raw_events.clear();
        if (platform_.input) {
            platform_.input->poll(raw_events);
        }
        for (const auto& ev : raw_events) {
            router_.handle_raw_key(ev);
        }
        router_.tick(dt_sec);

        if (hud_.pan_hint_frames > 0) --hud_.pan_hint_frames;
        if (hud_.toast_frames > 0) --hud_.toast_frames;
        hud_.alt_held = router_.alt_held();
        hud_.panning_active = router_.panning_held();
        hud_.input_mode = static_cast<int>(router_.input_mode());
        hud_.menu_open = menu_.is_open();

        if (core_inst_) {
            core_api_->run_frame(core_inst_);
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

        ++frames_done;
        if (cfg_.headless_frames > 0 && frames_done >= cfg_.headless_frames) {
            break;
        }

        if (cfg_.headless_frames == 0) {
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

    if (cfg_.verify_nonblank && !saw_nonblank_frame_) {
        std::fprintf(stderr, "czx: verify-nonblank failed: output stayed blank\n");
        return 3;
    }
    return 0;
}

}  // namespace czx::app
