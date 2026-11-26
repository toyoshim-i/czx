#include "android_platform.hpp"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "czx_android", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "czx_android", __VA_ARGS__)

namespace czx::platform::android {

AndroidBridge& AndroidBridge::instance() {
    static AndroidBridge inst;
    return inst;
}

AndroidBridge::~AndroidBridge() {
    shutdown_audio();
    release_egl();
    std::lock_guard<std::mutex> lock(surface_mtx_);
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void AndroidBridge::set_window(ANativeWindow* win) {
    if (!win) return;
    ANativeWindow_acquire(win);
    std::lock_guard<std::mutex> lock(surface_mtx_);
    if (window_) {
        ANativeWindow_release(window_);
    }
    window_ = win;
    surface_w_ = ANativeWindow_getWidth(win);
    surface_h_ = ANativeWindow_getHeight(win);
    window_changed_ = true;
    destroy_requested_ = false;
    surface_cv_.notify_all();
}

void AndroidBridge::on_surface_resized(int width, int height) {
    std::lock_guard<std::mutex> lock(surface_mtx_);
    surface_w_ = width;
    surface_h_ = height;
}

void AndroidBridge::clear_window() {
    std::unique_lock<std::mutex> lock(surface_mtx_);
    if (!window_ && egl_surface_ == EGL_NO_SURFACE) {
        return;
    }
    destroy_requested_ = true;
    window_changed_ = true;
    surface_cv_.notify_all();
    surface_cv_.wait_for(lock, std::chrono::milliseconds(500), [&]() {
        return !destroy_requested_ || egl_surface_ == EGL_NO_SURFACE;
    });
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
    destroy_requested_ = false;
}

void AndroidBridge::set_paused(bool paused) {
    paused_.store(paused, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(audio_mtx_);
    if (audio_stream_) {
        if (paused) {
            AAudioStream_requestPause(audio_stream_);
        } else {
            AAudioStream_requestStart(audio_stream_);
        }
    }
}

bool AndroidBridge::is_paused() const noexcept {
    return paused_.load(std::memory_order_relaxed);
}

void AndroidBridge::push_key_event(const czx::input::RawKeyEvent& ev) {
    std::lock_guard<std::mutex> lock(input_mtx_);
    pending_keys_.push_back(ev);
}

void AndroidBridge::set_gamepad_buttons(uint32_t port, uint32_t mask) {
    if (port >= 2u) return;
    std::lock_guard<std::mutex> lock(input_mtx_);
    const uint32_t newly_pressed = mask & ~gamepad_masks_[port];
    gamepad_latched_press_[port] |= newly_pressed;
    gamepad_masks_[port] = mask;
}

void AndroidBridge::push_mouse_event(const RawMouseEvent& ev) {
    std::lock_guard<std::mutex> lock(input_mtx_);
    pending_mouse_.push_back(ev);
}

void AndroidBridge::poll_keys(std::vector<czx::input::RawKeyEvent>& out) {
    std::lock_guard<std::mutex> lock(input_mtx_);
    if (pending_keys_.empty()) return;

    auto same_key = [](const czx::input::RawKeyEvent& a, const czx::input::RawKeyEvent& b) noexcept {
        return a.key == b.key && a.native_key == b.native_key;
    };

    std::vector<czx::input::RawKeyEvent> pressed_in_batch;
    std::vector<czx::input::RawKeyEvent> deferred;
    for (const auto& ev : pending_keys_) {
        const bool already_deferred = std::any_of(
            deferred.begin(), deferred.end(),
            [&](const czx::input::RawKeyEvent& d) { return same_key(d, ev); });
        if (already_deferred) {
            deferred.push_back(ev);
            continue;
        }
        const bool was_pressed_now = std::any_of(
            pressed_in_batch.begin(), pressed_in_batch.end(),
            [&](const czx::input::RawKeyEvent& p) { return same_key(p, ev); });
        if (!ev.pressed && was_pressed_now) {
            deferred.push_back(ev);
            continue;
        }
        if (ev.pressed) {
            pressed_in_batch.push_back(ev);
        }
        out.push_back(ev);
    }
    pending_keys_ = std::move(deferred);
}

void AndroidBridge::poll_gamepads(std::vector<GamepadState>& out) {
    std::lock_guard<std::mutex> lock(input_mtx_);
    for (uint32_t p = 0; p < 2u; ++p) {
        const uint32_t mask = gamepad_masks_[p] | gamepad_latched_press_[p];
        gamepad_latched_press_[p] = 0u;
        out.push_back(GamepadState{p, mask});
    }
}

void AndroidBridge::poll_mouse(std::vector<RawMouseEvent>& out) {
    std::lock_guard<std::mutex> lock(input_mtx_);
    if (!pending_mouse_.empty()) {
        out.insert(out.end(), pending_mouse_.begin(), pending_mouse_.end());
        pending_mouse_.clear();
    }
}

void AndroidBridge::process_surface_destroy_if_needed() {
    std::lock_guard<std::mutex> lock(surface_mtx_);
    if (!destroy_requested_) return;

    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }
    }
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
    destroy_requested_ = false;
    window_changed_ = false;
    surface_cv_.notify_all();
}

bool AndroidBridge::ensure_egl_surface() {
    std::lock_guard<std::mutex> lock(surface_mtx_);
    if (destroy_requested_) {
        if (egl_display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (egl_surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display_, egl_surface_);
                egl_surface_ = EGL_NO_SURFACE;
            }
        }
        if (window_) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }
        destroy_requested_ = false;
        window_changed_ = false;
        surface_cv_.notify_all();
        return false;
    }

    if (!window_) {
        return false;
    }

    if (egl_display_ == EGL_NO_DISPLAY) {
        egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl_display_ == EGL_NO_DISPLAY || !eglInitialize(egl_display_, nullptr, nullptr)) {
            LOGE("eglGetDisplay/eglInitialize failed");
            egl_display_ = EGL_NO_DISPLAY;
            return false;
        }

        const EGLint cfg_attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLint num_cfg = 0;
        if (!eglChooseConfig(egl_display_, cfg_attrs, &egl_config_, 1, &num_cfg) || num_cfg < 1) {
            LOGE("eglChooseConfig failed");
            return false;
        }
    }

    if (egl_context_ == EGL_NO_CONTEXT) {
        const EGLint ctx_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE,
        };
        egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, ctx_attrs);
        if (egl_context_ == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext failed");
            return false;
        }
    }

    if (window_changed_ || egl_surface_ == EGL_NO_SURFACE) {
        if (egl_surface_ != EGL_NO_SURFACE) {
            eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }
        egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_, window_, nullptr);
        if (egl_surface_ == EGL_NO_SURFACE) {
            LOGE("eglCreateWindowSurface failed");
            return false;
        }
        window_changed_ = false;
    }

    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    eglSwapInterval(egl_display_, 1);

    if (!presenter_ready_) {
        presenter_ready_ = presenter_.init(
            reinterpret_cast<Gles2Presenter::GlGetProcAddressFn>(eglGetProcAddress), false);
        if (!presenter_ready_) {
            LOGE("Gles2Presenter::init failed");
            return false;
        }
    }
    return true;
}

void AndroidBridge::release_egl() {
    std::lock_guard<std::mutex> lock(surface_mtx_);
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (presenter_ready_) {
            presenter_.cleanup();
            presenter_ready_ = false;
        }
        if (egl_surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }
        if (egl_context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display_, egl_context_);
            egl_context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }
}

void AndroidBridge::present_lcd(const uint16_t* rgb565) {
    if (!rgb565 || !ensure_egl_surface()) return;
    EGLint w = 0;
    EGLint h = 0;
    eglQuerySurface(egl_display_, egl_surface_, EGL_WIDTH, &w);
    eglQuerySurface(egl_display_, egl_surface_, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    presenter_.present_lcd(w, h, rgb565);
    eglSwapBuffers(egl_display_, egl_surface_);
}

void AndroidBridge::present_native(const NativeFrameView& view) {
    if (!ensure_egl_surface()) return;
    EGLint w = 0;
    EGLint h = 0;
    eglQuerySurface(egl_display_, egl_surface_, EGL_WIDTH, &w);
    eglQuerySurface(egl_display_, egl_surface_, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    presenter_.present_native(w, h, view);
    eglSwapBuffers(egl_display_, egl_surface_);
}

void AndroidBridge::present_hw(const NativeHwFrameView& view) {
    if (!ensure_egl_surface()) return;
    EGLint w = 0;
    EGLint h = 0;
    eglQuerySurface(egl_display_, egl_surface_, EGL_WIDTH, &w);
    eglQuerySurface(egl_display_, egl_surface_, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    presenter_.present_hw(w, h, view);
    eglSwapBuffers(egl_display_, egl_surface_);
}

bool AndroidBridge::hw_make_current() {
    return ensure_egl_surface();
}

void* AndroidBridge::hw_get_proc_address(const char* sym) const {
    return presenter_.get_proc_address(sym);
}

bool AndroidBridge::hw_ensure_fbo(int width, int height, bool depth, bool stencil) {
    if (!ensure_egl_surface()) return false;
    return presenter_.hw_ensure_fbo(width, height, depth, stencil);
}

void AndroidBridge::hw_release_fbo() {
    presenter_.release_hw_fbo();
}

uintptr_t AndroidBridge::hw_get_current_framebuffer() const {
    return presenter_.hw_fbo();
}

aaudio_data_callback_result_t AndroidBridge::on_audio_data(AAudioStream*,
                                                           void* user_data,
                                                           void* audio_data,
                                                           int32_t num_frames) {
    auto* self = static_cast<AndroidBridge*>(user_data);
    auto* out = static_cast<int16_t*>(audio_data);
    const size_t want = static_cast<size_t>(std::max<int32_t>(0, num_frames));
    const uint64_t rpos = self->audio_read_pos_.load(std::memory_order_relaxed);
    const uint64_t wpos = self->audio_write_pos_.load(std::memory_order_acquire);
    const size_t avail = static_cast<size_t>(wpos >= rpos ? (wpos - rpos) : 0u);
    const size_t take = std::min(want, avail);

    for (size_t i = 0; i < take; ++i) {
        const size_t idx = static_cast<size_t>((rpos + i) % kAudioRingFrames) * 2u;
        out[i * 2u + 0u] = self->audio_ring_[idx + 0u];
        out[i * 2u + 1u] = self->audio_ring_[idx + 1u];
    }
    if (take < want) {
        std::memset(out + take * 2u, 0, (want - take) * 2u * sizeof(int16_t));
    }
    self->audio_read_pos_.store(rpos + take, std::memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool AndroidBridge::init_audio(uint32_t sample_rate) {
    shutdown_audio();
    audio_sample_rate_ = (sample_rate > 0u) ? sample_rate : 44100u;
    audio_write_pos_.store(0, std::memory_order_relaxed);
    audio_read_pos_.store(0, std::memory_order_relaxed);
    std::fill(audio_ring_.begin(), audio_ring_.end(), 0);

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
        LOGE("AAudio_createStreamBuilder failed");
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(audio_sample_rate_));
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, on_audio_data, this);

    AAudioStream* stream = nullptr;
    const aaudio_result_t open_res = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (open_res != AAUDIO_OK || !stream) {
        LOGE("AAudioStreamBuilder_openStream failed: %d", static_cast<int>(open_res));
        return false;
    }

    const aaudio_result_t start_res = AAudioStream_requestStart(stream);
    if (start_res != AAUDIO_OK) {
        LOGE("AAudioStream_requestStart failed: %d", static_cast<int>(start_res));
        AAudioStream_close(stream);
        return false;
    }

    std::lock_guard<std::mutex> lock(audio_mtx_);
    audio_stream_ = stream;
    LOGI("AAudio initialized at %u Hz", audio_sample_rate_);
    return true;
}

void AndroidBridge::write_audio(const int16_t* stereo_s16, size_t frame_count) {
    if (!stereo_s16 || frame_count == 0u || paused_.load(std::memory_order_relaxed)) {
        return;
    }
    const uint64_t rpos = audio_read_pos_.load(std::memory_order_acquire);
    uint64_t wpos = audio_write_pos_.load(std::memory_order_relaxed);
    const size_t used = static_cast<size_t>(wpos >= rpos ? (wpos - rpos) : 0u);
    const size_t free_frames = (used < kAudioRingFrames) ? (kAudioRingFrames - used) : 0u;
    const size_t put = std::min(frame_count, free_frames);

    for (size_t i = 0; i < put; ++i) {
        const size_t idx = static_cast<size_t>((wpos + i) % kAudioRingFrames) * 2u;
        audio_ring_[idx + 0u] = stereo_s16[i * 2u + 0u];
        audio_ring_[idx + 1u] = stereo_s16[i * 2u + 1u];
    }
    audio_write_pos_.store(wpos + put, std::memory_order_release);
}

double AndroidBridge::audio_buffer_fill_ratio() const noexcept {
    const uint64_t rpos = audio_read_pos_.load(std::memory_order_acquire);
    const uint64_t wpos = audio_write_pos_.load(std::memory_order_acquire);
    const size_t used = static_cast<size_t>(wpos >= rpos ? (wpos - rpos) : 0u);
    return static_cast<double>(used) / static_cast<double>(kAudioRingFrames);
}

void AndroidBridge::shutdown_audio() {
    AAudioStream* stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(audio_mtx_);
        stream = audio_stream_;
        audio_stream_ = nullptr;
    }
    if (stream) {
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
    }
}

namespace {

class AndroidDisplay final : public PlatformDisplay {
public:
    bool init() override { return true; }

    void present(const uint16_t* rgb565) override {
        AndroidBridge::instance().present_lcd(rgb565);
    }

    bool supports_native_present() const override { return true; }

    void present_native(const NativeFrameView& view) override {
        AndroidBridge::instance().present_native(view);
    }

    bool is_fullscreen() const override { return true; }

    double physical_display_dar() const override { return 0.0; }

    bool is_paused() const override {
        auto& bridge = AndroidBridge::instance();
        bridge.process_surface_destroy_if_needed();
        return bridge.is_paused();
    }

    bool supports_hw_gles2() const override { return true; }
    bool hw_make_current() override { return AndroidBridge::instance().hw_make_current(); }
    void* hw_get_proc_address(const char* sym) const override {
        return AndroidBridge::instance().hw_get_proc_address(sym);
    }
    bool hw_ensure_fbo(int width, int height, bool depth, bool stencil) override {
        return AndroidBridge::instance().hw_ensure_fbo(width, height, depth, stencil);
    }
    void hw_release_fbo() override { AndroidBridge::instance().hw_release_fbo(); }
    uintptr_t hw_get_current_framebuffer() const override {
        return AndroidBridge::instance().hw_get_current_framebuffer();
    }
    void present_hw(const NativeHwFrameView& view) override {
        AndroidBridge::instance().present_hw(view);
    }

    const char* backend_name() const override { return "android_gles2"; }
};

class AndroidInput final : public PlatformInput {
public:
    bool init() override { return true; }
    void poll(std::vector<czx::input::RawKeyEvent>& out_events) override {
        AndroidBridge::instance().poll_keys(out_events);
    }
    void poll_mouse(std::vector<RawMouseEvent>& out_mouse) override {
        AndroidBridge::instance().poll_mouse(out_mouse);
    }
    void poll_gamepads(std::vector<GamepadState>& out_pads) override {
        AndroidBridge::instance().poll_gamepads(out_pads);
    }
    const char* backend_name() const override { return "android_touch"; }
};

class AndroidAudio final : public PlatformAudio {
public:
    ~AndroidAudio() override { AndroidBridge::instance().shutdown_audio(); }
    bool init(uint32_t sample_rate) override {
        return AndroidBridge::instance().init_audio(sample_rate);
    }
    void write_samples(const int16_t* stereo_s16, size_t frame_count) override {
        AndroidBridge::instance().write_audio(stereo_s16, frame_count);
    }
    double get_buffer_fill_ratio() const override {
        return AndroidBridge::instance().audio_buffer_fill_ratio();
    }
    const char* backend_name() const override { return "android_aaudio"; }
};

}  // namespace

}  // namespace czx::platform::android

namespace czx::platform {

PlatformBundle create_platform_bundle(const PlatformOptions&) {
    PlatformBundle bundle;
    bundle.display = std::make_unique<android::AndroidDisplay>();
    bundle.input = std::make_unique<android::AndroidInput>();
    bundle.audio = std::make_unique<android::AndroidAudio>();
    return bundle;
}

}  // namespace czx::platform
