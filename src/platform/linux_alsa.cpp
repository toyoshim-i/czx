#include "platform/linux_alsa.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

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
}

bool LinuxAlsaAudio::init(uint32_t sample_rate) {
    cleanup();

    dl_ = dlopen("libasound.so.2", RTLD_LAZY);
    if (!dl_) {
        return false;
    }

    fn_open_ = reinterpret_cast<FnPcmOpen>(dlsym(dl_, "snd_pcm_open"));
    fn_set_params_ = reinterpret_cast<FnPcmSetParams>(dlsym(dl_, "snd_pcm_set_params"));
    fn_writei_ = reinterpret_cast<FnPcmWritei>(dlsym(dl_, "snd_pcm_writei"));
    fn_recover_ = reinterpret_cast<FnPcmRecover>(dlsym(dl_, "snd_pcm_recover"));
    fn_drain_ = reinterpret_cast<FnPcmDrain>(dlsym(dl_, "snd_pcm_drain"));
    fn_close_ = reinterpret_cast<FnPcmClose>(dlsym(dl_, "snd_pcm_close"));

    if (!fn_open_ || !fn_set_params_ || !fn_writei_ || !fn_recover_ || !fn_close_) {
        cleanup();
        return false;
    }

    const char* device = std::getenv("CZX_AUDIO_DEVICE");
    if (!device || device[0] == '\0') {
        device = "default";
    }

    if (fn_open_(&pcm_, device, kSndPcmStreamPlayback, kSndPcmNonblock) < 0 || !pcm_) {
        cleanup();
        return false;
    }

    if (fn_set_params_(pcm_, kSndPcmFormatS16Le, kSndPcmAccessRwInterleaved,
                       2, sample_rate, 1, 45000) < 0) {
        cleanup();
        return false;
    }

    active_ = true;
    std::fprintf(stderr, "czx: ALSA audio active on '%s' (%u Hz)\n", device, sample_rate);
    return true;
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
