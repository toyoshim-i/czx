#include "platform/linux_audio.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

namespace czx::platform {

namespace {

constexpr int kPaSampleS16Le = 3;
constexpr int kPaContextNoAutospawn = 0x0001;
constexpr int kPaContextReady = 4;
constexpr int kPaContextFailed = 5;
constexpr int kPaContextTerminated = 6;
constexpr int kPaStreamReady = 2;
constexpr int kPaStreamFailed = 3;
constexpr int kPaStreamTerminated = 4;
constexpr int kPaStreamAdjustLatency = 0x2000;
constexpr int kPaSeekRelative = 0;

template <typename Fn>
bool resolve_sym(void* dl, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(dlsym(dl, name));
    return out != nullptr;
}

}  // namespace

struct LinuxPulseAudio::PaSampleSpec {
    int format;
    uint32_t rate;
    uint8_t channels;
};

struct LinuxPulseAudio::PaBufferAttr {
    uint32_t maxlength;
    uint32_t tlength;
    uint32_t prebuf;
    uint32_t minreq;
    uint32_t fragsize;
};

void LinuxPulseAudio::on_context_state(PaContext*, void* userdata) {
    auto* self = static_cast<LinuxPulseAudio*>(userdata);
    if (self && self->ml_ && self->fn_ml_signal_) {
        self->fn_ml_signal_(self->ml_, 0);
    }
}

void LinuxPulseAudio::on_stream_state(PaStream*, void* userdata) {
    auto* self = static_cast<LinuxPulseAudio*>(userdata);
    if (self && self->ml_ && self->fn_ml_signal_) {
        self->fn_ml_signal_(self->ml_, 0);
    }
}

LinuxPulseAudio::~LinuxPulseAudio() {
    cleanup();
}

void LinuxPulseAudio::cleanup() {
    if (ml_) {
        if (started_ && fn_ml_lock_) {
            fn_ml_lock_(ml_);
        }
        if (stream_) {
            if (fn_stream_set_state_cb_) fn_stream_set_state_cb_(stream_, nullptr, nullptr);
            if (fn_stream_disconnect_) fn_stream_disconnect_(stream_);
            if (fn_stream_unref_) fn_stream_unref_(stream_);
            stream_ = nullptr;
        }
        if (ctx_) {
            if (fn_ctx_set_state_cb_) fn_ctx_set_state_cb_(ctx_, nullptr, nullptr);
            if (fn_ctx_disconnect_) fn_ctx_disconnect_(ctx_);
            if (fn_ctx_unref_) fn_ctx_unref_(ctx_);
            ctx_ = nullptr;
        }
        if (started_) {
            if (fn_ml_unlock_) fn_ml_unlock_(ml_);
            if (fn_ml_stop_) fn_ml_stop_(ml_);
            started_ = false;
        }
        if (fn_ml_free_) {
            fn_ml_free_(ml_);
        }
        ml_ = nullptr;
    }
    if (dl_) {
        dlclose(dl_);
        dl_ = nullptr;
    }
    active_ = false;
    sample_rate_ = 0;

    fn_ml_new_ = nullptr;
    fn_ml_start_ = nullptr;
    fn_ml_stop_ = nullptr;
    fn_ml_free_ = nullptr;
    fn_ml_lock_ = nullptr;
    fn_ml_unlock_ = nullptr;
    fn_ml_wait_ = nullptr;
    fn_ml_signal_ = nullptr;
    fn_ml_get_api_ = nullptr;
    fn_ctx_new_ = nullptr;
    fn_ctx_set_state_cb_ = nullptr;
    fn_ctx_connect_ = nullptr;
    fn_ctx_disconnect_ = nullptr;
    fn_ctx_unref_ = nullptr;
    fn_ctx_get_state_ = nullptr;
    fn_ctx_errno_ = nullptr;
    fn_stream_new_ = nullptr;
    fn_stream_set_state_cb_ = nullptr;
    fn_stream_connect_playback_ = nullptr;
    fn_stream_disconnect_ = nullptr;
    fn_stream_unref_ = nullptr;
    fn_stream_get_state_ = nullptr;
    fn_stream_writable_size_ = nullptr;
    fn_stream_write_ = nullptr;
    fn_strerror_ = nullptr;
}

bool LinuxPulseAudio::init(uint32_t sample_rate) {
    return init_with_sink(sample_rate, nullptr);
}

bool LinuxPulseAudio::init_with_sink(uint32_t sample_rate, const char* sink_dev) {
    if (active_ && sample_rate_ == sample_rate) {
        return true;
    }
    cleanup();

    dl_ = dlopen("libpulse.so.0", RTLD_LAZY);
    if (!dl_) {
        return false;
    }

    const bool syms_ok =
        resolve_sym(dl_, "pa_threaded_mainloop_new", fn_ml_new_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_start", fn_ml_start_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_stop", fn_ml_stop_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_free", fn_ml_free_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_lock", fn_ml_lock_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_unlock", fn_ml_unlock_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_wait", fn_ml_wait_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_signal", fn_ml_signal_) &&
        resolve_sym(dl_, "pa_threaded_mainloop_get_api", fn_ml_get_api_) &&
        resolve_sym(dl_, "pa_context_new", fn_ctx_new_) &&
        resolve_sym(dl_, "pa_context_set_state_callback", fn_ctx_set_state_cb_) &&
        resolve_sym(dl_, "pa_context_connect", fn_ctx_connect_) &&
        resolve_sym(dl_, "pa_context_disconnect", fn_ctx_disconnect_) &&
        resolve_sym(dl_, "pa_context_unref", fn_ctx_unref_) &&
        resolve_sym(dl_, "pa_context_get_state", fn_ctx_get_state_) &&
        resolve_sym(dl_, "pa_context_errno", fn_ctx_errno_) &&
        resolve_sym(dl_, "pa_stream_new", fn_stream_new_) &&
        resolve_sym(dl_, "pa_stream_set_state_callback", fn_stream_set_state_cb_) &&
        resolve_sym(dl_, "pa_stream_connect_playback", fn_stream_connect_playback_) &&
        resolve_sym(dl_, "pa_stream_disconnect", fn_stream_disconnect_) &&
        resolve_sym(dl_, "pa_stream_unref", fn_stream_unref_) &&
        resolve_sym(dl_, "pa_stream_get_state", fn_stream_get_state_) &&
        resolve_sym(dl_, "pa_stream_writable_size", fn_stream_writable_size_) &&
        resolve_sym(dl_, "pa_stream_write", fn_stream_write_);
    resolve_sym(dl_, "pa_strerror", fn_strerror_);

    if (!syms_ok) {
        std::fprintf(stderr, "czx: PulseAudio symbol resolution failed in libpulse.so.0\n");
        cleanup();
        return false;
    }

    ml_ = fn_ml_new_();
    PaMainloopApi* api = ml_ ? fn_ml_get_api_(ml_) : nullptr;
    ctx_ = api ? fn_ctx_new_(api, "czx") : nullptr;
    if (!ctx_) {
        cleanup();
        return false;
    }

    fn_ctx_set_state_cb_(ctx_, on_context_state, this);
    if (fn_ml_start_(ml_) < 0) {
        cleanup();
        return false;
    }
    started_ = true;

    fn_ml_lock_(ml_);
    if (fn_ctx_connect_(ctx_, nullptr, kPaContextNoAutospawn, nullptr) < 0) {
        const int err = fn_ctx_errno_(ctx_);
        std::fprintf(stderr, "czx: PulseAudio connect failed: %s\n",
                     (fn_strerror_ && err) ? fn_strerror_(err) : "connection refused");
        fn_ml_unlock_(ml_);
        cleanup();
        return false;
    }

    for (;;) {
        const int st = fn_ctx_get_state_(ctx_);
        if (st == kPaContextReady) break;
        if (st == kPaContextFailed || st == kPaContextTerminated) {
            const int err = fn_ctx_errno_(ctx_);
            std::fprintf(stderr, "czx: PulseAudio context failed: %s\n",
                         (fn_strerror_ && err) ? fn_strerror_(err) : "connection refused");
            fn_ml_unlock_(ml_);
            cleanup();
            return false;
        }
        fn_ml_wait_(ml_);
    }

    const PaSampleSpec ss{kPaSampleS16Le, sample_rate, 2};
    stream_ = fn_stream_new_(ctx_, "Audio", &ss, nullptr);
    if (!stream_) {
        fn_ml_unlock_(ml_);
        cleanup();
        return false;
    }

    fn_stream_set_state_cb_(stream_, on_stream_state, this);

    const uint32_t bytes_per_sec = sample_rate * 4u;
    const uint32_t tlength = std::max<uint32_t>(4u, (bytes_per_sec * 60u / 1000u) & ~3u);
    const uint32_t prebuf = std::max<uint32_t>(4u, (bytes_per_sec * 20u / 1000u) & ~3u);
    const PaBufferAttr attr{tlength * 2u, tlength, prebuf, UINT32_MAX, UINT32_MAX};

    if (fn_stream_connect_playback_(stream_, sink_dev, &attr, kPaStreamAdjustLatency, nullptr, nullptr) < 0) {
        const int err = fn_ctx_errno_(ctx_);
        std::fprintf(stderr, "czx: PulseAudio stream connect failed: %s\n",
                     (fn_strerror_ && err) ? fn_strerror_(err) : "stream connect error");
        fn_ml_unlock_(ml_);
        cleanup();
        return false;
    }

    for (;;) {
        const int st = fn_stream_get_state_(stream_);
        if (st == kPaStreamReady) break;
        if (st == kPaStreamFailed || st == kPaStreamTerminated) {
            const int err = fn_ctx_errno_(ctx_);
            std::fprintf(stderr, "czx: PulseAudio stream failed: %s\n",
                         (fn_strerror_ && err) ? fn_strerror_(err) : "stream error");
            fn_ml_unlock_(ml_);
            cleanup();
            return false;
        }
        fn_ml_wait_(ml_);
    }

    active_ = true;
    sample_rate_ = sample_rate;
    fn_ml_unlock_(ml_);

    std::fprintf(stderr, "czx: PulseAudio/PipeWire audio active (%u Hz)\n", sample_rate);
    return true;
}

void LinuxPulseAudio::write_samples(const int16_t* stereo_s16, size_t frame_count) {
    if (!active_ || !ml_ || !stream_ || !stereo_s16 || frame_count == 0) return;

    fn_ml_lock_(ml_);
    if (fn_stream_get_state_(stream_) == kPaStreamReady) {
        const size_t avail = fn_stream_writable_size_(stream_);
        if (avail != static_cast<size_t>(-1) && avail >= 4u) {
            const size_t bytes = std::min(frame_count * 4u, avail) & ~static_cast<size_t>(3u);
            if (bytes > 0) {
                fn_stream_write_(stream_, stereo_s16, bytes, nullptr, 0, kPaSeekRelative);
            }
        }
    }
    fn_ml_unlock_(ml_);
}

bool LinuxAudio::init(uint32_t sample_rate) {
    const char* env_dev = std::getenv("CZX_AUDIO_DEVICE");
    if (env_dev && env_dev[0] != '\0') {
        if (std::strncmp(env_dev, "pulse:", 6) == 0) {
            alsa_.cleanup();
            const char* sink = env_dev[6] ? env_dev + 6 : nullptr;
            if (pulse_.init_with_sink(sample_rate, sink)) {
                active_ = ActiveBackend::Pulse;
                return true;
            }
            active_ = ActiveBackend::Inactive;
            return false;
        }
        pulse_.cleanup();
        if (alsa_.init(sample_rate)) {
            active_ = ActiveBackend::Alsa;
            return true;
        }
        active_ = ActiveBackend::Inactive;
        return false;
    }

    if (pulse_.init(sample_rate)) {
        alsa_.cleanup();
        active_ = ActiveBackend::Pulse;
        return true;
    }

    pulse_.cleanup();
    if (alsa_.init(sample_rate)) {
        active_ = ActiveBackend::Alsa;
        return true;
    }

    active_ = ActiveBackend::Inactive;
    return false;
}

void LinuxAudio::write_samples(const int16_t* stereo_s16, size_t frame_count) {
    if (active_ == ActiveBackend::Pulse) {
        pulse_.write_samples(stereo_s16, frame_count);
    } else if (active_ == ActiveBackend::Alsa) {
        alsa_.write_samples(stereo_s16, frame_count);
    }
}

const char* LinuxAudio::backend_name() const {
    switch (active_) {
        case ActiveBackend::Pulse:
            return pulse_.backend_name();
        case ActiveBackend::Alsa:
            return alsa_.backend_name();
        case ActiveBackend::Inactive:
            break;
    }
    return "null-audio";
}

}  // namespace czx::platform
