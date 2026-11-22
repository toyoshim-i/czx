#pragma once

#include "platform/platform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace czx::platform {

class LinuxAlsaAudio : public PlatformAudio {
public:
    LinuxAlsaAudio() = default;
    ~LinuxAlsaAudio() override;

    bool init(uint32_t sample_rate) override;
    void write_samples(const int16_t* stereo_s16, size_t frame_count) override;
    const char* backend_name() const override { return active_ ? "linux-alsa" : "null-audio"; }
    bool get_system_volume(SystemVolumeState& out) override;
    bool adjust_system_volume(int delta_percent, SystemVolumeState& out) override;
    bool toggle_system_mute(SystemVolumeState& out) override;
    void cleanup();

private:
    using PcmHandle = void*;
    using MixerHandle = void*;
    using MixerElem = void*;
    using FnPcmOpen = int (*)(PcmHandle*, const char*, int, int);
    using FnPcmSetParams = int (*)(PcmHandle, int, int, unsigned int, unsigned int, int, unsigned int);
    using FnPcmWritei = long (*)(PcmHandle, const void*, unsigned long);
    using FnPcmRecover = int (*)(PcmHandle, int, int);
    using FnPcmDrain = int (*)(PcmHandle);
    using FnPcmClose = int (*)(PcmHandle);
    using FnStrerror = const char* (*)(int);

    using FnMixerOpen = int (*)(MixerHandle*, int);
    using FnMixerClose = int (*)(MixerHandle);
    using FnMixerAttach = int (*)(MixerHandle, const char*);
    using FnMixerSelemRegister = int (*)(MixerHandle, void*, void**);
    using FnMixerLoad = int (*)(MixerHandle);
    using FnMixerFirstElem = MixerElem (*)(MixerHandle);
    using FnMixerElemNext = MixerElem (*)(MixerElem);
    using FnMixerSelemGetName = const char* (*)(MixerElem);
    using FnMixerSelemHasVol = int (*)(MixerElem);
    using FnMixerSelemHasSwitch = int (*)(MixerElem);
    using FnMixerSelemGetVolRange = int (*)(MixerElem, long*, long*);
    using FnMixerSelemGetVol = int (*)(MixerElem, int, long*);
    using FnMixerSelemSetVolAll = int (*)(MixerElem, long);
    using FnMixerSelemGetSwitch = int (*)(MixerElem, int, int*);
    using FnMixerSelemSetSwitchAll = int (*)(MixerElem, int);

    void* dl_ = nullptr;
    PcmHandle pcm_ = nullptr;
    bool active_ = false;
    uint32_t sample_rate_ = 0;
    bool soft_muted_ = false;
    int soft_mute_prev_pct_ = 60;

    FnPcmOpen fn_open_ = nullptr;
    FnPcmSetParams fn_set_params_ = nullptr;
    FnPcmWritei fn_writei_ = nullptr;
    FnPcmRecover fn_recover_ = nullptr;
    FnPcmDrain fn_drain_ = nullptr;
    FnPcmClose fn_close_ = nullptr;
    FnStrerror fn_strerror_ = nullptr;

    FnMixerOpen fn_mixer_open_ = nullptr;
    FnMixerClose fn_mixer_close_ = nullptr;
    FnMixerAttach fn_mixer_attach_ = nullptr;
    FnMixerSelemRegister fn_mixer_selem_register_ = nullptr;
    FnMixerLoad fn_mixer_load_ = nullptr;
    FnMixerFirstElem fn_mixer_first_elem_ = nullptr;
    FnMixerElemNext fn_mixer_elem_next_ = nullptr;
    FnMixerSelemGetName fn_mixer_selem_get_name_ = nullptr;
    FnMixerSelemHasVol fn_mixer_selem_has_vol_ = nullptr;
    FnMixerSelemHasSwitch fn_mixer_selem_has_switch_ = nullptr;
    FnMixerSelemGetVolRange fn_mixer_selem_get_vol_range_ = nullptr;
    FnMixerSelemGetVol fn_mixer_selem_get_vol_ = nullptr;
    FnMixerSelemSetVolAll fn_mixer_selem_set_vol_all_ = nullptr;
    FnMixerSelemGetSwitch fn_mixer_selem_get_switch_ = nullptr;
    FnMixerSelemSetSwitchAll fn_mixer_selem_set_switch_all_ = nullptr;

    bool ensure_dl_and_mixer_syms();
    bool open_device(const char* device, uint32_t sample_rate, std::string& last_err);
};

}  // namespace czx::platform
