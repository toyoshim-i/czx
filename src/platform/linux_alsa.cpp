#include "platform/linux_alsa.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <vector>

namespace czx::platform {

namespace {

constexpr int kSndPcmStreamPlayback = 0;
constexpr int kSndPcmNonblock = 1;
constexpr int kSndPcmFormatS16Le = 2;
constexpr int kSndPcmAccessRwInterleaved = 3;

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
    fn_open_ = nullptr;
    fn_set_params_ = nullptr;
    fn_writei_ = nullptr;
    fn_recover_ = nullptr;
    fn_drain_ = nullptr;
    fn_close_ = nullptr;
    fn_strerror_ = nullptr;
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

}  // namespace czx::platform
