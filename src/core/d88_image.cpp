#include "core/d88_image.hpp"

#include "video/font6x8.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace czx::core {

namespace {

constexpr uint32_t kMinD88Size = 0x2B0u;
constexpr size_t kMaxD88SubDisks = 64u;

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

uint32_t read_u32_le(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t fnv1a_64(std::string_view path, uint64_t offset, uint32_t size) noexcept {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint8_t b) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;
    };
    for (unsigned char c : path) {
        mix(static_cast<uint8_t>(c));
    }
    mix(0xFFu);
    for (int i = 0; i < 8; ++i) {
        mix(static_cast<uint8_t>((offset >> (i * 8)) & 0xFFu));
    }
    for (int i = 0; i < 4; ++i) {
        mix(static_cast<uint8_t>((size >> (i * 8)) & 0xFFu));
    }
    return h;
}

std::string decode_d88_name(const uint8_t* raw17, size_t index) {
    size_t len = 0;
    while (len < 17u && raw17[len] != '\0') {
        ++len;
    }
    while (len > 0 && raw17[len - 1u] == ' ') {
        --len;
    }
    std::string decoded = (len > 0)
                              ? czx::video::sjis_to_utf8(std::string_view(
                                    reinterpret_cast<const char*>(raw17), len))
                              : std::string();
    if (decoded.empty()) {
        decoded = "Disk " + std::to_string(index + 1u);
    }
    return decoded;
}

}  // namespace

const char* D88SubDisk::media_type_label() const noexcept {
    switch (media_type) {
        case 0x00: return "2D";
        case 0x10: return "2DD";
        case 0x20: return "2HD";
        case 0x30: return "1D";
        case 0x40: return "1DD";
        default: return "?";
    }
}

bool is_d88_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".d88" || ext == ".88d" || ext == ".d98" || ext == ".98d" ||
           ext == ".d77" || ext == ".77d";
}

bool inspect_d88(const std::string& path, std::vector<D88SubDisk>& out_disks) {
    out_disks.clear();
    if (!is_d88_ext(path)) return false;

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;

    const std::streamoff end_pos = ifs.tellg();
    if (end_pos < static_cast<std::streamoff>(kMinD88Size)) return false;
    const uint64_t file_size = static_cast<uint64_t>(end_pos);

    uint64_t offset = 0;
    while (offset < file_size && out_disks.size() < kMaxD88SubDisks) {
        if (file_size - offset < kMinD88Size) {
            return false;
        }
        uint8_t hdr[32]{};
        ifs.seekg(static_cast<std::streamoff>(offset));
        if (!ifs.read(reinterpret_cast<char*>(hdr), sizeof(hdr))) {
            return false;
        }
        const uint32_t disk_size = read_u32_le(hdr + 0x1C);
        if (disk_size < kMinD88Size || offset + disk_size > file_size) {
            return false;
        }

        D88SubDisk disk{};
        disk.index = out_disks.size();
        disk.offset = offset;
        disk.size = disk_size;
        disk.write_protected = (hdr[0x1A] != 0);
        disk.media_type = hdr[0x1B];
        disk.name_utf8 = decode_d88_name(hdr, disk.index);
        out_disks.push_back(std::move(disk));

        offset += disk_size;
    }

    return !out_disks.empty();
}

std::string extract_d88_subdisk(const std::string& src_path,
                                const D88SubDisk& disk,
                                const std::string& cache_dir) {
    if (cache_dir.empty() || disk.size < kMinD88Size) return {};

    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    if (ec) return {};

    char fname[64];
    const uint64_t h = fnv1a_64(src_path, disk.offset, disk.size);
    std::snprintf(fname, sizeof(fname), "d88_%016llx_d%zu.d88",
                  static_cast<unsigned long long>(h), disk.index);

    const fs::path dst_path = fs::path(cache_dir) / fname;
    if (fs::is_regular_file(dst_path, ec) &&
        fs::file_size(dst_path, ec) >= kMinD88Size) {
        return dst_path.string();
    }

    std::ifstream ifs(src_path, std::ios::binary);
    if (!ifs.is_open()) return {};
    ifs.seekg(static_cast<std::streamoff>(disk.offset));

    const fs::path tmp_path = fs::path(cache_dir) / (std::string(fname) + ".tmp");
    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return {};

        uint8_t buf[16384];
        uint32_t remaining = disk.size;
        while (remaining > 0) {
            const uint32_t chunk = (remaining < sizeof(buf)) ? remaining : static_cast<uint32_t>(sizeof(buf));
            if (!ifs.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(chunk))) {
                std::error_code rm_ec;
                fs::remove(tmp_path, rm_ec);
                return {};
            }
            if (!ofs.write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(chunk))) {
                std::error_code rm_ec;
                fs::remove(tmp_path, rm_ec);
                return {};
            }
            remaining -= chunk;
        }
    }

    fs::rename(tmp_path, dst_path, ec);
    if (ec) {
        std::error_code rm_ec;
        fs::remove(tmp_path, rm_ec);
        return {};
    }
    return dst_path.string();
}

}  // namespace czx::core
