#include "apple2roms_data.h"
#include "resource.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace apple2roms {
namespace {

struct ResourceSpec {
    uint32_t id;
    const char* filename;
    unsigned int optional_zero_size;  // 0 = required on disk; >0 = blank buffer if absent
    bool is_bitmap;
};

constexpr std::array<ResourceSpec, 29> kResourceSpecs = {{
    // Required system & Disk II ROMs (must exist on disk; never synthesized)
    {IDR_APPLE2E_ENHANCED_ROM, "Apple2e_Enhanced.rom", 0, false},
    {IDR_APPLE2E_ENHANCED_VIDEO_ROM, "Apple2e_Enhanced_Video.rom", 0, false},
    {IDR_DISK2_16SECTOR_FW, "DISK2.rom", 0, false},
    {IDR_APPLE2E_ROM, "Apple2e.rom", 0, false},
    {IDR_APPLE2_PLUS_ROM, "Apple2_Plus.rom", 0, false},
    {IDR_APPLE2_ROM, "Apple2.rom", 0, false},
    {IDR_APPLE2_JPLUS_ROM, "Apple2_JPlus.rom", 0, false},
    {IDR_PRAVETS_82_ROM, "PRAVETS82.ROM", 0, false},
    {IDR_PRAVETS_8M_ROM, "PRAVETS8M.ROM", 0, false},
    {IDR_PRAVETS_8C_ROM, "PRAVETS8C.ROM", 0, false},
    {IDR_TK3000_2E_ROM, "TK3000e.rom", 0, false},
    {IDR_BASE_64A_ROM, "Base64A.rom", 0, false},

    // Optional secondary video ROMs & peripheral slot firmwares
    {IDR_APPLE2_VIDEO_ROM, "Apple2_Video.rom", 2048, false},
    {IDR_APPLE2_JPLUS_VIDEO_ROM, "Apple2_JPlus_Video.rom", 2048, false},
    {IDR_BASE64A_VIDEO_ROM, "Base64A_German_Video.rom", 4096, false},
    {IDR_DISK2_13SECTOR_FW, "DISK2-13sector.rom", 256, false},
    {IDR_SSC_FW, "SSC.rom", 2048, false},
    {IDR_PRINTDRVR_FW, "Parallel.rom", 256, false},
    {IDR_MOCKINGBOARD_D_FW, "Mockingboard-D.rom", 2048, false},
    {IDR_MOUSEINTERFACE_FW, "MouseInterface.rom", 2048, false},
    {IDR_THUNDERCLOCKPLUS_FW, "ThunderClockPlus.rom", 2048, false},
    {IDR_TKCLOCK_FW, "TKClock.rom", 2304, false},
    {IDR_HDDRVR_FW, "Hddrvr.bin", 256, false},
    {IDR_HDDRVR_V2_FW, "Hddrvr-v2.bin", 256, false},
    {IDR_HDC_SMARTPORT_FW, "HDC-SmartPort.bin", 256, false},
    {IDR_FREEZES_F8_ROM, "Freezes_Non-autostart_F8_Rom.rom", 2048, false},

    // Optional Pravets 1bpp BMP character sets
    {IDB_CHARSET82, "CHARSET82.bmp", 66, true},
    {IDB_CHARSET8M, "CHARSET8M.bmp", 66, true},
    {IDB_CHARSET8C, "CHARSET8C.bmp", 66, true},
}};

bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

std::vector<fs::path> search_directories() {
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("CZX_APPLE2_ROM_DIR"); env && env[0] != '\0') {
        dirs.emplace_back(env);
    }
    if (const char* data_dir = std::getenv("CZX_DATA_DIR"); data_dir && data_dir[0] != '\0') {
        dirs.emplace_back(fs::path(data_dir) / "roms" / "apple2");
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        dirs.emplace_back(fs::path(home) / "czx" / "roms" / "apple2");
    }
    return dirs;
}

bool read_file_ci(const char* target_name, std::vector<unsigned char>& out) {
    for (const auto& dir : search_directories()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
            if (!equals_ignore_case(entry.path().filename().string(), target_name)) continue;

            std::ifstream ifs(entry.path(), std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) continue;
            const auto sz = ifs.tellg();
            if (sz <= 0) continue;
            out.resize(static_cast<size_t>(sz));
            ifs.seekg(0);
            if (ifs.read(reinterpret_cast<char*>(out.data()), sz)) {
                return true;
            }
            out.clear();
        }
    }
    return false;
}

std::vector<unsigned char> make_blank_1bpp_bmp() {
    // Valid 66-byte 1-bit 1x1 BMP matching AppleWin's extractBitmapData() parser.
    std::vector<unsigned char> bmp(66, 0);
    // BITMAPFILEHEADER (14 bytes)
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = 66;   // bfSize = 66
    bmp[10] = 62;  // bfOffBits = 14 + 40 + 8 = 62
    // BITMAPINFOHEADER (40 bytes)
    bmp[14] = 40;  // biSize = 40
    bmp[18] = 1;   // biWidth = 1
    bmp[22] = 1;   // biHeight = 1
    bmp[26] = 1;   // biPlanes = 1
    bmp[28] = 1;   // biBitCount = 1
    bmp[34] = 4;   // biSizeImage = 4 (one 4-byte padded row)
    return bmp;
}

struct CachedEntry {
    std::vector<unsigned char> bytes;
    ExternalRomMap::Entry entry;
};

}  // namespace

ExternalRomMap::Iterator ExternalRomMap::find(uint32_t id) const {
    static std::mutex mu;
    static std::map<uint32_t, CachedEntry> cache;

    std::lock_guard<std::mutex> lock(mu);
    if (const auto it = cache.find(id); it != cache.end()) {
        return Iterator{&it->second.entry};
    }

    const ResourceSpec* spec = nullptr;
    for (const auto& s : kResourceSpecs) {
        if (s.id == id) {
            spec = &s;
            break;
        }
    }
    if (!spec) {
        return end();
    }

    CachedEntry loaded;
    if (!read_file_ci(spec->filename, loaded.bytes)) {
        if (spec->optional_zero_size == 0) {
            return end();
        }
        if (spec->is_bitmap) {
            loaded.bytes = make_blank_1bpp_bmp();
        } else {
            loaded.bytes.assign(spec->optional_zero_size, 0);
        }
    }

    loaded.entry.first = id;
    loaded.entry.second = {loaded.bytes.data(), static_cast<unsigned int>(loaded.bytes.size())};
    auto [inserted, _] = cache.emplace(id, std::move(loaded));
    // Re-bind pointer after moving vector into the map node.
    inserted->second.entry.second.first = inserted->second.bytes.data();
    return Iterator{&inserted->second.entry};
}

const ExternalRomMap data{};

}  // namespace apple2roms
