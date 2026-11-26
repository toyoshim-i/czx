#pragma once

#include "platform/gles2_presenter.hpp"
#include "platform/platform.hpp"

#include <EGL/egl.h>
#include <aaudio/AAudio.h>
#include <android/native_window.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace czx::platform::android {

class AndroidBridge {
public:
    static AndroidBridge& instance();

    void set_window(ANativeWindow* win);
    void on_surface_resized(int width, int height);
    void clear_window();

    void set_paused(bool paused);
    bool is_paused() const noexcept;

    void push_key_event(const czx::input::RawKeyEvent& ev);
    void set_gamepad_buttons(uint32_t port, uint32_t mask);
    void push_mouse_event(const RawMouseEvent& ev);

    void poll_keys(std::vector<czx::input::RawKeyEvent>& out);
    void poll_gamepads(std::vector<GamepadState>& out);
    void poll_mouse(std::vector<RawMouseEvent>& out);

    // Display rendering helpers invoked on the emulator thread
    bool ensure_egl_surface();
    void process_surface_destroy_if_needed();
    void release_egl();

    void present_lcd(const uint16_t* rgb565);
    void present_native(const NativeFrameView& view);
    void present_hw(const NativeHwFrameView& view);

    bool hw_make_current();
    void* hw_get_proc_address(const char* sym) const;
    bool hw_ensure_fbo(int width, int height, bool depth, bool stencil);
    void hw_release_fbo();
    uintptr_t hw_get_current_framebuffer() const;

    bool init_audio(uint32_t sample_rate);
    void write_audio(const int16_t* stereo_s16, size_t frame_count);
    double audio_buffer_fill_ratio() const noexcept;
    void shutdown_audio();

    static aaudio_data_callback_result_t on_audio_data(AAudioStream* stream,
                                                       void* user_data,
                                                       void* audio_data,
                                                       int32_t num_frames);

private:
    AndroidBridge() = default;
    ~AndroidBridge();

    mutable std::mutex surface_mtx_;
    std::condition_variable surface_cv_;
    ANativeWindow* window_ = nullptr;
    int surface_w_ = 0;
    int surface_h_ = 0;
    bool window_changed_ = false;
    bool destroy_requested_ = false;
    std::atomic<bool> paused_{false};

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;
    EGLConfig egl_config_ = nullptr;
    EGLContext egl_context_ = EGL_NO_CONTEXT;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    Gles2Presenter presenter_;
    bool presenter_ready_ = false;

    std::mutex input_mtx_;
    std::vector<czx::input::RawKeyEvent> pending_keys_;
    std::vector<RawMouseEvent> pending_mouse_;
    std::array<uint32_t, 2> gamepad_masks_{0u, 0u};
    std::array<uint32_t, 2> gamepad_latched_press_{0u, 0u};

    static constexpr size_t kAudioRingFrames = 8192;
    std::mutex audio_mtx_;
    AAudioStream* audio_stream_ = nullptr;
    uint32_t audio_sample_rate_ = 44100;
    std::vector<int16_t> audio_ring_ = std::vector<int16_t>(kAudioRingFrames * 2u, 0);
    std::atomic<uint64_t> audio_write_pos_{0};
    std::atomic<uint64_t> audio_read_pos_{0};
};

}  // namespace czx::platform::android
