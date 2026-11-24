#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace czx::audio {

struct Mt32RomPair {
    std::string control_path;
    std::string pcm_path;
    std::string label;  // "CM-32L" or "MT-32"
};

// Searches `rom_dir` for a CM-32L or MT-32 control + PCM ROM pair (case-insensitive).
// Prefers CM-32L when both are present.
bool find_mt32_rom_pair(const std::string& rom_dir, Mt32RomPair& out);

// Background-threaded Roland MT-32 / CM-32L synthesizer backed by libmt32emu (munt).
class Mt32Synth {
public:
    Mt32Synth();
    ~Mt32Synth();

    Mt32Synth(const Mt32Synth&) = delete;
    Mt32Synth& operator=(const Mt32Synth&) = delete;

    // Discovers ROMs in `rom_dir` and starts the synthesis worker thread if found.
    // Returns true if ROMs were loaded and the synth is ready.
    bool init(const std::string& rom_dir, uint32_t sample_rate);
    void shutdown();

    void set_sample_rate(uint32_t sample_rate);
    void set_enabled(bool enabled) noexcept;
    bool is_enabled() const noexcept;

    bool has_roms() const noexcept;
    std::string rom_label() const;

    // Non-blocking MIDI byte input (safe to call from emulation thread).
    void write_midi_byte(uint8_t byte);
    void write_midi_bytes(const uint8_t* data, size_t len);

    // Resets MIDI parser & active voices (e.g. on core reset or switch).
    void reset();

    // Mixes rendered MT-32 stereo S16LE frames into `stereo_s16` with saturation.
    // Non-blocking; returns immediately when disabled, idle, or without ROMs.
    void mix_into(int16_t* stereo_s16, size_t frame_count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace czx::audio
