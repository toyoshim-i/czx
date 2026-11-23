#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include "paths.h"
#include "common.h"

namespace fs = std::filesystem;

// Provide minimal stubs referenced by src/common.cpp when compiled without _WIN32 or _USE_QT.
static std::string cpp_homedir;
static std::string my_procname;
static inline void _my_mkdir(const std::string&) {}

#define get_application_path cscp_orig_get_application_path
#define create_local_path cscp_orig_create_local_path
#define vswprintf(buf, fmt, ap) vswprintf(buf, sizeOfBuffer, fmt, ap)
#include "../../third_party/takeda-cscp-emulators/src/common.cpp"
#undef vswprintf
#undef create_local_path
#undef get_application_path

namespace {

std::string g_system_dir = ".";
std::string g_save_dir = ".";
char g_app_path[_MAX_PATH] = "./";

bool iequals(const std::string& a, const char* b) {
    const size_t blen = std::strlen(b);
    if (a.size() != blen) return false;
    for (size_t i = 0; i < blen; ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

bool ends_with_icase(const std::string& s, const char* suffix) {
    const size_t slen = std::strlen(suffix);
    if (s.size() < slen) return false;
    const size_t off = s.size() - slen;
    for (size_t i = 0; i < slen; ++i) {
        const auto ca = static_cast<unsigned char>(s[off + i]);
        const auto cb = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

std::string find_case_insensitive_in_dir(const fs::path& dir, const char* filename) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
        if (iequals(entry.path().filename().string(), filename) && entry.file_size(ec) > 0) {
            return entry.path().string();
        }
    }
    return {};
}

std::string resolve_local_file(const char* name) {
    if (!name || name[0] == '\0') {
        return g_system_dir;
    }
    if (name[0] == '/') {
        return name;
    }

    const std::string fname = fs::path(name).filename().string();
    if (iequals(fname, "USERDIC.DAT") ||
        iequals(fname, "JCOMMCARD.bin") ||
        ends_with_icase(fname, ".tmp") ||
        ends_with_icase(fname, ".txt")) {
        std::error_code ec;
        const fs::path save_sub = fs::path(g_save_dir) / "fm7";
        fs::create_directories(save_sub, ec);
        return (save_sub / fname).string();
    }

    const fs::path sys_dir(g_system_dir);
    const std::string found = find_case_insensitive_in_dir(sys_dir, name);
    if (!found.empty()) return found;
    return (sys_dir / name).string();
}

}  // namespace

void efm7_set_paths(const char* system_dir, const char* save_dir) {
    if (system_dir && system_dir[0] != '\0') {
        g_system_dir = system_dir;
        while (g_system_dir.size() > 1 && g_system_dir.back() == '/') {
            g_system_dir.pop_back();
        }
    } else {
        g_system_dir = ".";
    }

    if (save_dir && save_dir[0] != '\0') {
        g_save_dir = save_dir;
        while (g_save_dir.size() > 1 && g_save_dir.back() == '/') {
            g_save_dir.pop_back();
        }
    } else {
        g_save_dir = ".";
    }

    std::snprintf(g_app_path, sizeof(g_app_path), "%s/", g_system_dir.c_str());
}

const _TCHAR* DLL_PREFIX get_application_path() {
    return g_app_path;
}

const _TCHAR* DLL_PREFIX create_local_path(const _TCHAR* format, ...) {
    static _TCHAR file_path[8][_MAX_PATH];
    static unsigned int table_index = 0;
    unsigned int output_index = (table_index++) & 7u;

    _TCHAR file_name[_MAX_PATH];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(file_name, _MAX_PATH, format, ap);
    va_end(ap);

    const std::string resolved = resolve_local_file(file_name);
    my_tcscpy_s(file_path[output_index], _MAX_PATH, resolved.c_str());
    return file_path[output_index];
}

void DLL_PREFIX create_local_path(_TCHAR* file_path, int length, const _TCHAR* format, ...) {
    if (!file_path || length <= 0) return;
    _TCHAR file_name[_MAX_PATH];
    va_list ap;
    va_start(ap, format);
    std::vsnprintf(file_name, _MAX_PATH, format, ap);
    va_end(ap);

    const std::string resolved = resolve_local_file(file_name);
    my_tcscpy_s(file_path, static_cast<size_t>(length), resolved.c_str());
}
