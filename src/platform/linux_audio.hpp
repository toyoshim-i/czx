#pragma once

#include "platform/linux_alsa.hpp"
#include "platform/platform.hpp"

#include <cstdint>
#include <string>

namespace czx::platform {

class LinuxPulseAudio : public PlatformAudio {
public:
    LinuxPulseAudio() = default;
    ~LinuxPulseAudio() override;

    bool init(uint32_t sample_rate) override;
    bool init_with_sink(uint32_t sample_rate, const char* sink_dev, bool prefer_hdmi_sink = false);
    void write_samples(const int16_t* stereo_s16, size_t frame_count) override;
    double get_buffer_fill_ratio() const override;
    const char* backend_name() const override { return active_ ? "linux-pulse" : "null-audio"; }
    bool get_system_volume(SystemVolumeState& out) override;
    bool adjust_system_volume(int delta_percent, SystemVolumeState& out) override;
    bool toggle_system_mute(SystemVolumeState& out) override;
    void cleanup();

private:
    struct PaThreadedMainloop;
    struct PaMainloopApi;
    struct PaContext;
    struct PaStream;
    struct PaOperation;
    struct PaSampleSpec;
    struct PaChannelMap;
    struct PaCVolume;
    struct PaServerInfo;
    struct PaSinkInfo;
    struct PaBufferAttr;

    using PaContextNotifyCb = void (*)(PaContext*, void*);
    using PaStreamNotifyCb = void (*)(PaStream*, void*);
    using PaContextSuccessCb = void (*)(PaContext*, int, void*);
    using PaServerInfoCb = void (*)(PaContext*, const PaServerInfo*, void*);
    using PaSinkInfoCb = void (*)(PaContext*, const PaSinkInfo*, int, void*);

    using FnMlNew = PaThreadedMainloop* (*)();
    using FnMlStart = int (*)(PaThreadedMainloop*);
    using FnMlStop = void (*)(PaThreadedMainloop*);
    using FnMlFree = void (*)(PaThreadedMainloop*);
    using FnMlLock = void (*)(PaThreadedMainloop*);
    using FnMlUnlock = void (*)(PaThreadedMainloop*);
    using FnMlWait = void (*)(PaThreadedMainloop*);
    using FnMlSignal = void (*)(PaThreadedMainloop*, int);
    using FnMlGetApi = PaMainloopApi* (*)(PaThreadedMainloop*);

    using FnCtxNew = PaContext* (*)(PaMainloopApi*, const char*);
    using FnCtxSetStateCb = void (*)(PaContext*, PaContextNotifyCb, void*);
    using FnCtxConnect = int (*)(PaContext*, const char*, int, const void*);
    using FnCtxDisconnect = void (*)(PaContext*);
    using FnCtxUnref = void (*)(PaContext*);
    using FnCtxGetState = int (*)(const PaContext*);
    using FnCtxErrno = int (*)(const PaContext*);

    using FnStreamNew = PaStream* (*)(PaContext*, const char*, const PaSampleSpec*, const void*);
    using FnStreamSetStateCb = void (*)(PaStream*, PaStreamNotifyCb, void*);
    using FnStreamConnectPlayback = int (*)(PaStream*, const char*, const PaBufferAttr*, int,
                                            const void*, PaStream*);
    using FnStreamDisconnect = int (*)(PaStream*);
    using FnStreamUnref = void (*)(PaStream*);
    using FnStreamGetState = int (*)(const PaStream*);
    using FnStreamWritableSize = size_t (*)(const PaStream*);
    using FnStreamWrite = int (*)(PaStream*, const void*, size_t, void (*)(void*), int64_t, int);
    using FnStrerror = const char* (*)(int);

    using FnCtxGetServerInfo = PaOperation* (*)(PaContext*, PaServerInfoCb, void*);
    using FnCtxGetSinkInfoList = PaOperation* (*)(PaContext*, PaSinkInfoCb, void*);
    using FnCtxGetSinkInfoByName = PaOperation* (*)(PaContext*, const char*, PaSinkInfoCb, void*);
    using FnCtxSetSinkVolumeByName =
        PaOperation* (*)(PaContext*, const char*, const PaCVolume*, PaContextSuccessCb, void*);
    using FnCtxSetSinkMuteByName =
        PaOperation* (*)(PaContext*, const char*, int, PaContextSuccessCb, void*);
    using FnOpGetState = int (*)(const PaOperation*);
    using FnOpUnref = void (*)(PaOperation*);
    using FnCVolumeSet = PaCVolume* (*)(PaCVolume*, unsigned, uint32_t);
    using FnCVolumeAvg = uint32_t (*)(const PaCVolume*);

    static void on_context_state(PaContext* ctx, void* userdata);
    static void on_stream_state(PaStream* stream, void* userdata);

    bool wait_operation_locked(PaOperation* op);
    bool query_sink_locked(std::string& sink_name, PaCVolume& vol, bool& muted);

    void* dl_ = nullptr;
    PaThreadedMainloop* ml_ = nullptr;
    PaContext* ctx_ = nullptr;
    PaStream* stream_ = nullptr;
    bool started_ = false;
    bool active_ = false;
    uint32_t sample_rate_ = 0;
    uint32_t buffer_tlength_ = 0;
    std::string target_sink_;

    FnMlNew fn_ml_new_ = nullptr;
    FnMlStart fn_ml_start_ = nullptr;
    FnMlStop fn_ml_stop_ = nullptr;
    FnMlFree fn_ml_free_ = nullptr;
    FnMlLock fn_ml_lock_ = nullptr;
    FnMlUnlock fn_ml_unlock_ = nullptr;
    FnMlWait fn_ml_wait_ = nullptr;
    FnMlSignal fn_ml_signal_ = nullptr;
    FnMlGetApi fn_ml_get_api_ = nullptr;

    FnCtxNew fn_ctx_new_ = nullptr;
    FnCtxSetStateCb fn_ctx_set_state_cb_ = nullptr;
    FnCtxConnect fn_ctx_connect_ = nullptr;
    FnCtxDisconnect fn_ctx_disconnect_ = nullptr;
    FnCtxUnref fn_ctx_unref_ = nullptr;
    FnCtxGetState fn_ctx_get_state_ = nullptr;
    FnCtxErrno fn_ctx_errno_ = nullptr;

    FnStreamNew fn_stream_new_ = nullptr;
    FnStreamSetStateCb fn_stream_set_state_cb_ = nullptr;
    FnStreamConnectPlayback fn_stream_connect_playback_ = nullptr;
    FnStreamDisconnect fn_stream_disconnect_ = nullptr;
    FnStreamUnref fn_stream_unref_ = nullptr;
    FnStreamGetState fn_stream_get_state_ = nullptr;
    FnStreamWritableSize fn_stream_writable_size_ = nullptr;
    FnStreamWrite fn_stream_write_ = nullptr;
    FnStrerror fn_strerror_ = nullptr;

    FnCtxGetServerInfo fn_ctx_get_server_info_ = nullptr;
    FnCtxGetSinkInfoList fn_ctx_get_sink_info_list_ = nullptr;
    FnCtxGetSinkInfoByName fn_ctx_get_sink_info_by_name_ = nullptr;
    FnCtxSetSinkVolumeByName fn_ctx_set_sink_volume_by_name_ = nullptr;
    FnCtxSetSinkMuteByName fn_ctx_set_sink_mute_by_name_ = nullptr;
    FnOpGetState fn_op_get_state_ = nullptr;
    FnOpUnref fn_op_unref_ = nullptr;
    FnCVolumeSet fn_cvolume_set_ = nullptr;
    FnCVolumeAvg fn_cvolume_avg_ = nullptr;
};

class LinuxAudio : public PlatformAudio {
public:
    explicit LinuxAudio(bool prefer_hdmi_sink = false) : prefer_hdmi_sink_(prefer_hdmi_sink) {}
    ~LinuxAudio() override = default;

    bool init(uint32_t sample_rate) override;
    void write_samples(const int16_t* stereo_s16, size_t frame_count) override;
    double get_buffer_fill_ratio() const override;
    const char* backend_name() const override;
    bool get_system_volume(SystemVolumeState& out) override;
    bool adjust_system_volume(int delta_percent, SystemVolumeState& out) override;
    bool toggle_system_mute(SystemVolumeState& out) override;

private:
    enum class ActiveBackend { Inactive, Pulse, Alsa };

    LinuxPulseAudio pulse_;
    LinuxAlsaAudio alsa_;
    ActiveBackend active_ = ActiveBackend::Inactive;
    bool prefer_hdmi_sink_ = false;
};

}  // namespace czx::platform
