#pragma once

#include "czx/core.h"

#include <cstdint>

namespace czx::core {

struct LibretroKeyMapping {
    uint32_t retrok = 0;
    uint32_t character = 0;
    uint16_t retro_mods = 0;
    bool needs_synthetic_shift = false;
};

LibretroKeyMapping map_key_to_libretro(const czx_key_event_t& ev) noexcept;
uint16_t map_mods_to_libretro(uint32_t mods) noexcept;

}  // namespace czx::core
