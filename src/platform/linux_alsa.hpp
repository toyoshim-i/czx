#pragma once

#include "platform/platform.hpp"

#include <cstdint>
#include <string>

namespace czx::platform {

class LinuxAlsaAudio : public PlatformAudio {
public:
    LinuxAlsaAudio() = default;
    ~LinuxAlsaAudio() override;

    bool init(uint32_t sample_rate) override;
    void write_samples(const int16_t* stereo_s16, size_t frame_count) override;
    const char* backend_name() const override { return active_ ? "linux-alsa" : "null-audio"; }
    void cleanup();

private:
    using PcmHandle = void*;
    using FnPcmOpen = int (*)(PcmHandle*, const char*, int, int);
    using FnPcmSetParams = int (*)(PcmHandle, int, int, unsigned int, unsigned int, int, unsigned int);
    using FnPcmWritei = long (*)(PcmHandle, const void*, unsigned long);
    using FnPcmRecover = int (*)(PcmHandle, int, int);
    using FnPcmDrain = int (*)(PcmHandle);
    using FnPcmClose = int (*)(PcmHandle);
    using FnStrerror = const char* (*)(int);

    void* dl_ = nullptr;
    PcmHandle pcm_ = nullptr;
    bool active_ = false;
    uint32_t sample_rate_ = 0;

    FnPcmOpen fn_open_ = nullptr;
    FnPcmSetParams fn_set_params_ = nullptr;
    FnPcmWritei fn_writei_ = nullptr;
    FnPcmRecover fn_recover_ = nullptr;
    FnPcmDrain fn_drain_ = nullptr;
    FnPcmClose fn_close_ = nullptr;
    FnStrerror fn_strerror_ = nullptr;

    bool open_device(const char* device, uint32_t sample_rate, std::string& last_err);
};

}  // namespace czx::platform
