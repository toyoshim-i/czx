#include "audio/mt32_synth.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace {

void touch_file(const fs::path& p) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << "dummy";
}

void test_rom_discovery() {
    const fs::path dir =
        fs::temp_directory_path() / ("czx_test_mt32_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    czx::audio::Mt32RomPair pair{};
    CHECK(!czx::audio::find_mt32_rom_pair(dir.string(), pair));

    // Only control ROM present -> false
    touch_file(dir / "MT32_CONTROL.ROM");
    CHECK(!czx::audio::find_mt32_rom_pair(dir.string(), pair));

    // MT-32 complete pair -> detected as "MT-32"
    touch_file(dir / "mt32_pcm.rom");
    CHECK(czx::audio::find_mt32_rom_pair(dir.string(), pair));
    CHECK(pair.label == "MT-32");
    CHECK(!pair.control_path.empty() && !pair.pcm_path.empty());

    // CM-32L preferred over MT-32 when both exist
    touch_file(dir / "CM32L_CONTROL.ROM");
    touch_file(dir / "CM32L_PCM.ROM");
    CHECK(czx::audio::find_mt32_rom_pair(dir.string(), pair));
    CHECK(pair.label == "CM-32L");

    // Versioned filenames also work
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    touch_file(dir / "MT32_CONTROL.1.07.ROM");
    touch_file(dir / "MT32_PCM.1.00.ROM");
    CHECK(czx::audio::find_mt32_rom_pair(dir.string(), pair));
    CHECK(pair.label == "MT-32");

    fs::remove_all(dir, ec);
}

void test_synth_no_roms_lifecycle() {
    const fs::path empty_dir =
        fs::temp_directory_path() / ("czx_test_mt32_empty_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(empty_dir, ec);
    fs::create_directories(empty_dir, ec);

    czx::audio::Mt32Synth synth;
    CHECK(!synth.has_roms());
    CHECK(!synth.init(empty_dir.string(), 48000));
    CHECK(!synth.has_roms());
    CHECK(synth.rom_label().empty());

    // Safe no-ops without ROMs
    synth.set_enabled(true);
    synth.set_sample_rate(44100);
    synth.write_midi_byte(0x90);
    const uint8_t msg[3] = {0x90, 0x3C, 0x64};
    synth.write_midi_bytes(msg, sizeof(msg));
    synth.reset();

    std::vector<int16_t> pcm = {100, -100, 200, -200};
    synth.mix_into(pcm.data(), 2);
    CHECK(pcm[0] == 100 && pcm[1] == -100 && pcm[2] == 200 && pcm[3] == -200);

    synth.shutdown();
    synth.shutdown();

    fs::remove_all(empty_dir, ec);
}

}  // namespace

int main() {
    test_rom_discovery();
    test_synth_no_roms_lifecycle();
    std::printf("test_mt32: all checks passed\n");
    return 0;
}
