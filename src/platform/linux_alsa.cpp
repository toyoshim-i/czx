#include "platform/linux_alsa.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>
#include <vector>

namespace czx::platform {

namespace {

constexpr int kSndPcmStreamPlayback = 0;
constexpr int kSndPcmNonblock = 1;
constexpr int kSndPcmFormatS16Le = 2;
constexpr int kSndPcmAccessRwInterleaved = 3;

std::string to_lower_copy(const char* s) {
    std::string out;
    if (!s) return out;
    out = s;
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

int mixer_priority(const std::string& lower_name) {
    static constexpr const char* kPriority[] = {
        "master", "pcm", "speaker", "headphone", "dac", "playback",
    };
    for (size_t i = 0; i < sizeof(kPriority) / sizeof(kPriority[0]); ++i) {
        if (lower_name == kPriority[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

LinuxAlsaAudio::~LinuxAlsaAudio() {
    cleanup();
}

void LinuxAlsaAudio::cleanup() {
    if (pcm_ && fn_close_) {
        fn_close_(pcm_);
        pcm_ = nullptr;
    }
    if (dl_) {
        dlclose(dl_);
        dl_ = nullptr;
    }
    active_ = false;
    sample_rate_ = 0;
    soft_muted_ = false;
    soft_mute_prev_pct_ = 60;
    fn_open_ = nullptr;
    fn_set_params_ = nullptr;
    fn_writei_ = nullptr;
    fn_recover_ = nullptr;
    fn_drain_ = nullptr;
    fn_close_ = nullptr;
    fn_strerror_ = nullptr;
    fn_mixer_open_ = nullptr;
    fn_mixer_close_ = nullptr;
    fn_mixer_attach_ = nullptr;
    fn_mixer_selem_register_ = nullptr;
    fn_mixer_load_ = nullptr;
    fn_mixer_first_elem_ = nullptr;
    fn_mixer_elem_next_ = nullptr;
    fn_mixer_selem_get_name_ = nullptr;
    fn_mixer_selem_has_vol_ = nullptr;
    fn_mixer_selem_has_switch_ = nullptr;
    fn_mixer_selem_get_vol_range_ = nullptr;
    fn_mixer_selem_get_vol_ = nullptr;
    fn_mixer_selem_set_vol_all_ = nullptr;
    fn_mixer_selem_get_switch_ = nullptr;
    fn_mixer_selem_set_switch_all_ = nullptr;
}

bool LinuxAlsaAudio::open_device(const char* device, uint32_t sample_rate, std::string& last_err) {
    auto format_err = [&](int rc) -> std::string {
        if (fn_strerror_) {
            const char* msg = fn_strerror_(rc);
            if (msg && msg[0] != '\0') return std::string(msg);
        }
        return "error " + std::to_string(rc);
    };

    const int open_rc = fn_open_(&pcm_, device, kSndPcmStreamPlayback, kSndPcmNonblock);
    if (open_rc < 0 || !pcm_) {
        last_err = std::string(device) + ": snd_pcm_open failed (" + format_err(open_rc) + ")";
        pcm_ = nullptr;
        return false;
    }

    const int param_rc = fn_set_params_(pcm_, kSndPcmFormatS16Le, kSndPcmAccessRwInterleaved,
                                        2, sample_rate, 1, 100000);
    if (param_rc < 0) {
        last_err = std::string(device) + ": snd_pcm_set_params failed (" + format_err(param_rc) + ")";
        fn_close_(pcm_);
        pcm_ = nullptr;
        return false;
    }

    return true;
}

bool LinuxAlsaAudio::init(uint32_t sample_rate) {
    if (active_ && sample_rate_ == sample_rate) {
        return true;
    }
    cleanup();

    dl_ = dlopen("libasound.so.2", RTLD_LAZY);
    if (!dl_) {
        const char* err = dlerror();
        std::fprintf(stderr, "czx: ALSA unavailable (libasound.so.2): %s\n", err ? err : "unknown error");
        return false;
    }

    fn_open_ = reinterpret_cast<FnPcmOpen>(dlsym(dl_, "snd_pcm_open"));
    fn_set_params_ = reinterpret_cast<FnPcmSetParams>(dlsym(dl_, "snd_pcm_set_params"));
    fn_writei_ = reinterpret_cast<FnPcmWritei>(dlsym(dl_, "snd_pcm_writei"));
    fn_recover_ = reinterpret_cast<FnPcmRecover>(dlsym(dl_, "snd_pcm_recover"));
    fn_drain_ = reinterpret_cast<FnPcmDrain>(dlsym(dl_, "snd_pcm_drain"));
    fn_close_ = reinterpret_cast<FnPcmClose>(dlsym(dl_, "snd_pcm_close"));
    fn_strerror_ = reinterpret_cast<FnStrerror>(dlsym(dl_, "snd_strerror"));

    if (!fn_open_ || !fn_set_params_ || !fn_writei_ || !fn_recover_ || !fn_close_) {
        std::fprintf(stderr, "czx: ALSA symbol resolution failed in libasound.so.2\n");
        cleanup();
        return false;
    }

    std::vector<const char*> candidates;
    const char* env_device = std::getenv("CZX_AUDIO_DEVICE");
    if (env_device && env_device[0] != '\0') {
        candidates.push_back(env_device);
    } else {
        candidates = {"pipewire", "pulse", "default"};
    }

    std::string last_err = "no audio devices tried";
    for (const char* device : candidates) {
        if (open_device(device, sample_rate, last_err)) {
            active_ = true;
            sample_rate_ = sample_rate;
            std::fprintf(stderr, "czx: ALSA audio active on '%s' (%u Hz)\n", device, sample_rate);
            return true;
        }
    }

    std::fprintf(stderr, "czx: ALSA open failed: %s\n", last_err.c_str());
    cleanup();
    return false;
}

void LinuxAlsaAudio::write_samples(const int16_t* stereo_s16, size_t frame_count) {
    if (!active_ || !pcm_ || !stereo_s16 || frame_count == 0) return;

    const int16_t* ptr = stereo_s16;
    size_t remaining = frame_count;
    while (remaining > 0) {
        const long written = fn_writei_(pcm_, ptr, static_cast<unsigned long>(remaining));
        if (written > 0) {
            ptr += static_cast<size_t>(written) * 2u;
            remaining -= static_cast<size_t>(written);
        } else if (written == -EAGAIN) {
            break;
        } else {
            const int rec = fn_recover_(pcm_, static_cast<int>(written), 1);
            if (rec < 0) {
                break;
            }
        }
    }
}

bool LinuxAlsaAudio::ensure_dl_and_mixer_syms() {
    if (!dl_) {
        dl_ = dlopen("libasound.so.2", RTLD_LAZY);
        if (!dl_) {
            return false;
        }
    }
    if (fn_mixer_open_) {
        return true;
    }

    auto resolve = [&](auto& fn, const char* name) -> bool {
        using Fn = std::remove_reference_t<decltype(fn)>;
        fn = reinterpret_cast<Fn>(dlsym(dl_, name));
        return fn != nullptr;
    };

    const bool ok =
        resolve(fn_mixer_open_, "snd_mixer_open") &&
        resolve(fn_mixer_close_, "snd_mixer_close") &&
        resolve(fn_mixer_attach_, "snd_mixer_attach") &&
        resolve(fn_mixer_selem_register_, "snd_mixer_selem_register") &&
        resolve(fn_mixer_load_, "snd_mixer_load") &&
        resolve(fn_mixer_first_elem_, "snd_mixer_first_elem") &&
        resolve(fn_mixer_elem_next_, "snd_mixer_elem_next") &&
        resolve(fn_mixer_selem_get_name_, "snd_mixer_selem_get_name") &&
        resolve(fn_mixer_selem_has_vol_, "snd_mixer_selem_has_playback_volume") &&
        resolve(fn_mixer_selem_has_switch_, "snd_mixer_selem_has_playback_switch") &&
        resolve(fn_mixer_selem_get_vol_range_, "snd_mixer_selem_get_playback_volume_range") &&
        resolve(fn_mixer_selem_get_vol_, "snd_mixer_selem_get_playback_volume") &&
        resolve(fn_mixer_selem_set_vol_all_, "snd_mixer_selem_set_playback_volume_all") &&
        resolve(fn_mixer_selem_get_switch_, "snd_mixer_selem_get_playback_switch") &&
        resolve(fn_mixer_selem_set_switch_all_, "snd_mixer_selem_set_playback_switch_all");
    if (!ok) {
        fn_mixer_open_ = nullptr;
        return false;
    }
    return true;
}

namespace {

template <typename Fn>
bool run_with_alsa_mixer(
    const char* env_dev,
    int (*fn_open)(void**, int),
    int (*fn_close)(void*),
    int (*fn_attach)(void*, const char*),
    int (*fn_reg)(void*, void*, void**),
    int (*fn_load)(void*),
    void* (*fn_first)(void*),
    void* (*fn_next)(void*),
    const char* (*fn_name)(void*),
    int (*fn_has_vol)(void*),
    int (*fn_vol_range)(void*, long*, long*),
    Fn&& callback) {
    std::vector<std::string> cards;
    if (env_dev && env_dev[0] != '\0' && std::strncmp(env_dev, "pulse:", 6) != 0) {
        if (std::strncmp(env_dev, "plughw:", 7) == 0) {
            std::string card = std::string("hw:") + (env_dev + 7);
            const auto comma = card.find(',');
            if (comma != std::string::npos) card.resize(comma);
            cards.push_back(std::move(card));
        } else if (std::strncmp(env_dev, "hw:", 3) == 0) {
            std::string card = env_dev;
            const auto comma = card.find(',');
            if (comma != std::string::npos) card.resize(comma);
            cards.push_back(std::move(card));
        } else {
            cards.emplace_back(env_dev);
        }
    }
    for (const char* def_card : {"pipewire", "pulse", "default", "hw:0", "hw:1"}) {
        if (std::find(cards.begin(), cards.end(), def_card) == cards.end()) {
            cards.emplace_back(def_card);
        }
    }

    for (const std::string& card : cards) {
        void* handle = nullptr;
        if (fn_open(&handle, 0) < 0 || !handle) continue;
        if (fn_attach(handle, card.c_str()) < 0 ||
            fn_reg(handle, nullptr, nullptr) < 0 ||
            fn_load(handle) < 0) {
            fn_close(handle);
            continue;
        }

        void* best_elem = nullptr;
        int best_prio = 999;
        void* dacl_elem = nullptr;
        void* dacr_elem = nullptr;
        void* first_elem = nullptr;

        for (void* e = fn_first(handle); e; e = fn_next(e)) {
            if (!fn_has_vol(e)) continue;
            long vmin = 0;
            long vmax = 0;
            if (fn_vol_range(e, &vmin, &vmax) < 0 || vmax <= vmin) continue;
            if (!first_elem) first_elem = e;
            const std::string lname = to_lower_copy(fn_name(e));
            const int prio = mixer_priority(lname);
            if (prio >= 0 && prio < best_prio) {
                best_prio = prio;
                best_elem = e;
            }
            if (lname == "dacl" || lname == "dacl playback") dacl_elem = e;
            if (lname == "dacr" || lname == "dacr playback") dacr_elem = e;
        }

        std::vector<void*> targets;
        if (best_elem) {
            targets.push_back(best_elem);
        } else if (dacl_elem && dacr_elem) {
            targets.push_back(dacl_elem);
            targets.push_back(dacr_elem);
        } else if (first_elem) {
            targets.push_back(first_elem);
        }

        if (targets.empty()) {
            fn_close(handle);
            continue;
        }

        const bool ok = callback(targets);
        fn_close(handle);
        return ok;
    }
    return false;
}

}  // namespace

bool LinuxAlsaAudio::get_system_volume(SystemVolumeState& out) {
    if (!ensure_dl_and_mixer_syms()) return false;
    return run_with_alsa_mixer(
        std::getenv("CZX_AUDIO_DEVICE"),
        fn_mixer_open_, fn_mixer_close_, fn_mixer_attach_,
        fn_mixer_selem_register_, fn_mixer_load_,
        fn_mixer_first_elem_, fn_mixer_elem_next_, fn_mixer_selem_get_name_,
        fn_mixer_selem_has_vol_, fn_mixer_selem_get_vol_range_,
        [&](const std::vector<void*>& elems) -> bool {
            long vmin = 0;
            long vmax = 0;
            long val = 0;
            if (fn_mixer_selem_get_vol_range_(elems[0], &vmin, &vmax) < 0 || vmax <= vmin ||
                fn_mixer_selem_get_vol_(elems[0], 0, &val) < 0) {
                return false;
            }
            const int pct = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(val - vmin) * 100.0 /
                                             static_cast<double>(vmax - vmin))),
                0, 100);
            bool muted = false;
            if (fn_mixer_selem_has_switch_(elems[0])) {
                int sw = 1;
                if (fn_mixer_selem_get_switch_(elems[0], 0, &sw) == 0) {
                    muted = (sw == 0);
                }
            } else {
                muted = soft_muted_;
            }
            out.percent = muted && soft_muted_ ? soft_mute_prev_pct_ : pct;
            out.muted = muted;
            return true;
        });
}

bool LinuxAlsaAudio::adjust_system_volume(int delta_percent, SystemVolumeState& out) {
    if (!ensure_dl_and_mixer_syms()) return false;
    return run_with_alsa_mixer(
        std::getenv("CZX_AUDIO_DEVICE"),
        fn_mixer_open_, fn_mixer_close_, fn_mixer_attach_,
        fn_mixer_selem_register_, fn_mixer_load_,
        fn_mixer_first_elem_, fn_mixer_elem_next_, fn_mixer_selem_get_name_,
        fn_mixer_selem_has_vol_, fn_mixer_selem_get_vol_range_,
        [&](const std::vector<void*>& elems) -> bool {
            long vmin = 0;
            long vmax = 0;
            long val = 0;
            if (fn_mixer_selem_get_vol_range_(elems[0], &vmin, &vmax) < 0 || vmax <= vmin ||
                fn_mixer_selem_get_vol_(elems[0], 0, &val) < 0) {
                return false;
            }
            int cur_pct = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(val - vmin) * 100.0 /
                                             static_cast<double>(vmax - vmin))),
                0, 100);
            const bool has_hw_switch = (fn_mixer_selem_has_switch_(elems[0]) != 0);
            bool muted = false;
            if (has_hw_switch) {
                int sw = 1;
                if (fn_mixer_selem_get_switch_(elems[0], 0, &sw) == 0) {
                    muted = (sw == 0);
                }
            } else if (soft_muted_) {
                muted = true;
                cur_pct = soft_mute_prev_pct_;
            }

            const int next_pct = std::clamp(cur_pct + delta_percent, 0, 100);
            if (!has_hw_switch && soft_muted_) {
                soft_mute_prev_pct_ = next_pct;
                if (delta_percent > 0) {
                    soft_muted_ = false;
                    muted = false;
                }
            }

            const int apply_pct = (!has_hw_switch && muted) ? 0 : next_pct;
            for (void* e : elems) {
                long emin = 0;
                long emax = 0;
                if (fn_mixer_selem_get_vol_range_(e, &emin, &emax) < 0 || emax <= emin) continue;
                const long nval = std::clamp(
                    emin + static_cast<long>(std::lround(static_cast<double>(apply_pct) *
                                                         static_cast<double>(emax - emin) / 100.0)),
                    emin, emax);
                fn_mixer_selem_set_vol_all_(e, nval);
                if (has_hw_switch && delta_percent > 0 && muted && fn_mixer_selem_has_switch_(e)) {
                    fn_mixer_selem_set_switch_all_(e, 1);
                }
            }
            if (has_hw_switch && delta_percent > 0 && muted) {
                muted = false;
            }
            out.percent = next_pct;
            out.muted = muted;
            return true;
        });
}

bool LinuxAlsaAudio::toggle_system_mute(SystemVolumeState& out) {
    if (!ensure_dl_and_mixer_syms()) return false;
    return run_with_alsa_mixer(
        std::getenv("CZX_AUDIO_DEVICE"),
        fn_mixer_open_, fn_mixer_close_, fn_mixer_attach_,
        fn_mixer_selem_register_, fn_mixer_load_,
        fn_mixer_first_elem_, fn_mixer_elem_next_, fn_mixer_selem_get_name_,
        fn_mixer_selem_has_vol_, fn_mixer_selem_get_vol_range_,
        [&](const std::vector<void*>& elems) -> bool {
            long vmin = 0;
            long vmax = 0;
            long val = 0;
            if (fn_mixer_selem_get_vol_range_(elems[0], &vmin, &vmax) < 0 || vmax <= vmin ||
                fn_mixer_selem_get_vol_(elems[0], 0, &val) < 0) {
                return false;
            }
            const int cur_pct = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(val - vmin) * 100.0 /
                                             static_cast<double>(vmax - vmin))),
                0, 100);

            if (fn_mixer_selem_has_switch_(elems[0])) {
                int sw = 1;
                if (fn_mixer_selem_get_switch_(elems[0], 0, &sw) < 0) {
                    return false;
                }
                const int next_sw = (sw == 0) ? 1 : 0;
                for (void* e : elems) {
                    if (fn_mixer_selem_has_switch_(e)) {
                        fn_mixer_selem_set_switch_all_(e, next_sw);
                    }
                }
                out.percent = cur_pct;
                out.muted = (next_sw == 0);
                return true;
            }

            if (!soft_muted_) {
                if (cur_pct > 0) soft_mute_prev_pct_ = cur_pct;
                soft_muted_ = true;
                for (void* e : elems) {
                    long emin = 0;
                    long emax = 0;
                    if (fn_mixer_selem_get_vol_range_(e, &emin, &emax) == 0 && emax > emin) {
                        fn_mixer_selem_set_vol_all_(e, emin);
                    }
                }
                out.percent = soft_mute_prev_pct_;
                out.muted = true;
            } else {
                soft_muted_ = false;
                for (void* e : elems) {
                    long emin = 0;
                    long emax = 0;
                    if (fn_mixer_selem_get_vol_range_(e, &emin, &emax) == 0 && emax > emin) {
                        const long nval = std::clamp(
                            emin + static_cast<long>(std::lround(static_cast<double>(soft_mute_prev_pct_) *
                                                                 static_cast<double>(emax - emin) / 100.0)),
                            emin, emax);
                        fn_mixer_selem_set_vol_all_(e, nval);
                    }
                }
                out.percent = soft_mute_prev_pct_;
                out.muted = false;
            }
            return true;
        });
}

}  // namespace czx::platform
