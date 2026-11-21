#pragma once

#include "core/core_registry.hpp"
#include "input/input_router.hpp"
#include "platform/platform.hpp"
#include "video/file_browser.hpp"
#include "video/hud.hpp"
#include "video/menu.hpp"
#include "video/scaler.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace czx::app {

struct AppConfig {
    std::string core_spec;  // Empty = default (first loaded real core, else test_pattern)
    std::string media_path;
    std::string core_dir;
    std::string data_dir;
    uint32_t preset_index = 0;
    czx::video::DisplayMode display_mode = czx::video::DisplayMode::OverflowFit;
    czx::video::AspectPolicy aspect_policy = czx::video::AspectPolicy::ParAware;
    bool show_borders = false;
    czx::video::MarginFill margin_fill = czx::video::MarginFill::Auto;
    int headless_frames = 0;
    bool verify_nonblank = false;
    czx::platform::PlatformOptions platform;
};

class EmulatorApp {
public:
    explicit EmulatorApp(AppConfig cfg);
    ~EmulatorApp();

    int run();

private:
    AppConfig cfg_;
    czx::core::CoreRegistry registry_;
    int core_index_ = 0;
    const czx_core_api_t* core_api_ = nullptr;
    czx_core_instance_t* core_inst_ = nullptr;
    czx::platform::PlatformBundle platform_;

    czx::video::Scaler scaler_;
    czx::video::ScalerConfig scaler_cfg_;
    czx::video::ViewportLayout last_layout_{};
    czx::video::HudState hud_{};
    czx::video::OverlayMenu menu_;
    czx::video::FileBrowser browser_;
    czx::input::InputRouter router_;

    std::vector<uint16_t> lcd_fb_;
    int last_src_w_ = 0;
    int last_src_h_ = 0;
    uint32_t preset_index_ = 0;
    bool running_ = true;
    bool saw_nonblank_frame_ = false;
    std::string core_error_;

    void ensure_data_dirs();
    std::string default_media_dir_for_core() const;
    bool activate_core(int index);
    void destroy_active_core();
    void render_core_error_screen();
    czx::video::MenuViewModel build_menu_vm() const;

    void show_toast(const char* text);
    void set_display_mode(czx::video::DisplayMode mode);
    void apply_pan_delta(int dx, int dy);
    void cycle_core(int delta);
    void cycle_preset(int delta);
    void cycle_display_mode(int delta);
    void handle_host_command(czx::input::HostCommand cmd);
    void handle_menu_key(const czx_key_event_t& ev);

    static void cb_video_frame(void* user_data, const czx_frame_t* frame);
    static void cb_audio_samples(void* user_data, const int16_t* stereo_s16, size_t frame_count);
    static void cb_log(void* user_data, int level, const char* msg);
    static void cb_host_command(void* user_data, czx::input::HostCommand cmd);
    static void cb_pan(void* user_data, int dx, int dy);
    static void cb_menu_key(void* user_data, const czx_key_event_t& ev);
    static void cb_guest_key(void* user_data, const czx_key_event_t& ev);
    static void cb_guest_joystick(void* user_data, uint32_t port, uint32_t buttons);
};

}  // namespace czx::app
