#include "platform/linux_audio.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <thread>
#include <vector>

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
constexpr int kPaChannelPosFrontLeft = 1;
constexpr int kPaChannelPosFrontRight = 2;

template <typename Fn>
bool resolve_sym(void* dl, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(dlsym(dl, name));
    return out != nullptr;
}

bool contains_icase(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    const size_t nlen = std::strlen(needle);
    if (nlen == 0) return true;
    const size_t hlen = std::strlen(haystack);
    if (hlen < nlen) return false;
    for (size_t i = 0; i + nlen <= hlen; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
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

struct LinuxPulseAudio::PaCardProfileInfo {
    const char* name;
    const char* description;
    uint32_t n_sinks;
    uint32_t n_sources;
    uint32_t priority;
};

struct LinuxPulseAudio::PaCardProfileInfo2 {
    const char* name;
    const char* description;
    uint32_t n_sinks;
    uint32_t n_sources;
    uint32_t priority;
    int available;
};

struct LinuxPulseAudio::PaCardInfo {
    uint32_t index;
    const char* name;
    uint32_t owner_module;
    const char* driver;
    uint32_t n_profiles;
    PaCardProfileInfo* profiles;
    PaCardProfileInfo* active_profile;
    void* proplist;
    uint32_t n_ports;
    void** ports;
    PaCardProfileInfo2** profiles2;
    PaCardProfileInfo2* active_profile2;
};

struct LinuxPulseAudio::PaBufferAttr {
    uint32_t maxlength;
    uint32_t tlength;
    uint32_t prebuf;
    uint32_t minreq;
    uint32_t fragsize;
};

struct LinuxPulseAudio::SuccessQuery {
    PaThreadedMainloop* ml = nullptr;
    FnMlSignal signal = nullptr;
    bool ok = false;
};

std::string select_best_hdmi_card_profile(const std::vector<PulseCardProfileCandidate>& profiles) {
    std::string best_name;
    int best_tier = -1;
    uint32_t best_prio = 0;
    for (const auto& p : profiles) {
        if (!p.available || p.n_sinks == 0u || p.name.empty() || p.name == "off") {
            continue;
        }
        int tier = 1;
        if (contains_icase(p.name.c_str(), "hdmi-stereo")) {
            tier = 3;
        } else if (contains_icase(p.name.c_str(), "hdmi") ||
                   contains_icase(p.name.c_str(), "stereo")) {
            tier = 2;
        }
        if (tier > best_tier || (tier == best_tier && p.priority > best_prio)) {
            best_tier = tier;
            best_prio = p.priority;
            best_name = p.name;
        }
    }
    return best_name;
}

void LinuxPulseAudio::on_op_success(PaContext*, int success, void* userdata) {
    auto* q = static_cast<SuccessQuery*>(userdata);
    if (!q) return;
    q->ok = (success != 0);
    if (q->ml && q->signal) {
        q->signal(q->ml, 0);
    }
}

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
    restore_hdmi_card_profile();
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
    fn_ctx_get_sink_info_list_ = nullptr;
    fn_ctx_get_sink_info_by_name_ = nullptr;
    fn_ctx_set_sink_volume_by_name_ = nullptr;
    fn_ctx_set_sink_mute_by_name_ = nullptr;
    fn_ctx_get_card_info_list_ = nullptr;
    fn_ctx_set_card_profile_by_name_ = nullptr;
    fn_proplist_gets_ = nullptr;
    fn_op_get_state_ = nullptr;
    fn_op_unref_ = nullptr;
    fn_cvolume_set_ = nullptr;
    fn_cvolume_avg_ = nullptr;
}

bool LinuxPulseAudio::init(uint32_t sample_rate) {
    return init_with_sink(sample_rate, nullptr, false);
}

bool LinuxPulseAudio::init_with_sink(uint32_t sample_rate, const char* sink_dev, bool prefer_hdmi_sink) {
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
    resolve_sym(dl_, "pa_context_get_sink_info_list", fn_ctx_get_sink_info_list_);
    resolve_sym(dl_, "pa_context_get_sink_info_by_name", fn_ctx_get_sink_info_by_name_);
    resolve_sym(dl_, "pa_context_set_sink_volume_by_name", fn_ctx_set_sink_volume_by_name_);
    resolve_sym(dl_, "pa_context_set_sink_mute_by_name", fn_ctx_set_sink_mute_by_name_);
    resolve_sym(dl_, "pa_context_get_card_info_list", fn_ctx_get_card_info_list_);
    resolve_sym(dl_, "pa_context_set_card_profile_by_name", fn_ctx_set_card_profile_by_name_);
    resolve_sym(dl_, "pa_proplist_gets", fn_proplist_gets_);
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

    PaChannelMap sink_chmap{};
    bool has_sink_chmap = false;
    bool just_activated_hdmi = false;
    if (prefer_hdmi_sink && target_sink_.empty() && fn_ctx_get_sink_info_list_) {
        std::string hdmi_sink;
        std::string seen_sinks;
        PaChannelMap hdmi_chmap{};
        bool hdmi_muted = false;
        (void)find_hdmi_sink_locked(hdmi_sink, seen_sinks, hdmi_chmap, hdmi_muted);
        if (hdmi_sink.empty() && activate_hdmi_card_profile_locked()) {
            just_activated_hdmi = true;
            for (int attempt = 0; attempt < 25 && hdmi_sink.empty(); ++attempt) {
                fn_ml_unlock_(ml_);
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                fn_ml_lock_(ml_);
                if (fn_ctx_get_state_(ctx_) != kPaContextReady) {
                    break;
                }
                (void)find_hdmi_sink_locked(hdmi_sink, seen_sinks, hdmi_chmap, hdmi_muted);
            }
        }
        if (!hdmi_sink.empty()) {
            target_sink_ = std::move(hdmi_sink);
            sink_chmap = hdmi_chmap;
            has_sink_chmap = (hdmi_chmap.channels >= 2);
            if (just_activated_hdmi && hdmi_muted && fn_ctx_set_sink_mute_by_name_) {
                SuccessQuery mq{ml_, fn_ml_signal_, false};
                PaOperation* mop = fn_ctx_set_sink_mute_by_name_(
                    ctx_, target_sink_.c_str(), 0, on_op_success, &mq);
                (void)wait_operation_locked(mop);
            }
        } else if (!seen_sinks.empty()) {
            std::fprintf(stderr, "czx: HDMI audio sink not found (available: %s)\n",
                         seen_sinks.c_str());
        }
    }

    const PaSampleSpec ss{kPaSampleS16Le, sample_rate, 2};
    PaChannelMap custom_map{};
    bool used_custom_chmap = false;
    if (has_sink_chmap && sink_chmap.channels >= 2 && sink_chmap.map[0] >= 0 && sink_chmap.map[1] >= 0 &&
        (sink_chmap.map[0] != kPaChannelPosFrontLeft || sink_chmap.map[1] != kPaChannelPosFrontRight)) {
        custom_map.channels = 2;
        custom_map.map[0] = sink_chmap.map[0];
        custom_map.map[1] = sink_chmap.map[1];
        stream_ = fn_stream_new_(ctx_, "Audio", &ss, &custom_map);
        if (stream_) {
            used_custom_chmap = true;
        }
    }
    if (!stream_) {
        stream_ = fn_stream_new_(ctx_, "Audio", &ss, nullptr);
    }
    if (!stream_) {
        restore_hdmi_card_profile_locked();
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

    const char* connect_sink = target_sink_.empty() ? nullptr : target_sink_.c_str();
    if (fn_stream_connect_playback_(stream_, connect_sink, &attr, 0, nullptr, nullptr) < 0) {
        const int err = fn_ctx_errno_(ctx_);
        std::fprintf(stderr, "czx: PulseAudio stream connect failed: %s\n",
                     (fn_strerror_ && err) ? fn_strerror_(err) : "stream connect error");
        restore_hdmi_card_profile_locked();
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
            restore_hdmi_card_profile_locked();
            fn_ml_unlock_(ml_);
            cleanup();
            return false;
        }
        fn_ml_wait_(ml_);
    }

    active_ = true;
    sample_rate_ = sample_rate;
    buffer_tlength_ = tlength;
    const std::string connected_sink = target_sink_;
    fn_ml_unlock_(ml_);

    if (!connected_sink.empty()) {
        if (used_custom_chmap) {
            std::fprintf(stderr, "czx: PulseAudio/PipeWire audio active (%u Hz, sink=%s, chmap=%d,%d)\n",
                         sample_rate, connected_sink.c_str(), custom_map.map[0], custom_map.map[1]);
        } else {
            std::fprintf(stderr, "czx: PulseAudio/PipeWire audio active (%u Hz, sink=%s)\n",
                         sample_rate, connected_sink.c_str());
        }
    } else {
        std::fprintf(stderr, "czx: PulseAudio/PipeWire audio active (%u Hz)\n", sample_rate);
    }
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

bool LinuxPulseAudio::find_hdmi_sink_locked(std::string& out_sink,
                                            std::string& out_seen,
                                            PaChannelMap& out_chmap,
                                            bool& out_muted) {
    out_sink.clear();
    out_seen.clear();
    out_chmap = PaChannelMap{};
    out_muted = false;
    if (!ctx_ || !fn_ctx_get_sink_info_list_) return false;

    struct SinkListQuery {
        PaThreadedMainloop* ml = nullptr;
        FnMlSignal signal = nullptr;
        std::string hdmi_sink;
        std::string seen_sinks;
        PaChannelMap chmap{};
        bool muted = false;
    } slq{ml_, fn_ml_signal_, {}, {}, {}, false};

    auto on_sink_list = [](PaContext*, const PaSinkInfo* info, int eol, void* userdata) {
        auto* q = static_cast<SinkListQuery*>(userdata);
        if (!q) return;
        if (!eol && info && info->name && info->name[0] != '\0') {
            if (!q->seen_sinks.empty()) {
                q->seen_sinks += ", ";
            }
            q->seen_sinks += info->name;
            if (q->hdmi_sink.empty() &&
                (contains_icase(info->name, "hdmi") ||
                 contains_icase(info->description, "hdmi"))) {
                q->hdmi_sink = info->name;
                q->chmap = info->channel_map;
                q->muted = (info->mute != 0);
            }
        }
        if (eol && q->ml && q->signal) {
            q->signal(q->ml, 0);
        }
    };

    PaOperation* op = fn_ctx_get_sink_info_list_(ctx_, on_sink_list, &slq);
    if (!wait_operation_locked(op)) {
        return false;
    }
    out_sink = std::move(slq.hdmi_sink);
    out_seen = std::move(slq.seen_sinks);
    out_chmap = slq.chmap;
    out_muted = slq.muted;
    return true;
}

bool LinuxPulseAudio::activate_hdmi_card_profile_locked() {
    if (!ctx_ || !fn_ctx_get_card_info_list_ || !fn_ctx_set_card_profile_by_name_) {
        return false;
    }

    struct CardListQuery {
        PaThreadedMainloop* ml = nullptr;
        FnMlSignal signal = nullptr;
        FnProplistGets proplist_gets = nullptr;
        std::string card_name;
        std::string prev_profile;
        std::string chosen_profile;
    } clq{ml_, fn_ml_signal_, fn_proplist_gets_, {}, {}, {}};

    auto on_card_list = [](PaContext*, const PaCardInfo* info, int eol, void* userdata) {
        auto* q = static_cast<CardListQuery*>(userdata);
        if (!q) return;
        if (eol) {
            if (q->ml && q->signal) {
                q->signal(q->ml, 0);
            }
            return;
        }
        if (!info || !info->name || info->name[0] == '\0' || !q->card_name.empty()) {
            return;
        }
        bool is_hdmi = contains_icase(info->name, "hdmi");
        if (!is_hdmi && q->proplist_gets && info->proplist) {
            const char* card_name = q->proplist_gets(info->proplist, "alsa.card_name");
            const char* long_name = q->proplist_gets(info->proplist, "alsa.long_card_name");
            const char* desc = q->proplist_gets(info->proplist, "device.description");
            is_hdmi = contains_icase(card_name, "hdmi") ||
                      contains_icase(long_name, "hdmi") ||
                      contains_icase(desc, "hdmi");
        }
        if (!is_hdmi) {
            return;
        }
        const uint32_t cur_sinks = info->active_profile2
                                       ? info->active_profile2->n_sinks
                                       : (info->active_profile ? info->active_profile->n_sinks : 0u);
        if (cur_sinks > 0u) {
            return;
        }

        std::vector<PulseCardProfileCandidate> profiles;
        profiles.reserve(info->n_profiles);
        if (info->profiles2) {
            for (uint32_t i = 0; i < info->n_profiles && info->profiles2[i]; ++i) {
                const auto* p = info->profiles2[i];
                profiles.push_back(PulseCardProfileCandidate{
                    p->name ? p->name : "",
                    p->n_sinks,
                    p->priority,
                    p->available != 0,
                });
            }
        } else if (info->profiles) {
            for (uint32_t i = 0; i < info->n_profiles; ++i) {
                const auto& p = info->profiles[i];
                profiles.push_back(PulseCardProfileCandidate{
                    p.name ? p.name : "",
                    p.n_sinks,
                    p.priority,
                    true,
                });
            }
        }
        std::string chosen = select_best_hdmi_card_profile(profiles);
        if (chosen.empty()) {
            return;
        }
        const char* prev_name = info->active_profile2
                                    ? info->active_profile2->name
                                    : (info->active_profile ? info->active_profile->name : nullptr);
        q->card_name = info->name;
        q->prev_profile = (prev_name && prev_name[0] != '\0') ? prev_name : "off";
        q->chosen_profile = std::move(chosen);
    };

    PaOperation* list_op = fn_ctx_get_card_info_list_(ctx_, on_card_list, &clq);
    if (!wait_operation_locked(list_op) || clq.card_name.empty() || clq.chosen_profile.empty()) {
        return false;
    }

    SuccessQuery pq{ml_, fn_ml_signal_, false};
    PaOperation* set_op = fn_ctx_set_card_profile_by_name_(
        ctx_, clq.card_name.c_str(), clq.chosen_profile.c_str(), on_op_success, &pq);
    if (!wait_operation_locked(set_op) || !pq.ok) {
        return false;
    }

    if (activated_hdmi_card_.empty()) {
        activated_hdmi_card_ = clq.card_name;
        prev_hdmi_card_profile_ = clq.prev_profile;
    }
    std::fprintf(stderr, "czx: Activated HDMI audio card %s profile '%s' (was '%s')\n",
                 clq.card_name.c_str(), clq.chosen_profile.c_str(), clq.prev_profile.c_str());
    return true;
}

void LinuxPulseAudio::restore_hdmi_card_profile_locked() {
    if (activated_hdmi_card_.empty() || prev_hdmi_card_profile_.empty()) {
        return;
    }
    if (ctx_ && fn_ctx_get_state_ && fn_ctx_get_state_(ctx_) == kPaContextReady &&
        fn_ctx_set_card_profile_by_name_) {
        SuccessQuery pq{ml_, fn_ml_signal_, false};
        PaOperation* op = fn_ctx_set_card_profile_by_name_(
            ctx_, activated_hdmi_card_.c_str(), prev_hdmi_card_profile_.c_str(),
            on_op_success, &pq);
        (void)wait_operation_locked(op);
    }
    activated_hdmi_card_.clear();
    prev_hdmi_card_profile_.clear();
}

void LinuxPulseAudio::restore_hdmi_card_profile() {
    if (activated_hdmi_card_.empty() || prev_hdmi_card_profile_.empty()) {
        return;
    }
    if (ml_ && started_ && ctx_ && fn_ml_lock_ && fn_ml_unlock_) {
        fn_ml_lock_(ml_);
        restore_hdmi_card_profile_locked();
        fn_ml_unlock_(ml_);
    } else {
        activated_hdmi_card_.clear();
        prev_hdmi_card_profile_.clear();
    }
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

    if (delta_percent > 0 && muted && fn_ctx_set_sink_mute_by_name_) {
        SuccessQuery mq{ml_, fn_ml_signal_, false};
        PaOperation* mop = fn_ctx_set_sink_mute_by_name_(ctx_, sink_name.c_str(), 0, on_op_success, &mq);
        if (wait_operation_locked(mop) && mq.ok) {
            muted = false;
        }
    }

    SuccessQuery vq{ml_, fn_ml_signal_, false};
    PaOperation* vop = fn_ctx_set_sink_volume_by_name_(ctx_, sink_name.c_str(), &next_cvol, on_op_success, &vq);
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
    SuccessQuery mq{ml_, fn_ml_signal_, false};
    PaOperation* op = fn_ctx_set_sink_mute_by_name_(
        ctx_, sink_name.c_str(), next_muted ? 1 : 0, on_op_success, &mq);
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

    if (pulse_.init_with_sink(sample_rate, nullptr, prefer_hdmi_sink_)) {
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
