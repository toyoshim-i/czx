#include "core/core_registry.hpp"

#include "core/g800_core.hpp"
#include "core/test_core.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
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

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

void append_unique(std::vector<std::string>& dst, const char* id) {
    if (!id || id[0] == '\0') return;
    if (std::find(dst.begin(), dst.end(), id) == dst.end()) {
        dst.emplace_back(id);
    }
}

bool pipe_extensions_contain(std::string_view exts, std::string_view dotless_ext) noexcept {
    if (exts.empty() || dotless_ext.empty()) return false;
    size_t pos = 0;
    while (pos < exts.size()) {
        const size_t bar = exts.find('|', pos);
        std::string_view tok = (bar == std::string_view::npos) ? exts.substr(pos) : exts.substr(pos, bar - pos);
        if (!tok.empty() && tok.front() == '.') {
            tok.remove_prefix(1);
        }
        if (tok.size() == dotless_ext.size()) {
            bool eq = true;
            for (size_t i = 0; i < tok.size(); ++i) {
                char a = tok[i];
                char b = dotless_ext[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) {
                    eq = false;
                    break;
                }
            }
            if (eq) return true;
        }
        if (bar == std::string_view::npos) break;
        pos = bar + 1;
    }
    return false;
}

int core_sort_priority(const std::string& id) {
    if (id == "applewin") return 0;
    if (id == "vice_x64") return 1;
    if (id == "vice_x64sc") return 2;
    if (id == "quasi88") return 3;
    if (id == "np2kai") return 4;
    if (id == "bluemsx") return 5;
    if (id == "emuz700") return 6;
    if (id == "efm7") return 7;
    if (id == "efm77av") return 8;
    if (id == "efm77av40") return 9;
    if (id == "msx68k") return 10;
    return 15;
}

}  // namespace

std::vector<std::string> candidate_core_ids_for_media(const std::string& media_path) {
    std::vector<std::string> out;
    if (media_path.empty()) return out;

    const fs::path p(media_path);
    const std::string ext = to_lower_copy(p.extension().string());
    if (ext.empty()) return out;

    // 1. Unambiguous single-target extensions
    if (ext == ".woz" || ext == ".do" || ext == ".po" || ext == ".nib" ||
        ext == ".2mg" || ext == ".2img" || ext == ".hdv") {
        append_unique(out, "applewin");
        return out;
    }
    if (ext == ".d64" || ext == ".g64" || ext == ".x64" || ext == ".d71" ||
        ext == ".d81" || ext == ".d82" || ext == ".prg" || ext == ".crt" ||
        ext == ".t64" || ext == ".p00") {
        append_unique(out, "vice_x64");
        append_unique(out, "vice_x64sc");
        return out;
    }
    if (ext == ".t88" || ext == ".cmt" || ext == ".88d") {
        append_unique(out, "quasi88");
        return out;
    }
    if (ext == ".fdi" || ext == ".hdm" || ext == ".nhd" || ext == ".hdi" ||
        ext == ".thd" || ext == ".nfd" || ext == ".fdd" || ext == ".d98" ||
        ext == ".98d" || ext == ".cmd") {
        append_unique(out, "np2kai");
        return out;
    }
    if (ext == ".rom" || ext == ".mx1" || ext == ".mx2" || ext == ".cas" ||
        ext == ".ri" || ext == ".sc" || ext == ".sg" || ext == ".col") {
        append_unique(out, "bluemsx");
        return out;
    }
    if (ext == ".ihx" || ext == ".hex" || ext == ".bas" || ext == ".txt") {
        append_unique(out, "g800");
        return out;
    }
    if (ext == ".d77" || ext == ".77d") {
        append_unique(out, "efm77av40");
        append_unique(out, "efm77av");
        append_unique(out, "efm7");
        return out;
    }
    if (ext == ".t77") {
        append_unique(out, "efm77av40");
        append_unique(out, "efm77av");
        append_unique(out, "efm7");
        append_unique(out, "emuz700");
        return out;
    }
    if (ext == ".mzt" || ext == ".mzf" || ext == ".m12" || ext == ".q20") {
        append_unique(out, "emuz700");
        return out;
    }
    if (ext == ".dim" || ext == ".hds" || ext == ".hdf" || ext == ".2dd") {
        append_unique(out, "msx68k");
        return out;
    }

    // 2. Ambiguous extensions: check directory path hints first
    std::vector<std::string> dir_parts;
    for (const auto& part : p.parent_path()) {
        const std::string low = to_lower_copy(part.string());
        if (!low.empty() && low != "/" && low != ".") {
            dir_parts.push_back(low);
        }
    }
    auto has_dir_hint = [&](std::initializer_list<const char*> tokens) -> bool {
        for (const auto& dp : dir_parts) {
            for (const char* tok : tokens) {
                if (dp.find(tok) != std::string::npos) return true;
            }
        }
        return false;
    };

    if (has_dir_hint({"x68000", "x68k", "msx68k"})) {
        append_unique(out, "msx68k");
    }
    if (has_dir_hint({"pc98", "np2kai"})) {
        append_unique(out, "np2kai");
    }
    if (has_dir_hint({"pc88", "quasi88"})) {
        append_unique(out, "quasi88");
    }
    if (has_dir_hint({"bluemsx"})) {
        append_unique(out, "bluemsx");
    } else {
        for (const auto& dp : dir_parts) {
            if (dp == "msx" || dp.rfind("msx-", 0) == 0 || dp.rfind("msx_", 0) == 0) {
                append_unique(out, "bluemsx");
                break;
            }
        }
    }
    if (has_dir_hint({"apple2", "applewin"})) {
        append_unique(out, "applewin");
    }
    if (has_dir_hint({"c64", "vice"})) {
        append_unique(out, "vice_x64");
        append_unique(out, "vice_x64sc");
    }
    if (has_dir_hint({"fm7", "fm77", "efm7"})) {
        append_unique(out, "efm77av40");
        append_unique(out, "efm77av");
        append_unique(out, "efm7");
    }
    if (has_dir_hint({"mz700", "emuz700"})) {
        append_unique(out, "emuz700");
    }
    if (has_dir_hint({"g800", "g850", "g815", "e200"})) {
        append_unique(out, "g800");
    }

    // 3. Format-specific disambiguation / defaults
    std::error_code ec;
    if (ext == ".dsk") {
        if (fs::is_regular_file(p, ec)) {
            const uintmax_t sz = fs::file_size(p, ec);
            if (!ec) {
                if (sz == 143360u || sz == 116480u) {
                    append_unique(out, "applewin");
                    append_unique(out, "bluemsx");
                    return out;
                }
                if (sz == 368640u || sz == 737280u) {
                    append_unique(out, "bluemsx");
                    append_unique(out, "applewin");
                    return out;
                }
            }
        }
        append_unique(out, "applewin");
        append_unique(out, "bluemsx");
    } else if (ext == ".tap") {
        bool is_c64_tap = false;
        std::ifstream ifs(p, std::ios::binary);
        if (ifs.is_open()) {
            char hdr[12]{};
            if (ifs.read(hdr, sizeof(hdr)) && std::memcmp(hdr, "C64-TAPE-RAW", 12) == 0) {
                is_c64_tap = true;
            }
        }
        if (is_c64_tap) {
            append_unique(out, "vice_x64");
            append_unique(out, "vice_x64sc");
            append_unique(out, "emuz700");
        } else {
            append_unique(out, "emuz700");
            append_unique(out, "vice_x64");
            append_unique(out, "vice_x64sc");
        }
    } else if (ext == ".xdf") {
        append_unique(out, "msx68k");
        append_unique(out, "np2kai");
    } else if (ext == ".2hd") {
        append_unique(out, "np2kai");
        append_unique(out, "msx68k");
    } else if (ext == ".d88") {
        append_unique(out, "quasi88");
        append_unique(out, "np2kai");
        append_unique(out, "emuz700");
        append_unique(out, "efm77av40");
        append_unique(out, "efm77av");
        append_unique(out, "efm7");
    } else if (ext == ".2d" || ext == ".wav") {
        append_unique(out, "emuz700");
        append_unique(out, "efm77av40");
        append_unique(out, "efm77av");
        append_unique(out, "efm7");
    } else if (ext == ".img") {
        append_unique(out, "msx68k");
        append_unique(out, "np2kai");
    }

    return out;
}

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

const czx_core_api_t* CoreRegistry::find_for_media(const std::string& media_path) const noexcept {
    if (media_path.empty()) return nullptr;
    for (const std::string& id : candidate_core_ids_for_media(media_path)) {
        if (const czx_core_api_t* api = find_by_id(id)) {
            return api;
        }
    }
    std::string ext = to_lower_copy(fs::path(media_path).extension().string());
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    if (ext.empty()) return nullptr;
    for (const auto& loader : loaders_) {
        if (loader && pipe_extensions_contain(loader->extensions(), ext)) {
            return loader->api();
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
