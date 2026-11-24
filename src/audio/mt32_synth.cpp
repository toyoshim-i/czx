#include "audio/mt32_synth.hpp"

#include <mt32emu/mt32emu.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace czx::audio {

namespace {

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string suf(suffix);
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

int16_t saturating_add_s16(int16_t a, int16_t b) noexcept {
    const int32_t sum = static_cast<int32_t>(a) + static_cast<int32_t>(b);
    return static_cast<int16_t>(std::clamp<int32_t>(sum, -32768, 32767));
}

}  // namespace

bool find_mt32_rom_pair(const std::string& rom_dir, Mt32RomPair& out) {
    out = Mt32RomPair{};
    if (rom_dir.empty()) return false;

    std::error_code ec;
    if (!fs::is_directory(rom_dir, ec)) return false;

    std::string cm32l_ctrl;
    std::string cm32l_pcm;
    std::string mt32_ctrl;
    std::string mt32_pcm;

    for (const auto& entry : fs::directory_iterator(rom_dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fname = to_lower_copy(entry.path().filename().string());
        if (!ends_with(fname, ".rom") && !ends_with(fname, ".bin")) continue;
        const std::string full = entry.path().string();

        if (fname == "cm32l_control.rom") {
            cm32l_ctrl = full;
        } else if (fname == "cm32l_pcm.rom") {
            cm32l_pcm = full;
        } else if (fname == "mt32_control.rom") {
            mt32_ctrl = full;
        } else if (fname == "mt32_pcm.rom") {
            mt32_pcm = full;
        } else if (fname.find("cm32l") != std::string::npos) {
            if (cm32l_ctrl.empty() && fname.find("control") != std::string::npos) {
                cm32l_ctrl = full;
            } else if (cm32l_pcm.empty() && fname.find("pcm") != std::string::npos) {
                cm32l_pcm = full;
            }
        } else if (fname.find("mt32") != std::string::npos) {
            if (mt32_ctrl.empty() && fname.find("control") != std::string::npos) {
                mt32_ctrl = full;
            } else if (mt32_pcm.empty() && fname.find("pcm") != std::string::npos) {
                mt32_pcm = full;
            }
        }
    }

    if (!cm32l_ctrl.empty() && !cm32l_pcm.empty()) {
        out = Mt32RomPair{cm32l_ctrl, cm32l_pcm, "CM-32L"};
        return true;
    }
    if (!mt32_ctrl.empty() && !mt32_pcm.empty()) {
        out = Mt32RomPair{mt32_ctrl, mt32_pcm, "MT-32"};
        return true;
    }
    return false;
}

struct Mt32Synth::Impl {
    static constexpr size_t kRingCapacityFrames = 8192;
    static constexpr size_t kTargetBufferedFrames = 2048;
    static constexpr size_t kRenderChunkFrames = 256;

    std::atomic<bool> enabled{true};
    std::atomic<bool> ready{false};
    std::atomic<size_t> ring_count{0};

    std::string rom_label;

    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;
    bool reset_req = false;
    uint32_t sample_rate = 48000;
    bool rate_changed = false;

    std::vector<uint8_t> midi_in;
    std::vector<int16_t> ring_pcm;  // interleaved stereo, kRingCapacityFrames * 2
    size_t ring_read = 0;
    size_t ring_write = 0;

    MT32Emu::FileStream ctrl_stream;
    MT32Emu::FileStream pcm_stream;
    const MT32Emu::ROMImage* ctrl_rom = nullptr;
    const MT32Emu::ROMImage* pcm_rom = nullptr;
    std::unique_ptr<MT32Emu::Synth> synth;
    std::unique_ptr<MT32Emu::DefaultMidiStreamParser> parser;
    std::unique_ptr<MT32Emu::SampleRateConverter> resampler;
    std::thread worker;

    void worker_loop() {
        std::vector<uint8_t> local_midi;
        local_midi.reserve(512);
        std::vector<int16_t> chunk(kRenderChunkFrames * 2u, 0);

        while (true) {
            bool do_reset = false;
            uint32_t new_rate = 0;
            bool is_enabled = enabled.load(std::memory_order_relaxed);
            local_midi.clear();

            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [&]() {
                    if (stop || reset_req || rate_changed || !midi_in.empty()) return true;
                    if (enabled.load(std::memory_order_relaxed) &&
                        synth && synth->isActive() &&
                        ring_count.load(std::memory_order_relaxed) < kTargetBufferedFrames) {
                        return true;
                    }
                    return false;
                });
                if (stop) break;

                do_reset = reset_req;
                reset_req = false;
                if (rate_changed) {
                    new_rate = sample_rate;
                    rate_changed = false;
                }
                if (!midi_in.empty()) {
                    local_midi.swap(midi_in);
                }
                is_enabled = enabled.load(std::memory_order_relaxed);
            }

            if (new_rate > 0 && synth) {
                resampler = std::make_unique<MT32Emu::SampleRateConverter>(
                    *synth, static_cast<double>(new_rate), MT32Emu::SamplerateConversionQuality_FAST);
                std::lock_guard<std::mutex> lock(mtx);
                ring_read = 0;
                ring_write = 0;
                ring_count.store(0, std::memory_order_release);
            }

            if (do_reset && synth) {
                synth->flushMIDIQueue();
                // Re-open or send All Sound Off on all 16 channels and recreate parser.
                for (uint32_t ch = 0; ch < 16u; ++ch) {
                    synth->playMsgNow(0x000078B0u | ch);  // CC 120: All Sound Off
                    synth->playMsgNow(0x00007BB0u | ch);  // CC 123: All Notes Off
                }
                parser = std::make_unique<MT32Emu::DefaultMidiStreamParser>(*synth);
                std::lock_guard<std::mutex> lock(mtx);
                ring_read = 0;
                ring_write = 0;
                ring_count.store(0, std::memory_order_release);
            }

            if (!local_midi.empty() && parser) {
                if (is_enabled) {
                    parser->parseStream(local_midi.data(), static_cast<MT32Emu::Bit32u>(local_midi.size()));
                }
            }

            if (!is_enabled) {
                if (ring_count.load(std::memory_order_relaxed) > 0) {
                    std::lock_guard<std::mutex> lock(mtx);
                    ring_read = 0;
                    ring_write = 0;
                    ring_count.store(0, std::memory_order_release);
                }
                continue;
            }

            while (synth && resampler && synth->isActive()) {
                const size_t cur = ring_count.load(std::memory_order_acquire);
                if (cur >= kTargetBufferedFrames) break;
                if (!enabled.load(std::memory_order_relaxed)) break;

                resampler->getOutputSamples(chunk.data(), static_cast<unsigned int>(kRenderChunkFrames));

                std::lock_guard<std::mutex> lock(mtx);
                if (stop || reset_req || rate_changed) break;
                const size_t free_frames = kRingCapacityFrames - ring_count.load(std::memory_order_relaxed);
                const size_t to_write = std::min(kRenderChunkFrames, free_frames);
                for (size_t i = 0; i < to_write; ++i) {
                    ring_pcm[ring_write * 2u + 0u] = chunk[i * 2u + 0u];
                    ring_pcm[ring_write * 2u + 1u] = chunk[i * 2u + 1u];
                    ring_write = (ring_write + 1u) % kRingCapacityFrames;
                }
                ring_count.fetch_add(to_write, std::memory_order_release);
                if (!midi_in.empty()) break;  // Drain newly arrived MIDI promptly.
            }
        }
    }
};

Mt32Synth::Mt32Synth() : impl_(std::make_unique<Impl>()) {}

Mt32Synth::~Mt32Synth() {
    shutdown();
}

bool Mt32Synth::init(const std::string& rom_dir, uint32_t sample_rate) {
    shutdown();

    Mt32RomPair pair{};
    if (!find_mt32_rom_pair(rom_dir, pair)) {
        return false;
    }

    if (!impl_->ctrl_stream.open(pair.control_path.c_str()) ||
        !impl_->pcm_stream.open(pair.pcm_path.c_str())) {
        impl_->ctrl_stream.close();
        impl_->pcm_stream.close();
        return false;
    }

    impl_->ctrl_rom = MT32Emu::ROMImage::makeROMImage(&impl_->ctrl_stream);
    impl_->pcm_rom = MT32Emu::ROMImage::makeROMImage(&impl_->pcm_stream);
    if (!impl_->ctrl_rom || !impl_->pcm_rom) {
        shutdown();
        return false;
    }

    impl_->synth = std::make_unique<MT32Emu::Synth>();
    impl_->synth->selectRendererType(MT32Emu::RendererType_BIT16S);
    if (!impl_->synth->open(*impl_->ctrl_rom, *impl_->pcm_rom,
                            MT32Emu::DEFAULT_MAX_PARTIALS,
                            MT32Emu::AnalogOutputMode_COARSE)) {
        std::fprintf(stderr, "czx: MT-32 synth open failed (%s)\n", pair.label.c_str());
        shutdown();
        return false;
    }

    impl_->sample_rate = (sample_rate >= 8000u && sample_rate <= 192000u) ? sample_rate : 48000u;
    impl_->parser = std::make_unique<MT32Emu::DefaultMidiStreamParser>(*impl_->synth);
    impl_->resampler = std::make_unique<MT32Emu::SampleRateConverter>(
        *impl_->synth, static_cast<double>(impl_->sample_rate), MT32Emu::SamplerateConversionQuality_FAST);
    impl_->ring_pcm.assign(Impl::kRingCapacityFrames * 2u, 0);
    impl_->ring_read = 0;
    impl_->ring_write = 0;
    impl_->ring_count.store(0, std::memory_order_relaxed);
    impl_->stop = false;
    impl_->reset_req = false;
    impl_->rate_changed = false;
    impl_->rom_label = pair.label;
    impl_->ready.store(true, std::memory_order_release);

    impl_->worker = std::thread([this]() { impl_->worker_loop(); });
    std::fprintf(stderr, "czx: MT-32 synth ready (%s, %u Hz, background thread)\n",
                 pair.label.c_str(), impl_->sample_rate);
    return true;
}

void Mt32Synth::shutdown() {
    if (!impl_) return;
    if (impl_->worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(impl_->mtx);
            impl_->stop = true;
        }
        impl_->cv.notify_all();
        impl_->worker.join();
    }
    impl_->ready.store(false, std::memory_order_release);
    impl_->resampler.reset();
    impl_->parser.reset();
    if (impl_->synth) {
        impl_->synth->close();
        impl_->synth.reset();
    }
    if (impl_->ctrl_rom) {
        MT32Emu::ROMImage::freeROMImage(impl_->ctrl_rom);
        impl_->ctrl_rom = nullptr;
    }
    if (impl_->pcm_rom) {
        MT32Emu::ROMImage::freeROMImage(impl_->pcm_rom);
        impl_->pcm_rom = nullptr;
    }
    impl_->ctrl_stream.close();
    impl_->pcm_stream.close();
    impl_->rom_label.clear();
    impl_->midi_in.clear();
    impl_->ring_pcm.clear();
    impl_->ring_read = 0;
    impl_->ring_write = 0;
    impl_->ring_count.store(0, std::memory_order_relaxed);
}

void Mt32Synth::set_sample_rate(uint32_t sample_rate) {
    if (!impl_ || !impl_->ready.load(std::memory_order_acquire)) return;
    if (sample_rate < 8000u || sample_rate > 192000u) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        if (impl_->sample_rate == sample_rate) return;
        impl_->sample_rate = sample_rate;
        impl_->rate_changed = true;
    }
    impl_->cv.notify_one();
}

void Mt32Synth::set_enabled(bool enabled) noexcept {
    if (!impl_) return;
    impl_->enabled.store(enabled, std::memory_order_release);
    impl_->cv.notify_one();
}

bool Mt32Synth::is_enabled() const noexcept {
    return impl_ && impl_->enabled.load(std::memory_order_acquire);
}

bool Mt32Synth::has_roms() const noexcept {
    return impl_ && impl_->ready.load(std::memory_order_acquire);
}

std::string Mt32Synth::rom_label() const {
    return impl_ ? impl_->rom_label : std::string{};
}

void Mt32Synth::write_midi_byte(uint8_t byte) {
    if (!impl_ || !impl_->ready.load(std::memory_order_acquire) ||
        !impl_->enabled.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        if (impl_->midi_in.size() < 16384u) {
            impl_->midi_in.push_back(byte);
        }
    }
    impl_->cv.notify_one();
}

void Mt32Synth::write_midi_bytes(const uint8_t* data, size_t len) {
    if (!data || len == 0u) return;
    if (!impl_ || !impl_->ready.load(std::memory_order_acquire) ||
        !impl_->enabled.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const size_t avail = (impl_->midi_in.size() < 16384u) ? (16384u - impl_->midi_in.size()) : 0u;
        const size_t n = std::min(len, avail);
        impl_->midi_in.insert(impl_->midi_in.end(), data, data + n);
    }
    impl_->cv.notify_one();
}

void Mt32Synth::reset() {
    if (!impl_ || !impl_->ready.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->midi_in.clear();
        impl_->reset_req = true;
    }
    impl_->cv.notify_one();
}

void Mt32Synth::mix_into(int16_t* stereo_s16, size_t frame_count) {
    if (!stereo_s16 || frame_count == 0u || !impl_) return;
    if (!impl_->ready.load(std::memory_order_acquire) ||
        !impl_->enabled.load(std::memory_order_acquire)) {
        return;
    }
    if (impl_->ring_count.load(std::memory_order_acquire) == 0u) {
        return;
    }

    size_t remaining_after = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        const size_t avail = impl_->ring_count.load(std::memory_order_relaxed);
        const size_t to_mix = std::min(frame_count, avail);
        for (size_t i = 0; i < to_mix; ++i) {
            const size_t idx = impl_->ring_read * 2u;
            stereo_s16[i * 2u + 0u] = saturating_add_s16(stereo_s16[i * 2u + 0u], impl_->ring_pcm[idx + 0u]);
            stereo_s16[i * 2u + 1u] = saturating_add_s16(stereo_s16[i * 2u + 1u], impl_->ring_pcm[idx + 1u]);
            impl_->ring_read = (impl_->ring_read + 1u) % Impl::kRingCapacityFrames;
        }
        impl_->ring_count.fetch_sub(to_mix, std::memory_order_release);
        remaining_after = avail - to_mix;
    }
    if (remaining_after < Impl::kTargetBufferedFrames) {
        impl_->cv.notify_one();
    }
}

}  // namespace czx::audio
