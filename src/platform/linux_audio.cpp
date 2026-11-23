#include "platform/linux_audio.hpp"

#include <algorithm>
#include <cmath>
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
constexpr int kPaSeekRelative = 0;
constexpr int kPaOperationRunning = 0;
constexpr int kPaOperationDone = 1;
constexpr uint32_t kPaVolumeNorm = 0x10000u;
constexpr size_t kPaChannelsMax = 32u;

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

struct LinuxPulseAudio::PaChannelMap {
    uint8_t channels;
    int map[kPaChannelsMax];
};

struct LinuxPulseAudio::PaCVolume {
    uint8_t channels;
    uint32_t values[kPaChannelsMax];
};

struct LinuxPulseAudio::PaServerInfo {
    const char* user_name;
    const char* host_name;
    const char* server_version;
    const char* server_name;
    PaSampleSpec sample_spec;
    const char* default_sink_name;
    const char* default_source_name;
    uint32_t cookie;
    PaChannelMap channel_map;
};

struct LinuxPulseAudio::PaSinkInfo {
    const char* name;
    uint32_t index;
    const char* description;
    PaSampleSpec sample_spec;
    PaChannelMap channel_map;
    uint32_t owner_module;
    PaCVolume volume;
    int mute;
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
    buffer_tlength_ = 0;
    target_sink_.clear();

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
    fn_ctx_get_server_info_ = nullptr;
    fn_ctx_get_sink_info_by_name_ = nullptr;
    fn_ctx_set_sink_volume_by_name_ = nullptr;
    fn_ctx_set_sink_mute_by_name_ = nullptr;
    fn_op_get_state_ = nullptr;
    fn_op_unref_ = nullptr;
    fn_cvolume_set_ = nullptr;
    fn_cvolume_avg_ = nullptr;
}

bool LinuxPulseAudio::init(uint32_t sample_rate) {
    return init_with_sink(sample_rate, nullptr);
}

bool LinuxPulseAudio::init_with_sink(uint32_t sample_rate, const char* sink_dev) {
    if (active_ && sample_rate_ == sample_rate) {
        return true;
    }
    cleanup();
    if (sink_dev && sink_dev[0] != '\0') {
        target_sink_ = sink_dev;
    }

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
    resolve_sym(dl_, "pa_context_get_server_info", fn_ctx_get_server_info_);
    resolve_sym(dl_, "pa_context_get_sink_info_by_name", fn_ctx_get_sink_info_by_name_);
    resolve_sym(dl_, "pa_context_set_sink_volume_by_name", fn_ctx_set_sink_volume_by_name_);
    resolve_sym(dl_, "pa_context_set_sink_mute_by_name", fn_ctx_set_sink_mute_by_name_);
    resolve_sym(dl_, "pa_operation_get_state", fn_op_get_state_);
    resolve_sym(dl_, "pa_operation_unref", fn_op_unref_);
    resolve_sym(dl_, "pa_cvolume_set", fn_cvolume_set_);
    resolve_sym(dl_, "pa_cvolume_avg", fn_cvolume_avg_);

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
    const uint32_t tlength = std::max<uint32_t>(4u, (bytes_per_sec * 120u / 1000u) & ~3u);
    const uint32_t prebuf = std::max<uint32_t>(4u, (bytes_per_sec * 60u / 1000u) & ~3u);
    const uint32_t minreq = std::max<uint32_t>(4u, (bytes_per_sec * 20u / 1000u) & ~3u);
    const PaBufferAttr attr{tlength * 2u, tlength, prebuf, minreq, UINT32_MAX};

    if (fn_stream_connect_playback_(stream_, sink_dev, &attr, 0, nullptr, nullptr) < 0) {
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
    buffer_tlength_ = tlength;
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

double LinuxPulseAudio::get_buffer_fill_ratio() const {
    if (!active_ || !ml_ || !stream_ || buffer_tlength_ == 0u || !fn_stream_writable_size_ ||
        !fn_stream_get_state_ || !fn_ml_lock_ || !fn_ml_unlock_) {
        return -1.0;
    }
    fn_ml_lock_(ml_);
    double ratio = -1.0;
    if (fn_stream_get_state_(stream_) == kPaStreamReady) {
        const size_t avail = fn_stream_writable_size_(stream_);
        if (avail != static_cast<size_t>(-1)) {
            const size_t tlen = static_cast<size_t>(buffer_tlength_);
            const double queued = (avail >= tlen) ? 0.0 : static_cast<double>(tlen - avail);
            ratio = std::clamp(queued / static_cast<double>(tlen), 0.0, 1.0);
        }
    }
    fn_ml_unlock_(ml_);
    return ratio;
}

bool LinuxPulseAudio::wait_operation_locked(PaOperation* op) {
    if (!op || !fn_op_get_state_ || !fn_op_unref_) return false;
    while (fn_op_get_state_(op) == kPaOperationRunning) {
        if (!fn_ctx_get_state_ || fn_ctx_get_state_(ctx_) != kPaContextReady) {
            fn_op_unref_(op);
            return false;
        }
        fn_ml_wait_(ml_);
    }
    const int st = fn_op_get_state_(op);
    fn_op_unref_(op);
    return st == kPaOperationDone;
}

bool LinuxPulseAudio::query_sink_locked(std::string& sink_name, PaCVolume& vol, bool& muted) {
    if (!active_ || !ml_ || !ctx_ || fn_ctx_get_state_(ctx_) != kPaContextReady) return false;
    if (!fn_ctx_get_sink_info_by_name_ || !fn_op_get_state_ || !fn_op_unref_ || !fn_cvolume_avg_) {
        return false;
    }

    sink_name = target_sink_;
    if (sink_name.empty() && fn_ctx_get_server_info_) {
        struct ServerQuery {
            PaThreadedMainloop* ml = nullptr;
            FnMlSignal signal = nullptr;
            std::string default_sink;
        } sq{ml_, fn_ml_signal_, {}};

        auto on_server = [](PaContext*, const PaServerInfo* info, void* userdata) {
            auto* q = static_cast<ServerQuery*>(userdata);
            if (info && info->default_sink_name && info->default_sink_name[0] != '\0') {
                q->default_sink = info->default_sink_name;
            }
            if (q->ml && q->signal) q->signal(q->ml, 0);
        };
        PaOperation* op = fn_ctx_get_server_info_(ctx_, on_server, &sq);
        (void)wait_operation_locked(op);
        sink_name = std::move(sq.default_sink);
    }
    if (sink_name.empty()) {
        sink_name = "@DEFAULT_SINK@";
    }

    struct SinkQuery {
        PaThreadedMainloop* ml = nullptr;
        FnMlSignal signal = nullptr;
        std::string resolved_name;
        PaCVolume vol{};
        bool muted = false;
        bool ok = false;
    } sink_q{ml_, fn_ml_signal_, {}, {}, false, false};

    auto on_sink = [](PaContext*, const PaSinkInfo* info, int eol, void* userdata) {
        auto* q = static_cast<SinkQuery*>(userdata);
        if (!eol && info) {
            if (info->name && info->name[0] != '\0') {
                q->resolved_name = info->name;
            }
            q->vol = info->volume;
            q->muted = (info->mute != 0);
            q->ok = true;
        }
        if (q->ml && q->signal) q->signal(q->ml, 0);
    };

    PaOperation* op = fn_ctx_get_sink_info_by_name_(ctx_, sink_name.c_str(), on_sink, &sink_q);
    if (!wait_operation_locked(op) || !sink_q.ok) {
        return false;
    }
    if (!sink_q.resolved_name.empty()) {
        sink_name = std::move(sink_q.resolved_name);
    }
    vol = sink_q.vol;
    muted = sink_q.muted;
    return true;
}

bool LinuxPulseAudio::get_system_volume(SystemVolumeState& out) {
    if (!active_ || !ml_ || !ctx_) return false;
    fn_ml_lock_(ml_);
    std::string sink_name;
    PaCVolume vol{};
    bool muted = false;
    const bool ok = query_sink_locked(sink_name, vol, muted);
    if (ok) {
        const uint32_t avg = fn_cvolume_avg_(&vol);
        out.percent = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(avg) * 100.0 /
                                         static_cast<double>(kPaVolumeNorm))),
            0, 100);
        out.muted = muted;
    }
    fn_ml_unlock_(ml_);
    return ok;
}

bool LinuxPulseAudio::adjust_system_volume(int delta_percent, SystemVolumeState& out) {
    if (!active_ || !ml_ || !ctx_ || !fn_ctx_set_sink_volume_by_name_ || !fn_cvolume_set_) {
        return false;
    }
    fn_ml_lock_(ml_);
    std::string sink_name;
    PaCVolume vol{};
    bool muted = false;
    if (!query_sink_locked(sink_name, vol, muted)) {
        fn_ml_unlock_(ml_);
        return false;
    }

    const uint32_t cur_avg = fn_cvolume_avg_(&vol);
    const int cur_pct = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(cur_avg) * 100.0 /
                                     static_cast<double>(kPaVolumeNorm))),
        0, 100);
    const int next_pct = std::clamp(cur_pct + delta_percent, 0, 100);
    const uint32_t raw_vol = static_cast<uint32_t>(std::clamp(
        static_cast<long>(std::lround(static_cast<double>(next_pct) *
                                      static_cast<double>(kPaVolumeNorm) / 100.0)),
        0L, static_cast<long>(kPaVolumeNorm)));

    PaCVolume next_cvol = vol;
    const unsigned ch =
        (vol.channels > 0 && vol.channels <= kPaChannelsMax) ? vol.channels : 2u;
    fn_cvolume_set_(&next_cvol, ch, raw_vol);

    struct SuccessQuery {
        PaThreadedMainloop* ml = nullptr;
        FnMlSignal signal = nullptr;
        bool ok = false;
    };
    auto on_success = [](PaContext*, int success, void* userdata) {
        auto* q = static_cast<SuccessQuery*>(userdata);
        q->ok = (success != 0);
        if (q->ml && q->signal) q->signal(q->ml, 0);
    };

    if (delta_percent > 0 && muted && fn_ctx_set_sink_mute_by_name_) {
        SuccessQuery mq{ml_, fn_ml_signal_, false};
        PaOperation* mop = fn_ctx_set_sink_mute_by_name_(ctx_, sink_name.c_str(), 0, on_success, &mq);
        if (wait_operation_locked(mop) && mq.ok) {
            muted = false;
        }
    }

    SuccessQuery vq{ml_, fn_ml_signal_, false};
    PaOperation* vop = fn_ctx_set_sink_volume_by_name_(ctx_, sink_name.c_str(), &next_cvol, on_success, &vq);
    const bool ok = wait_operation_locked(vop) && vq.ok;
    if (ok) {
        out.percent = next_pct;
        out.muted = muted;
    }
    fn_ml_unlock_(ml_);
    return ok;
}

bool LinuxPulseAudio::toggle_system_mute(SystemVolumeState& out) {
    if (!active_ || !ml_ || !ctx_ || !fn_ctx_set_sink_mute_by_name_) {
        return false;
    }
    fn_ml_lock_(ml_);
    std::string sink_name;
    PaCVolume vol{};
    bool muted = false;
    if (!query_sink_locked(sink_name, vol, muted)) {
        fn_ml_unlock_(ml_);
        return false;
    }

    const bool next_muted = !muted;
    struct SuccessQuery {
        PaThreadedMainloop* ml = nullptr;
        FnMlSignal signal = nullptr;
        bool ok = false;
    } mq{ml_, fn_ml_signal_, false};
    auto on_success = [](PaContext*, int success, void* userdata) {
        auto* q = static_cast<SuccessQuery*>(userdata);
        q->ok = (success != 0);
        if (q->ml && q->signal) q->signal(q->ml, 0);
    };

    PaOperation* op = fn_ctx_set_sink_mute_by_name_(
        ctx_, sink_name.c_str(), next_muted ? 1 : 0, on_success, &mq);
    const bool ok = wait_operation_locked(op) && mq.ok;
    if (ok) {
        const uint32_t avg = fn_cvolume_avg_(&vol);
        out.percent = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(avg) * 100.0 /
                                         static_cast<double>(kPaVolumeNorm))),
            0, 100);
        out.muted = next_muted;
    }
    fn_ml_unlock_(ml_);
    return ok;
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

double LinuxAudio::get_buffer_fill_ratio() const {
    if (active_ == ActiveBackend::Pulse) {
        return pulse_.get_buffer_fill_ratio();
    }
    if (active_ == ActiveBackend::Alsa) {
        return alsa_.get_buffer_fill_ratio();
    }
    return -1.0;
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

bool LinuxAudio::get_system_volume(SystemVolumeState& out) {
    if (active_ == ActiveBackend::Pulse && pulse_.get_system_volume(out)) {
        return true;
    }
    return alsa_.get_system_volume(out);
}

bool LinuxAudio::adjust_system_volume(int delta_percent, SystemVolumeState& out) {
    if (active_ == ActiveBackend::Pulse && pulse_.adjust_system_volume(delta_percent, out)) {
        return true;
    }
    return alsa_.adjust_system_volume(delta_percent, out);
}

bool LinuxAudio::toggle_system_mute(SystemVolumeState& out) {
    if (active_ == ActiveBackend::Pulse && pulse_.toggle_system_mute(out)) {
        return true;
    }
    return alsa_.toggle_system_mute(out);
}

}  // namespace czx::platform
