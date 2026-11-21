#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace czx::core {

struct D88SubDisk {
    size_t index = 0;
    uint64_t offset = 0;
    uint32_t size = 0;
    std::string name_utf8;
    uint8_t media_type = 0;
    bool write_protected = false;

    const char* media_type_label() const noexcept;
};

bool is_d88_ext(const std::string& path);

bool inspect_d88(const std::string& path, std::vector<D88SubDisk>& out_disks);

std::string extract_d88_subdisk(const std::string& src_path,
                                const D88SubDisk& disk,
                                const std::string& cache_dir);

}  // namespace czx::core
