#include "core/core_registry.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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

std::string first_candidate(const std::string& path) {
    const auto cands = czx::core::candidate_core_ids_for_media(path);
    return cands.empty() ? std::string{} : cands.front();
}

void write_bytes(const fs::path& p, size_t size, const char* prefix, size_t prefix_len) {
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    CHECK(ofs.is_open());
    std::vector<char> buf(size, 0);
    for (size_t i = 0; i < prefix_len && i < buf.size(); ++i) {
        buf[i] = prefix[i];
    }
    ofs.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    CHECK(ofs.good());
}

void test_unambiguous_extensions() {
    CHECK(first_candidate("game.woz") == "applewin");
    CHECK(first_candidate("GAME.HDV") == "applewin");
    CHECK(first_candidate("disk.d64") == "vice_x64");
    CHECK(first_candidate("PROG.PRG") == "vice_x64");
    CHECK(first_candidate("tape.t88") == "quasi88");
    CHECK(first_candidate("tape.CMT") == "quasi88");
    CHECK(first_candidate("boot.fdi") == "np2kai");
    CHECK(first_candidate("hdd.HDI") == "np2kai");
    CHECK(first_candidate("cart.rom") == "bluemsx");
    CHECK(first_candidate("cart.MX2") == "bluemsx");
    CHECK(first_candidate("prog.bas") == "g800");
    CHECK(first_candidate("code.IHX") == "g800");
    CHECK(first_candidate("game.MZT") == "emuz700");
    CHECK(first_candidate("qd.q20") == "emuz700");
    CHECK(first_candidate("disk.2d") == "emuz700");
    CHECK(first_candidate("disk.d77") == "efm77av40");
    CHECK(first_candidate("disk.77D") == "efm77av40");
    CHECK(first_candidate("tape.t77") == "efm77av40");
    CHECK(first_candidate("boot.dim") == "msx68k");
    CHECK(first_candidate("SXWIN31.HDS") == "msx68k");
    CHECK(first_candidate("disk.2DD") == "msx68k");
}

void test_ambiguous_dir_hints() {
    CHECK(first_candidate("/home/user/czx/media/pc88/game.d88") == "quasi88");
    CHECK(first_candidate("/home/user/czx/media/pc98/game.d88") == "np2kai");
    CHECK(first_candidate("/home/user/czx/media/mz700/game.d88") == "emuz700");
    CHECK(first_candidate("/home/user/czx/media/fm7/game.d88") == "efm77av40");
    CHECK(first_candidate("/home/user/czx/media/fm7/game.2d") == "efm77av40");
    CHECK(first_candidate("/home/user/czx/media/fm7/tape.wav") == "efm77av40");
    CHECK(first_candidate("/home/user/czx/media/pc98/disk.xdf") == "np2kai");
    CHECK(first_candidate("/home/user/czx/media/x68000/HUMAN302.XDF") == "msx68k");
    CHECK(first_candidate("/home/user/czx/media/x68000/plin.2hd") == "msx68k");
    CHECK(first_candidate("/home/user/czx/media/pc98/game.2hd") == "np2kai");
    CHECK(first_candidate("/home/user/czx/media/msx/disk.dsk") == "bluemsx");
    CHECK(first_candidate("/home/user/czx/media/apple2/disk.dsk") == "applewin");

    // Default without dir hints
    CHECK(first_candidate("HUMAN302.XDF") == "msx68k");
    CHECK(first_candidate("disk.2hd") == "np2kai");
    CHECK(first_candidate("disk.d88") == "quasi88");
}

void test_file_content_disambiguation() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / "czx_test_core_detect";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    CHECK(fs::is_directory(tmp, ec));

    const fs::path a2_dsk = tmp / "apple_sample.dsk";
    write_bytes(a2_dsk, 143360u, nullptr, 0);
    CHECK(first_candidate(a2_dsk.string()) == "applewin");

    const fs::path msx_dsk = tmp / "msx_sample.dsk";
    write_bytes(msx_dsk, 737280u, nullptr, 0);
    CHECK(first_candidate(msx_dsk.string()) == "bluemsx");

    const fs::path c64_tap = tmp / "c64_sample.tap";
    write_bytes(c64_tap, 64u, "C64-TAPE-RAW", 12);
    CHECK(first_candidate(c64_tap.string()) == "vice_x64");

    const fs::path mz_tap = tmp / "mz_sample.tap";
    write_bytes(mz_tap, 64u, "\x00\x01\x02\x03", 4);
    CHECK(first_candidate(mz_tap.string()) == "emuz700");

    fs::remove_all(tmp, ec);
}

}  // namespace

int main() {
    test_unambiguous_extensions();
    test_ambiguous_dir_hints();
    test_file_content_disambiguation();
    std::printf("test_core_detect: all checks passed\n");
    return 0;
}
