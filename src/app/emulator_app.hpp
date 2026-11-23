#pragma once

#include "app/launcher_state.hpp"
#include "core/core_registry.hpp"
#include "input/input_router.hpp"
#include "platform/platform.hpp"
#include "video/file_browser.hpp"
#include "video/hud.hpp"
#include "video/launcher.hpp"
#include "video/menu.hpp"
#include "video/scaler.hpp"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace czx::app {

struct AppConfig {
    std::string core_spec;  // Empty = auto-detect from media, else default core
    std::string media_path;
    std::string media2_path;
    std::string core_dir;
    std::string data_dir;
    uint32_t preset_index = 0;
    czx::video::DisplayMode display_mode = czx::video::DisplayMode::OverflowFit;
    bool display_mode_explicit = false;
    bool force_desktop = false;
    bool force_lcd_sim = false;
    int desktop_scale = 2;
    czx::video::AspectPolicy aspect_policy = czx::video::AspectPolicy::ParAware;
    bool show_borders = false;
    czx::video::MarginFill margin_fill = czx::video::MarginFill::Auto;
    bool show_activity_indicator = true;
    bool activity_indicator_override = false;
    bool tape_warp_enabled = true;
    bool tape_warp_override = false;
    bool vsync_enabled = true;
    bool vsync_override = false;
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
    std::vector<MachineTarget> targets_;
    LauncherState launcher_state_;
    std::string launcher_state_path_;
    czx::video::LauncherView launcher_;
    bool in_launcher_ = false;
    bool menu_from_launcher_ = false;
    int launcher_index_ = 0;
    int active_target_index_ = -1;
    int pending_target_index_ = -1;  // -1 = "Launcher" when in-game
    int active_variant_index_ = 0;
    int pending_variant_index_ = 0;
    bool default_desktop_native_ = false;
    int default_desktop_scale_ = 2;
    std::string active_media1_;
    std::string active_media2_;
    std::string active_media1_label_;
    std::string active_media2_label_;
    int core_index_ = -1;
    int pending_core_index_ = -1;
    uint64_t core_epoch_ = 0;
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
    uint32_t pending_drive_ = 0;
    struct DriveD88State {
        std::string source_path;
        int disk_index = -1;
    };
    std::array<DriveD88State, 2> drive_d88_{};
    std::string core_error_;
    std::map<std::string, std::string> last_media_dirs_;

    std::mutex scale_mtx_;
    std::condition_variable scale_cv_;
    std::thread scale_thread_;
    bool scale_thread_running_ = false;
    bool scale_stop_ = false;
    std::vector<uint8_t> stage_raw_;
    std::vector<uint8_t> pending_raw_;
    czx_frame_t pending_frame_{};
    czx::video::ScalerConfig pending_cfg_{};
    bool has_pending_raw_ = false;
    bool has_rescale_request_ = false;
    bool clear_work_frame_ = false;
    czx::video::ScalerConfig rescale_cfg_{};
    czx_frame_t last_frame_meta_{};
    bool has_last_frame_meta_ = false;
    std::vector<uint8_t> desktop_last_raw_bytes_;
    czx_frame_t desktop_last_raw_frame_{};
    bool desktop_has_last_raw_ = false;
    std::vector<uint8_t> work_raw_;
    std::vector<uint16_t> work_fb_;
    std::vector<uint16_t> ready_fb_;
    uint64_t ready_seq_ = 0;
    uint64_t presented_seq_ = 0;
    bool force_present_ = true;
    bool prev_overlay_active_ = false;
    bool desktop_native_mode_ = false;
    int desktop_scale_ = 2;
    int last_win_req_w_ = 0;
    int last_win_req_h_ = 0;
    std::vector<uint32_t> native_xrgb_;
    int native_w_ = 0;
    int native_h_ = 0;
    int native_par_num_ = 1;
    int native_par_den_ = 1;
    uint32_t native_margin_xrgb_ = 0;
    uint64_t native_seq_ = 0;
    uint64_t presented_native_seq_ = 0;
    std::vector<uint16_t> ov_black_;
    std::vector<uint16_t> ov_white_;
    std::vector<uint32_t> overlay_argb_;
    std::string last_window_title_;
    bool show_activity_indicator_ = true;
    bool tape_warp_enabled_ = true;
    bool vsync_enabled_ = true;
    std::string c64_region_ = "auto";
    bool live_warp_requested_ = false;
    uint32_t activity_flags_ = 0;
    int8_t activity_tape_percent_ = -1;
    uint8_t activity_drive_mask_ = 0;
    uint32_t activity_hold_frames_ = 0;

    void start_scale_worker();
    void stop_scale_worker();
    void scale_worker_loop();
    void reset_scaled_frame();
    void build_overlay_argb();
    void update_window_title();

    void ensure_data_dirs();
    void load_app_config();
    void save_app_config() const;
    void sync_core_warp_option();
    void sync_core_region_option();
    std::string resolve_target_region_id(int target_index) const;
    void sync_core_gpu_scale_option();
    void sync_router_menu_open();
    void clear_activity_state() noexcept;
    void poll_core_activity();
    const czx_core_api_t* target_core_api(int target_index, int variant_index) const noexcept;
    int resolve_target_variant_index(int target_index) const noexcept;
    const TargetSavedState* find_target_saved_state(int target_index) const noexcept;
    TargetSavedState& target_saved_state(int target_index);
    void apply_target_prefs(int target_index);
    void capture_current_prefs_to_target(int target_index, bool include_active_media);
    void persist_launcher_state() const;
    std::vector<czx::video::LauncherItemView> build_launcher_items() const;
    bool launch_target(int target_index,
                       bool use_saved_media = true,
                       const std::string& override_m1 = {},
                       const std::string& override_m2 = {},
                       const std::string& override_disp_label = {});
    void enter_launcher(int focus_target_index);
    void handle_launcher_key(const czx_key_event_t& ev);
    const czx_core_api_t* displayed_menu_core_api() const noexcept;
    bool has_pending_core_change() const noexcept;
    std::string default_media_dir_for_core(const czx_core_api_t* api = nullptr) const;
    void present_loading_screen(const char* core_name, const std::string& media_label = {});
    bool activate_core_with_loading(int index,
                                    const std::string& initial_media = {},
                                    const std::string& initial_media2 = {},
                                    const std::string& display_media_label = {});
    void commit_pending_core(bool force_reload = false);
    void update_native_from_frame(const czx_frame_t& frame);
    void refresh_paused_preview();
    bool activate_core(int index, const std::string& initial_media = {}, const std::string& initial_media2 = {});
    void destroy_active_core();
    void render_core_error_screen();
    czx::video::MenuViewModel build_menu_vm() const;
    void open_browser_for_drive(uint32_t drive);
    void eject_drive_slot(uint32_t drive);

    void show_toast(const char* text);
    void set_display_mode(czx::video::DisplayMode mode);
    void set_desktop_scale(int scale);
    void sync_desktop_window_size(bool force = false);
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
    static void cb_guest_mouse(void* user_data, int32_t dx, int32_t dy, uint32_t buttons);
};

}  // namespace czx::app
