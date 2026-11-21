#pragma once

#include <cstdint>
#include <utility>

namespace apple2roms {

struct ExternalRomMap {
    struct Entry {
        uint32_t first = 0;
        std::pair<const unsigned char*, unsigned int> second{nullptr, 0};
    };
    struct Iterator {
        const Entry* ptr = nullptr;
        bool operator==(const Iterator& o) const noexcept { return ptr == o.ptr; }
        bool operator!=(const Iterator& o) const noexcept { return ptr != o.ptr; }
        const Entry* operator->() const noexcept { return ptr; }
    };

    Iterator find(uint32_t id) const;
    Iterator end() const noexcept { return Iterator{nullptr}; }
};

extern const ExternalRomMap data;

}  // namespace apple2roms
