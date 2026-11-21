#include "core/core_registry.hpp"

#include "core/g800_core.hpp"
#include "core/test_core.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

namespace czx::core {

namespace {

std::string executable_dir() {
    char buf[1024] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return fs::path(buf).parent_path().string();
}

int core_sort_priority(const std::string& id) {
    if (id == "applewin") return 0;
    if (id == "vice_x64") return 1;
    if (id == "vice_x64sc") return 2;
    if (id == "quasi88") return 3;
    if (id == "np2kai") return 4;
    if (id == "bluemsx") return 5;
    if (id == "emuz700") return 6;
    return 10;
}

}  // namespace

CoreRegistry CoreRegistry::create_default(const std::string& explicit_core_dir,
                                          const std::string& data_dir) {
    CoreRegistry reg;

    std::vector<fs::path> search_dirs;
    if (!explicit_core_dir.empty()) {
        search_dirs.emplace_back(explicit_core_dir);
    }
    if (const char* env_dir = std::getenv("CZX_CORES_DIR"); env_dir && env_dir[0] != '\0') {
        search_dirs.emplace_back(env_dir);
    }
    const std::string exe_dir = executable_dir();
    if (!exe_dir.empty()) {
        search_dirs.push_back(fs::path(exe_dir) / "cores");
    }
    if (!data_dir.empty()) {
        search_dirs.push_back(fs::path(data_dir) / "cores");
    }
    search_dirs.emplace_back("/usr/share/APPLaunch/share/czx/cores");

    std::vector<std::string> candidate_sos;
    std::vector<fs::path> visited_dirs;
    for (const auto& dir : search_dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        const fs::path norm = fs::weakly_canonical(dir, ec);
        if (std::find(visited_dirs.begin(), visited_dirs.end(), norm) != visited_dirs.end()) {
            continue;
        }
        visited_dirs.push_back(norm);

        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0) {
                candidate_sos.push_back(entry.path().string());
            }
        }
    }

    std::sort(candidate_sos.begin(), candidate_sos.end(), [](const std::string& a, const std::string& b) {
        const std::string sa = fs::path(a).stem().string();
        const std::string sb = fs::path(b).stem().string();
        auto strip = [](std::string s) {
            constexpr const char* kSuf = "_libretro";
            if (s.size() > 9 && s.compare(s.size() - 9, 9, kSuf) == 0) s.resize(s.size() - 9);
            return s;
        };
        const std::string ia = strip(sa);
        const std::string ib = strip(sb);
        const int pa = core_sort_priority(ia);
        const int pb = core_sort_priority(ib);
        if (pa != pb) return pa < pb;
        return ia < ib;
    });

    for (const auto& so : candidate_sos) {
        reg.load_libretro_core(so);
    }

    reg.register_core(get_g800_core_api());
    reg.register_core(get_test_core_api());
    return reg;
}

void CoreRegistry::register_core(const czx_core_api_t* api) {
    if (!api || !api->get_desc) return;
    const auto* desc = api->get_desc(api);
    if (!desc || !desc->id || find_by_id(desc->id) != nullptr) {
        return;
    }
    cores_.push_back(api);
}

bool CoreRegistry::load_libretro_core(const std::string& so_path) {
    auto loader = std::make_unique<LibretroCoreLoader>();
    if (!loader->load(so_path)) {
        return false;
    }
    if (find_by_id(loader->id()) != nullptr) {
        return false;
    }
    const czx_core_api_t* api = loader->api();
    loaders_.push_back(std::move(loader));
    cores_.push_back(api);
    return true;
}

const czx_core_api_t* CoreRegistry::get(size_t index) const noexcept {
    if (index >= cores_.size()) return nullptr;
    return cores_[index];
}

const czx_core_api_t* CoreRegistry::find_by_id(const std::string& id) const noexcept {
    for (const auto* api : cores_) {
        const auto* desc = api->get_desc(api);
        if (desc && desc->id && id == desc->id) {
            return api;
        }
    }
    return nullptr;
}

int CoreRegistry::index_of(const czx_core_api_t* api) const noexcept {
    for (size_t i = 0; i < cores_.size(); ++i) {
        if (cores_[i] == api) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace czx::core
