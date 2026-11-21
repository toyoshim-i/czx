#include "core/d88_image.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> make_dummy_d88_disk(const std::string& sjis_name, uint8_t media_type, uint32_t size) {
    CHECK(size >= 0x2B0u);
    std::vector<uint8_t> buf(size, 0);
    const size_t n = std::min<size_t>(16u, sjis_name.size());
    std::memcpy(buf.data(), sjis_name.data(), n);
    buf[0x1B] = media_type;
    buf[0x1C] = static_cast<uint8_t>(size & 0xFFu);
    buf[0x1D] = static_cast<uint8_t>((size >> 8) & 0xFFu);
    buf[0x1E] = static_cast<uint8_t>((size >> 16) & 0xFFu);
    buf[0x1F] = static_cast<uint8_t>((size >> 24) & 0xFFu);
    buf[0x20] = 0xB0;  // track 0 offset
    buf[0x21] = 0x02;
    return buf;
}

}  // namespace

int main() {
    CHECK(czx::core::is_d88_ext("foo/ゲーム.D88"));
    CHECK(czx::core::is_d88_ext("bar.88d"));
    CHECK(!czx::core::is_d88_ext("bar.d64"));

    const fs::path tmp_dir = fs::temp_directory_path() / "czx_test_d88";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    const fs::path d88_path = tmp_dir / "テスト.d88";
    auto d1 = make_dummy_d88_disk("\x83\x56\x83\x58\x83\x65\x83\x80", 0x00, 0x300u);  // "システム" (2D)
    auto d2 = make_dummy_d88_disk("\x83\x66\x81\x5B\x83\x5E", 0x20, 0x340u);          // "データ" (2HD)
    d2[0x1A] = 0x10;
    {
        std::ofstream ofs(d88_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(d1.data()), static_cast<std::streamsize>(d1.size()));
        ofs.write(reinterpret_cast<const char*>(d2.data()), static_cast<std::streamsize>(d2.size()));
    }

    std::vector<czx::core::D88SubDisk> disks;
    CHECK(czx::core::inspect_d88(d88_path.string(), disks));
    CHECK(disks.size() == 2u);
    CHECK(disks[0].name_utf8 == "システム");
    CHECK(std::strcmp(disks[0].media_type_label(), "2D") == 0);
    CHECK(!disks[0].write_protected);
    CHECK(disks[1].name_utf8 == "データ");
    CHECK(std::strcmp(disks[1].media_type_label(), "2HD") == 0);
    CHECK(disks[1].write_protected);

    const fs::path cache_dir = tmp_dir / "cache";
    const std::string ext1 = czx::core::extract_d88_subdisk(d88_path.string(), disks[0], cache_dir.string());
    const std::string ext2 = czx::core::extract_d88_subdisk(d88_path.string(), disks[1], cache_dir.string());
    CHECK(!ext1.empty() && !ext2.empty() && ext1 != ext2);
    CHECK(fs::file_size(ext1) == 0x300u);
    CHECK(fs::file_size(ext2) == 0x340u);

    // Modifying cached sub-disk must be preserved (re-extract must not clobber).
    {
        std::ofstream ofs(ext2, std::ios::binary | std::ios::in | std::ios::out);
        ofs.seekp(0x2B0);
        const char marker[4] = {'S', 'A', 'V', 'E'};
        ofs.write(marker, 4);
    }
    const std::string ext2_again = czx::core::extract_d88_subdisk(d88_path.string(), disks[1], cache_dir.string());
    CHECK(ext2_again == ext2);
    {
        std::ifstream ifs(ext2_again, std::ios::binary);
        ifs.seekg(0x2B0);
        char marker[4]{};
        ifs.read(marker, 4);
        CHECK(std::memcmp(marker, "SAVE", 4) == 0);
    }

    // Truncated file fails cleanly.
    const fs::path bad_path = tmp_dir / "bad.d88";
    {
        std::ofstream ofs(bad_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(d1.data()), 0x2D0);
    }
    CHECK(!czx::core::inspect_d88(bad_path.string(), disks));

    fs::remove_all(tmp_dir, ec);
    return 0;
}
