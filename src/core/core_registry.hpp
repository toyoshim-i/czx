#pragma once

#include "czx/core.h"
#include "core/libretro_core.hpp"

#include <memory>
#include <string>
#include <vector>

namespace czx::core {

class CoreRegistry {
public:
    static CoreRegistry create_default(const std::string& explicit_core_dir = {},
                                       const std::string& data_dir = {});

    void register_core(const czx_core_api_t* api);
    bool load_libretro_core(const std::string& so_path);

    size_t count() const noexcept { return cores_.size(); }
    const czx_core_api_t* get(size_t index) const noexcept;
    const czx_core_api_t* find_by_id(const std::string& id) const noexcept;
    int index_of(const czx_core_api_t* api) const noexcept;

private:
    std::vector<std::unique_ptr<LibretroCoreLoader>> loaders_;
    std::vector<const czx_core_api_t*> cores_;
};

}  // namespace czx::core
