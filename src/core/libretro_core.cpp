#include "core/libretro_core.hpp"

#include "core/d88_image.hpp"
#include "core/libretro_keys.hpp"
#include "czx/libretro.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace czx::core {

constexpr uint32_t kTargetAudioSampleRate = 48000u;
constexpr double kTargetAudioSampleRateF = static_cast<double>(kTargetAudioSampleRate);

struct LibretroCoreLoader::Symbols {
    unsigned (*retro_api_version)(void) = nullptr;
    void (*retro_init)(void) = nullptr;
    void (*retro_deinit)(void) = nullptr;
    void (*retro_get_system_info)(retro_system_info*) = nullptr;
    void (*retro_get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*retro_set_environment)(retro_environment_t) = nullptr;
    void (*retro_set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*retro_set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*retro_set_input_poll)(retro_input_poll_t) = nullptr;
    void (*retro_set_input_state)(retro_input_state_t) = nullptr;
    void (*retro_reset)(void) = nullptr;
    void (*retro_run)(void) = nullptr;
    bool (*retro_load_game)(const retro_game_info*) = nullptr;
    void (*retro_unload_game)(void) = nullptr;
    void (*retro_set_controller_port_device)(unsigned, unsigned) = nullptr;
};

namespace {

class LibretroCoreInstance;
LibretroCoreInstance* g_active_instance = nullptr;

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

bool rom_file_exists(const fs::path& dir, const char* filename, bool case_insensitive) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    if (!case_insensitive) {
        const fs::path p = dir / filename;
        return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
        if (equals_ignore_case(entry.path().filename().string(), filename) &&
            entry.file_size(ec) > 0) {
            return true;
        }
    }
    return false;
}

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

std::string derive_core_id(const std::string& so_path) {
    std::string stem = fs::path(so_path).stem().string();
    constexpr const char* kSuffix = "_libretro";
    if (stem.size() > std::strlen(kSuffix) &&
        stem.compare(stem.size() - std::strlen(kSuffix), std::strlen(kSuffix), kSuffix) == 0) {
        stem.resize(stem.size() - std::strlen(kSuffix));
    }
    return to_lower_copy(stem);
}

std::string derive_display_name(const std::string& id, const char* lib_name) {
    if (id == "applewin") return "Apple II (AppleWin)";
    if (id == "vice_x64sc") return "C64 (VICE x64sc)";
    if (id == "vice_x64") return "C64 (VICE x64)";
    if (id == "quasi88") return "PC-8801 (QUASI88)";
    if (id == "np2kai") return "PC-9801 (NP2kai)";
    if (id == "bluemsx") return "MSX (blueMSX)";
    if (id == "emuz700") return "MZ-700 (EmuZ-700)";
    if (lib_name && lib_name[0] != '\0') return lib_name;
    return id;
}

std::string executable_dir() {
    char buf[1024] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return fs::path(buf).parent_path().string();
}

void seed_bluemsx_open_data(const fs::path& dst_root) {
    std::error_code ec;
    const bool has_cbios = fs::is_regular_file(
        dst_root / "Machines" / "MSX2+ - C-BIOS" / "cbios_main_msx2+.rom", ec);
    const bool has_db = fs::is_regular_file(dst_root / "Databases" / "msxromdb.xml", ec);
    if (has_cbios && has_db) return;

    std::vector<fs::path> candidates;
    const std::string exe_dir = executable_dir();
    if (!exe_dir.empty()) {
        candidates.push_back(fs::path(exe_dir) / "bluemsx-data");
    }
    candidates.emplace_back("/usr/share/APPLaunch/share/czx/bluemsx");

    for (const auto& src : candidates) {
        if (!fs::is_regular_file(src / "Machines" / "MSX2+ - C-BIOS" / "cbios_main_msx2+.rom", ec)) {
            continue;
        }
        constexpr auto kOpts = fs::copy_options::recursive | fs::copy_options::skip_existing;
        if (fs::is_directory(src / "Databases", ec)) {
            fs::copy(src / "Databases", dst_root / "Databases", kOpts, ec);
        }
        if (fs::is_directory(src / "Machines", ec)) {
            fs::copy(src / "Machines", dst_root / "Machines", kOpts, ec);
        }
        return;
    }
}

bool is_msx_disk_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".dsk" || ext == ".m3u";
}

bool is_floppy_disk_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".dsk" || ext == ".do" || ext == ".po" || ext == ".nib" ||
           ext == ".woz" || ext == ".2mg" || ext == ".2img" || ext == ".hdv" ||
           ext == ".d64" || ext == ".g64" || ext == ".x64" || ext == ".d71" ||
           ext == ".d81" || ext == ".d82" ||
           ext == ".d88" || ext == ".88d" || ext == ".d98" || ext == ".98d" ||
           ext == ".fdi" || ext == ".xdf" || ext == ".hdm" || ext == ".dup" ||
           ext == ".2hd" || ext == ".tfd" || ext == ".nfd" || ext == ".fdd";
}

bool is_tape_ext(const std::string& path) {
    return to_lower_copy(fs::path(path).extension().string()) == ".tap";
}

bool is_mz700_tape_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".mzt" || ext == ".mzf" || ext == ".m12" || ext == ".wav" ||
           ext == ".cas" || ext == ".tap" || ext == ".t77";
}

bool is_pc98_hdd_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".hdi" || ext == ".nhd" || ext == ".thd" || ext == ".vhd" ||
           ext == ".hdd" || ext == ".hdn" || ext == ".slh";
}

bool write_text_file(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << content;
    return ofs.good();
}

bool starts_with_ignore_case(std::string_view s, std::string_view prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    return equals_ignore_case(s.substr(0, prefix.size()), prefix);
}

std::string trim_ascii(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

std::string find_rom_filename_ci(const fs::path& dir, const char* filename) {
    std::error_code ec;
    if (!filename || filename[0] == '\0' || !fs::is_directory(dir, ec)) return {};
    if (fs::exists(dir / filename, ec) &&
        (fs::is_regular_file(dir / filename, ec) || fs::is_symlink(dir / filename, ec)) &&
        fs::file_size(dir / filename, ec) > 0) {
        return filename;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
        if (equals_ignore_case(entry.path().filename().string(), filename) &&
            entry.file_size(ec) > 0) {
            return entry.path().filename().string();
        }
    }
    return {};
}

struct BluemsxSlotRom {
    int slot = 0;
    int subslot = 0;
    int start_page = 0;
    int page_count = 0;
    int rom_type = 0;
    std::string rom_path;
    std::string in_zip;
};

struct BluemsxMachineInfo {
    bool valid_ini = false;
    int fdc_count = 0;
    bool has_disk_rom = false;
    std::vector<BluemsxSlotRom> slot_roms;
};

bool is_bluemsx_disk_rom_type(int rom_type) noexcept {
    switch (rom_type) {
        case 21:   // ROM_DISKPATCH
        case 57:   // ROM_TC8566AF
        case 63:   // ROM_MICROSOL
        case 64:   // ROM_NATIONALFDC
        case 65:   // ROM_PHILIPSFDC
        case 67:   // ROM_SVI738FDC
        case 75:   // ROM_SVI328FDC
        case 143:  // ROM_TC8566AF_TR
        case 162:  // ROM_SVI707FDC
            return true;
        default:
            return false;
    }
}

std::string resolve_bluemsx_machine_dir_name(const std::string& machine_name) {
    if (machine_name == "ColecoVision") return "COL - ColecoVision";
    if (machine_name == "Coleco (Spectravideo SVI-603)") return "COL - Spectravideo SVI-603 Coleco";
    return machine_name;
}

fs::path resolve_bluemsx_rom_path(const fs::path& system_dir, const std::string& raw_path) {
    std::string norm = raw_path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    if (norm.empty()) return {};
    if (norm.front() == '/') return fs::path(norm);
    if (starts_with_ignore_case(norm, "Machines/")) {
        return system_dir / "Machines" / norm.substr(9);
    }
    return system_dir / norm;
}

bool check_bluemsx_rom_size(int rom_type, int start_page, uintmax_t size) noexcept {
    if (size == 0 || start_page < 0 || start_page >= 8) return false;
    switch (rom_type) {
        case 24:  // ROM_KANJI
            return size == 0x20000u || size == 0x40000u;
        case 16:  // ROM_FMPAC
            return size == 0x10000u;
        case 19:  // ROM_PLAIN
            return size <= 0x10000u;
        case 39:  // ROM_BASIC
            return size <= 0x8000u;
        case 66: {  // ROM_CASPATCH
            const uintmax_t pages = size / 0x2000u;
            return pages > 0 && static_cast<uintmax_t>(start_page) + pages <= 8u;
        }
        case 20:  // ROM_NORMAL
        case 42:  // ROM_0x4000
        case 78:  // ROM_MSXMUSIC
        case 86: {  // ROM_0xC000
            const uintmax_t pages = (size + 0x1fffu) / 0x2000u;
            return pages > 0 && static_cast<uintmax_t>(start_page) + pages <= 8u;
        }
        default:
            return true;
    }
}

bool parse_bluemsx_slot_line(const std::string& line, BluemsxSlotRom& out) {
    const char* p = line.c_str();
    std::array<int, 5> nums{};
    for (int& v : nums) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') return false;
        char* end = nullptr;
        const long val = std::strtol(p, &end, 10);
        if (end == p) return false;
        v = static_cast<int>(val);
        p = end;
    }
    auto parse_quoted = [&](std::string& dst) -> bool {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '"') return false;
        ++p;
        const char* start = p;
        while (*p != '\0' && *p != '"') ++p;
        if (*p != '"') return false;
        dst.assign(start, static_cast<size_t>(p - start));
        ++p;
        return true;
    };
    std::string rom_path;
    std::string in_zip;
    if (!parse_quoted(rom_path)) return false;
    (void)parse_quoted(in_zip);

    out.slot = nums[0];
    out.subslot = nums[1];
    out.start_page = nums[2];
    out.page_count = nums[3];
    out.rom_type = nums[4];
    out.rom_path = std::move(rom_path);
    out.in_zip = std::move(in_zip);
    return true;
}

BluemsxMachineInfo inspect_bluemsx_machine_ini(const fs::path& system_dir, const std::string& machine_name) {
    BluemsxMachineInfo info{};
    const fs::path ini_path =
        system_dir / "Machines" / resolve_bluemsx_machine_dir_name(machine_name) / "config.ini";
    std::ifstream ifs(ini_path);
    if (!ifs.is_open()) return info;

    info.valid_ini = true;
    std::string section;
    std::string line;
    while (std::getline(ifs, line)) {
        const std::string trimmed = trim_ascii(line);
        if (trimmed.empty() || trimmed.front() == ';') continue;
        if (trimmed.front() == '[') {
            const auto close = trimmed.find(']');
            section = to_lower_copy((close == std::string::npos) ? trimmed.substr(1) : trimmed.substr(1, close - 1));
            continue;
        }
        if (section == "fdc") {
            const auto eq = trimmed.find('=');
            if (eq != std::string::npos &&
                equals_ignore_case(trim_ascii(trimmed.substr(0, eq)), "count")) {
                info.fdc_count = std::atoi(trim_ascii(trimmed.substr(eq + 1)).c_str());
            }
        } else if (section == "slots") {
            BluemsxSlotRom sr;
            if (!parse_bluemsx_slot_line(trimmed, sr)) continue;
            if (is_bluemsx_disk_rom_type(sr.rom_type)) {
                info.has_disk_rom = true;
            }
            if (!sr.rom_path.empty() && sr.in_zip.empty()) {
                info.slot_roms.push_back(std::move(sr));
            }
        }
    }
    return info;
}

bool validate_bluemsx_machine_roms(const fs::path& system_dir,
                                   const std::string& machine_name,
                                   std::vector<std::string>& problems,
                                   BluemsxMachineInfo* out_info = nullptr) {
    problems.clear();
    const BluemsxMachineInfo info = inspect_bluemsx_machine_ini(system_dir, machine_name);
    if (out_info) *out_info = info;

    if (!info.valid_ini) {
        problems.push_back("Machines/" + resolve_bluemsx_machine_dir_name(machine_name) + "/config.ini (missing)");
        return false;
    }

    std::error_code ec;
    for (const auto& sr : info.slot_roms) {
        const fs::path full_path = resolve_bluemsx_rom_path(system_dir, sr.rom_path);
        const bool exists = fs::is_regular_file(full_path, ec) || fs::is_symlink(full_path, ec);
        if (!exists) {
            problems.push_back(sr.rom_path);
            continue;
        }
        const uintmax_t sz = fs::file_size(full_path, ec);
        if (ec || sz == 0 || !check_bluemsx_rom_size(sr.rom_type, sr.start_page, sz)) {
            problems.push_back(sr.rom_path + " (invalid size)");
        }
    }
    return problems.empty();
}

bool generate_czx_bluemsx_config(const fs::path& system_dir,
                                 const char* base_machine,
                                 const char* czx_machine) {
    const fs::path src_ini = system_dir / "Machines" / base_machine / "config.ini";
    std::ifstream ifs(src_ini);
    if (!ifs.is_open()) return false;

    const fs::path shared_dir = system_dir / "Machines" / "Shared Roms";
    std::vector<std::string> out_lines;
    std::string section;
    size_t fdc_line_idx = std::string::npos;
    bool has_disk = false;

    std::string line;
    std::error_code ec;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string trimmed = trim_ascii(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            const auto close = trimmed.find(']');
            section = to_lower_copy((close == std::string::npos) ? trimmed.substr(1) : trimmed.substr(1, close - 1));
            out_lines.push_back(line);
            continue;
        }
        if (section == "fdc") {
            const auto eq = trimmed.find('=');
            if (eq != std::string::npos &&
                equals_ignore_case(trim_ascii(trimmed.substr(0, eq)), "count")) {
                fdc_line_idx = out_lines.size();
                out_lines.push_back("Count=0");
                continue;
            }
            out_lines.push_back(line);
            continue;
        }
        if (section == "slots") {
            if (trimmed.empty()) continue;
            BluemsxSlotRom sr;
            if (!parse_bluemsx_slot_line(trimmed, sr)) continue;
            if (sr.rom_path.empty() || !sr.in_zip.empty()) {
                out_lines.push_back(trimmed);
                continue;
            }

            std::string norm = sr.rom_path;
            std::replace(norm.begin(), norm.end(), '\\', '/');
            std::string actual_rel;
            fs::path resolved_file;
            int eff_type = sr.rom_type;

            constexpr std::string_view kSharedPrefix = "Machines/Shared Roms/";
            if (starts_with_ignore_case(norm, kSharedPrefix)) {
                const std::string fname = norm.substr(kSharedPrefix.size());
                std::string actual = find_rom_filename_ci(shared_dir, fname.c_str());
                if (actual.empty() && (sr.rom_type == 57 || sr.rom_type == 65)) {
                    const char* alt_name = (sr.rom_type == 57) ? "PHILIPSDISK.rom" : "PANASONICDISK.rom";
                    const int alt_type = (sr.rom_type == 57) ? 65 : 57;
                    actual = find_rom_filename_ci(shared_dir, alt_name);
                    if (!actual.empty()) eff_type = alt_type;
                }
                if (!actual.empty()) {
                    actual_rel = "Machines/Shared Roms/" + actual;
                    resolved_file = shared_dir / actual;
                }
            } else {
                const fs::path candidate = resolve_bluemsx_rom_path(system_dir, norm);
                const std::string actual = find_rom_filename_ci(
                    candidate.parent_path(), candidate.filename().string().c_str());
                if (!actual.empty()) {
                    resolved_file = candidate.parent_path() / actual;
                    actual_rel = fs::path(norm).parent_path().empty()
                                     ? actual
                                     : (fs::path(norm).parent_path() / actual).string();
                }
            }

            if (resolved_file.empty()) continue;
            const uintmax_t sz = fs::file_size(resolved_file, ec);
            if (ec || !check_bluemsx_rom_size(eff_type, sr.start_page, sz)) continue;

            if (is_bluemsx_disk_rom_type(eff_type)) {
                has_disk = true;
            }
            out_lines.push_back(std::to_string(sr.slot) + " " + std::to_string(sr.subslot) + " " +
                                std::to_string(sr.start_page) + " " + std::to_string(sr.page_count) + " " +
                                std::to_string(eff_type) + " \"" + actual_rel + "\" \"\"");
            continue;
        }
        out_lines.push_back(line);
    }

    if (fdc_line_idx != std::string::npos) {
        out_lines[fdc_line_idx] = has_disk ? "Count=2" : "Count=0";
    }

    std::string body;
    for (const auto& l : out_lines) {
        body += l;
        body += '\n';
    }
    const fs::path dst_ini = system_dir / "Machines" / czx_machine / "config.ini";
    if (!write_text_file(dst_ini, body)) return false;

    std::vector<std::string> problems;
    BluemsxMachineInfo check_info{};
    return validate_bluemsx_machine_roms(system_dir, czx_machine, problems, &check_info) &&
           !check_info.slot_roms.empty();
}

uint16_t rgb1555_to_rgb565(uint16_t p) noexcept {
    const uint16_t r5 = static_cast<uint16_t>((p >> 10) & 0x1Fu);
    const uint16_t g5 = static_cast<uint16_t>((p >> 5) & 0x1Fu);
    const uint16_t b5 = static_cast<uint16_t>(p & 0x1Fu);
    const uint16_t g6 = static_cast<uint16_t>((g5 << 1) | (g5 >> 4));
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

class LibretroCoreInstance {
public:
    LibretroCoreInstance(LibretroCoreLoader* loader, const czx_host_t* host, std::string extensions)
        : loader_(loader), need_fullpath_(loader->need_fullpath()), extensions_(std::move(extensions)) {
        if (host) host_ = *host;
        is_applewin_ = (loader_->id().find("applewin") != std::string::npos);
        is_vice_ = (loader_->id().find("vice") != std::string::npos);
        is_quasi88_ = (loader_->id().find("quasi88") != std::string::npos);
        is_np2kai_ = (loader_->id().find("np2kai") != std::string::npos);
        is_bluemsx_ = (loader_->id().find("bluemsx") != std::string::npos);
        is_emuz700_ = (loader_->id().find("emuz700") != std::string::npos);
        if (const char* dd = std::getenv("CZX_DATA_DIR"); dd && dd[0] != '\0') {
            set_data_dir(dd);
        } else if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
            set_data_dir((fs::path(home) / "czx").string());
        } else {
            set_data_dir("/tmp/czx");
        }
    }

    ~LibretroCoreInstance() {
        teardown_core();
        if (g_active_instance == this) {
            g_active_instance = nullptr;
        }
    }

    bool init() {
        if (host_.initial_media && host_.initial_media[0] != '\0') {
            const std::string init_path = host_.initial_media;
            if (get_drive_count() >= 2u && (is_quasi88_ || is_np2kai_) && is_d88_ext(init_path)) {
                std::vector<D88SubDisk> disks;
                if (inspect_d88(init_path, disks) && disks.size() >= 2u) {
                    const std::string cache_dir = (fs::path(save_dir_) / "d88_cache").string();
                    const std::string p0 = extract_d88_subdisk(init_path, disks[0], cache_dir);
                    const std::string p1 = extract_d88_subdisk(init_path, disks[1], cache_dir);
                    if (!p0.empty() && !p1.empty()) {
                        return load_dual_media(p0.c_str(), disks[0].name_utf8.c_str(),
                                               p1.c_str(), disks[1].name_utf8.c_str());
                    }
                }
            }
            if (!start_core(init_path.c_str())) {
                return false;
            }
            const std::string lbl = fs::path(init_path).filename().string();
            if (is_np2kai_ && is_pc98_hdd_ext(init_path)) {
                hdd_path_ = init_path;
                drive_labels_[0] = lbl;
            } else {
                drive_paths_[0] = init_path;
                drive_labels_[0] = lbl;
            }
            return true;
        }
        return start_core(nullptr);
    }

    void set_data_dir(const std::string& dir) {
        data_dir_ = dir;
        std::error_code ec;
        if (is_applewin_) {
            system_dir_ = (fs::path(data_dir_) / "roms" / "apple2").string();
            setenv("CZX_APPLE2_ROM_DIR", system_dir_.c_str(), 1);
            fs::create_directories(system_dir_, ec);
        } else if (is_vice_) {
            system_dir_ = (fs::path(data_dir_) / "roms").string();
            fs::create_directories(fs::path(system_dir_) / "vice" / "C64", ec);
            fs::create_directories(fs::path(system_dir_) / "vice" / "DRIVES", ec);
        } else if (is_quasi88_) {
            system_dir_ = (fs::path(data_dir_) / "roms").string();
            fs::create_directories(fs::path(system_dir_) / "quasi88", ec);
        } else if (is_np2kai_) {
            system_dir_ = (fs::path(data_dir_) / "roms").string();
            fs::create_directories(fs::path(system_dir_) / "np2kai", ec);
        } else if (is_bluemsx_) {
            system_dir_ = (fs::path(data_dir_) / "roms" / "bluemsx").string();
            fs::create_directories(fs::path(system_dir_) / "Machines" / "Shared Roms", ec);
            fs::create_directories(fs::path(system_dir_) / "Databases", ec);
            seed_bluemsx_open_data(fs::path(system_dir_));
        } else if (is_emuz700_) {
            system_dir_ = (fs::path(data_dir_) / "roms" / "mz700").string();
            fs::create_directories(system_dir_, ec);
        } else {
            system_dir_ = (fs::path(data_dir_) / "roms").string();
            fs::create_directories(system_dir_, ec);
        }
        save_dir_ = (fs::path(data_dir_) / "saves").string();
        fs::create_directories(save_dir_, ec);
    }

    void reset(bool) {
        g_active_instance = this;
        clear_held_inputs();
        clear_leds();
        if (loader_->symbols().retro_reset) {
            loader_->symbols().retro_reset();
        }
    }

    bool load_media(const std::string& path) {
        g_active_instance = this;
        if (path.empty()) return false;

        // Hot-swap floppy images when already running and core exposes disk control.
        if (has_media_ && has_disk_control() &&
            is_floppy_disk_ext(media_path_) && is_floppy_disk_ext(path)) {
            if (hot_swap_disk(path)) {
                media_path_ = path;
                media_label_ = fs::path(path).filename().string();
                ejected_ = false;
                drive_paths_[0] = path;
                drive_labels_[0] = media_label_;
                return true;
            }
        }

        if (is_vice_ || is_quasi88_ || is_np2kai_ || is_bluemsx_ || is_emuz700_) {
            const bool ok = reload_core_with_media(path);
            if (ok) {
                drive_paths_[0] = path;
                drive_labels_[0] = media_label_;
            }
            return ok;
        }

        if (!loader_->is_library_open()) {
            return start_core(path.c_str());
        }

        const auto& syms = loader_->symbols();
        clear_held_inputs();
        if (game_loaded_ && syms.retro_unload_game) {
            syms.retro_unload_game();
            game_loaded_ = false;
            has_media_ = false;
            media_label_ = "None";
        }

        std::vector<uint8_t> buf;
        retro_game_info info{};
        info.path = path.c_str();
        if (!need_fullpath_) {
            std::ifstream ifs(path, std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) {
                log(3, ("failed to open media: " + path).c_str());
                return false;
            }
            const auto sz = ifs.tellg();
            if (sz > 0) {
                buf.resize(static_cast<size_t>(sz));
                ifs.seekg(0);
                ifs.read(reinterpret_cast<char*>(buf.data()), sz);
            }
            info.data = buf.data();
            info.size = buf.size();
        }

        if (!syms.retro_load_game(&info)) {
            log(3, ("retro_load_game failed: " + path).c_str());
            if (support_no_game_ && syms.retro_load_game(nullptr)) {
                game_loaded_ = true;
                refresh_av_info();
            }
            return false;
        }

        game_loaded_ = true;
        has_media_ = true;
        ejected_ = false;
        media_path_ = path;
        media_label_ = fs::path(path).filename().string();
        refresh_av_info();
        return true;
    }

    bool eject_media() {
        g_active_instance = this;
        if ((is_vice_ && has_media_ && is_tape_ext(media_path_)) ||
            (is_emuz700_ && has_media_ && is_mz700_tape_ext(media_path_))) {
            pulsed_retrok_ = RETROK_KP_PLUS;
            pulsed_frames_ = 2;
            pulsed_emitted_ = false;
            return true;
        }
        if (!has_disk_control()) {
            return false;
        }
        const bool next_eject = !ejected_;
        if (disk_set_eject_(next_eject)) {
            ejected_ = next_eject;
            if (ejected_) {
                media_label_ = "(Ejected)";
            } else if (!media_path_.empty()) {
                media_label_ = fs::path(media_path_).filename().string();
            }
            return true;
        }
        return false;
    }

    void run_frame() {
        g_active_instance = this;
        if (!game_loaded_ || !loader_->is_library_open()) return;
        audio_out_frame_.clear();
        if (pulsed_frames_ > 0 && pulsed_retrok_ < retro_key_down_.size() && !pulsed_emitted_) {
            emit_retro_key(true, pulsed_retrok_, 0, RETROKMOD_NONE);
            pulsed_emitted_ = true;
        }
        loader_->symbols().retro_run();
        if (pulsed_frames_ > 0) {
            --pulsed_frames_;
            if (pulsed_frames_ == 0) {
                if (pulsed_retrok_ < retro_key_down_.size()) {
                    emit_retro_key(false, pulsed_retrok_, 0, RETROKMOD_NONE);
                }
                pulsed_retrok_ = RETROK_UNKNOWN;
                pulsed_emitted_ = false;
            }
        }
        if (!audio_out_frame_.empty() && host_.audio_samples) {
            host_.audio_samples(host_.user_data, audio_out_frame_.data(), audio_out_frame_.size() / 2u);
        }
    }

    void on_key(const czx_key_event_t& ev) {
        g_active_instance = this;
        const size_t key_idx = static_cast<size_t>(ev.key);
        if (key_idx >= active_keys_.size()) return;

        if (ev.pressed) {
            if (ev.key == CZX_KEY_LSHIFT || ev.key == CZX_KEY_RSHIFT) {
                phys_shift_held_ = true;
            }
            const auto mapped = map_key_to_libretro(ev);
            if (mapped.retrok == RETROK_UNKNOWN) return;

            if (!ev.repeat && !active_keys_[key_idx].down) {
                if (mapped.needs_synthetic_shift) {
                    if (synthetic_shift_count_ == 0 && !phys_shift_held_) {
                        emit_retro_key(true, RETROK_LSHIFT, 0, RETROKMOD_SHIFT);
                    }
                    ++synthetic_shift_count_;
                }
                active_keys_[key_idx] = HeldKey{true, mapped.retrok, mapped.needs_synthetic_shift};
            }
            emit_retro_key(true, mapped.retrok, mapped.character, mapped.retro_mods);
        } else {
            if (ev.key == CZX_KEY_LSHIFT || ev.key == CZX_KEY_RSHIFT) {
                phys_shift_held_ = false;
            }
            const HeldKey held = active_keys_[key_idx];
            uint32_t retrok = held.down ? held.retrok : map_key_to_libretro(ev).retrok;
            if (retrok != RETROK_UNKNOWN) {
                emit_retro_key(false, retrok, 0, map_mods_to_libretro(ev.mods));
            }
            if (held.down) {
                active_keys_[key_idx] = HeldKey{};
                if (held.used_synth_shift && synthetic_shift_count_ > 0) {
                    --synthetic_shift_count_;
                    if (synthetic_shift_count_ == 0 && !phys_shift_held_) {
                        emit_retro_key(false, RETROK_LSHIFT, 0, RETROKMOD_NONE);
                    }
                }
            }
        }
    }

    void on_joystick(uint32_t port, uint32_t buttons) {
        if (port < joy_state_.size()) {
            joy_state_[port] = buttons;
        }
    }

    void on_mouse(int32_t dx, int32_t dy, uint32_t buttons) {
        mouse_pending_dx_ += dx;
        mouse_pending_dy_ += dy;
        mouse_buttons_ = buttons;
    }

    void latch_mouse_poll() {
        mouse_latched_dx_ = static_cast<int16_t>(std::clamp<int32_t>(mouse_pending_dx_, -32768, 32767));
        mouse_latched_dy_ = static_cast<int16_t>(std::clamp<int32_t>(mouse_pending_dy_, -32768, 32767));
        mouse_pending_dx_ = 0;
        mouse_pending_dy_ = 0;
    }

    bool set_option(const char* key, const char* value) {
        if (!key || !value) return false;
        options_[key] = value;
        option_dirty_ = true;
        return true;
    }

    const char* get_extensions() const noexcept {
        return extensions_.empty() ? nullptr : extensions_.c_str();
    }

    const char* get_media_label() const noexcept {
        return media_label_.c_str();
    }

    uint32_t get_drive_count() const noexcept {
        return (is_quasi88_ || is_np2kai_ || is_applewin_) ? 2u : 1u;
    }

    const char* get_drive_name(uint32_t drive) const noexcept {
        if (get_drive_count() < 2u) return "Media";
        if (is_np2kai_) return (drive == 0u) ? "FDD 1" : "FDD 2";
        return (drive == 0u) ? "Drive 1" : "Drive 2";
    }

    const char* get_drive_media_label(uint32_t drive) const noexcept {
        if (get_drive_count() < 2u) return media_label_.c_str();
        return (drive < drive_labels_.size()) ? drive_labels_[drive].c_str() : "Empty";
    }

    bool load_dual_media(const char* p0, const char* l0, const char* p1, const char* l1) {
        g_active_instance = this;
        if (!p0 || p0[0] == '\0') return false;
        if (!p1 || p1[0] == '\0') {
            return load_media_in_drive(0u, p0, l0);
        }
        if (get_drive_count() < 2u) {
            return load_media_in_drive(0u, p0, l0);
        }

        const std::string path0 = p0;
        const std::string path1 = p1;
        const std::string lbl0 = (l0 && l0[0] != '\0') ? l0 : fs::path(path0).filename().string();
        const std::string lbl1 = (l1 && l1[0] != '\0') ? l1 : fs::path(path1).filename().string();

        drive_paths_[0] = path0;
        drive_labels_[0] = lbl0;
        drive_paths_[1] = path1;
        drive_labels_[1] = lbl1;

        const std::string target = build_dual_boot_file();
        return !target.empty() && reload_core_with_media(target);
    }

    bool load_media_in_drive(uint32_t drive, const std::string& path, const char* display_label) {
        g_active_instance = this;
        if (path.empty()) return false;

        const std::string lbl = (display_label && display_label[0] != '\0')
                                    ? std::string(display_label)
                                    : fs::path(path).filename().string();
        if (get_drive_count() < 2u || drive >= 2u) {
            const bool ok = load_media(path);
            if (ok) {
                media_label_ = lbl;
                drive_paths_[0] = path;
                drive_labels_[0] = lbl;
            }
            return ok;
        }

        const std::string ext = to_lower_copy(fs::path(path).extension().string());

        if (is_quasi88_) {
            const bool non_floppy = (ext == ".cmt" || ext == ".t88" || ext == ".m3u" || ext == ".zip");
            if (non_floppy) {
                drive_paths_[1].clear();
                drive_labels_[1] = "Empty";
                drive_paths_[0] = path;
                drive_labels_[0] = lbl;
                return reload_core_with_media(path);
            }
            drive_paths_[drive] = path;
            drive_labels_[drive] = lbl;
            if (!drive_paths_[0].empty() && !drive_paths_[1].empty()) {
                const std::string target = build_dual_boot_file();
                return !target.empty() && reload_core_with_media(target);
            }
            if (!drive_paths_[0].empty()) {
                return reload_core_with_media(drive_paths_[0]);
            }
            return true;
        }

        if (is_np2kai_) {
            if (ext == ".cmd") {
                drive_paths_[1].clear();
                drive_labels_[1] = "Empty";
                hdd_path_.clear();
                drive_paths_[0] = path;
                drive_labels_[0] = lbl;
                return reload_core_with_media(path);
            }
            if (is_pc98_hdd_ext(path)) {
                hdd_path_ = path;
                if (drive_paths_[0].empty()) {
                    drive_labels_[0] = lbl;
                }
                const std::string target = build_np2kai_boot_file();
                return !target.empty() && reload_core_with_media(target);
            }
            drive_paths_[drive] = path;
            drive_labels_[drive] = lbl;
            if (has_media_ && has_disk_control()) {
                options_["np2kai_drive"] = (drive == 0u) ? "FDD1" : "FDD2";
                option_dirty_ = true;
                if (hot_swap_disk(path)) {
                    if (drive == 0u) {
                        media_path_ = path;
                        media_label_ = lbl;
                    }
                    ejected_ = false;
                    return true;
                }
            }
            if (!drive_paths_[0].empty() || !hdd_path_.empty()) {
                const std::string target = build_np2kai_boot_file();
                return !target.empty() && reload_core_with_media(target);
            }
            return true;
        }

        if (is_applewin_) {
            const bool non_floppy = (ext == ".m3u" || ext == ".yaml" || ext == ".zip" || ext == ".hdv");
            if (non_floppy) {
                drive_paths_[1].clear();
                drive_labels_[1] = "Empty";
                drive_paths_[0] = path;
                drive_labels_[0] = lbl;
                return reload_core_with_media(path);
            }
            drive_paths_[drive] = path;
            drive_labels_[drive] = lbl;
            if (!drive_paths_[0].empty() && !drive_paths_[1].empty()) {
                const std::string target = build_dual_boot_file();
                return !target.empty() && reload_core_with_media(target);
            }
            if (drive == 0u) {
                const bool ok = load_media(path);
                if (ok) {
                    media_label_ = lbl;
                    drive_labels_[0] = lbl;
                }
                return ok;
            }
            return true;
        }

        return false;
    }

    bool eject_drive(uint32_t drive) {
        g_active_instance = this;
        if (get_drive_count() < 2u) {
            return eject_media();
        }
        if (drive >= 2u) return false;

        if (drive == 0u && is_np2kai_ && drive_paths_[0].empty() && !hdd_path_.empty()) {
            hdd_path_.clear();
        }
        drive_paths_[drive].clear();
        drive_labels_[drive] = "Empty";

        if (is_np2kai_) {
            if (!drive_paths_[0].empty() || !hdd_path_.empty()) {
                const std::string target = build_np2kai_boot_file();
                return !target.empty() && reload_core_with_media(target);
            }
            return !has_media_ || reload_core_with_media("");
        }

        if (!drive_paths_[0].empty()) {
            return reload_core_with_media(drive_paths_[0]);
        }
        return !has_media_ || reload_core_with_media("");
    }

    bool get_activity(czx_activity_t* out) const noexcept {
        if (!out) return false;
        out->flags = 0u;
        out->tape_percent = -1;
        out->drive_mask = 0u;

        if (led_tape_ != 0) {
            if (is_emuz700_ && led_tape_ >= 100 && led_tape_ <= 200) {
                out->flags |= CZX_ACTIVITY_TAPE_READ;
                out->tape_percent = static_cast<int8_t>(led_tape_ - 100);
            } else if (is_emuz700_ && led_tape_ >= 300 && led_tape_ <= 400) {
                out->flags |= CZX_ACTIVITY_TAPE_WRITE;
                out->tape_percent = static_cast<int8_t>(led_tape_ - 300);
            } else {
                out->flags |= CZX_ACTIVITY_TAPE_READ;
                out->tape_percent = -1;
            }
        }
        if (led_qd_ != 0) {
            out->flags |= CZX_ACTIVITY_QD;
        }
        if (led_fdd_ != 0) {
            out->flags |= CZX_ACTIVITY_DISK;
            if (is_emuz700_) {
                out->drive_mask = static_cast<uint8_t>(led_fdd_ & 0x0F);
            }
        }
        return out->flags != 0u;
    }

private:
    struct HeldKey {
        bool down = false;
        uint32_t retrok = RETROK_UNKNOWN;
        bool used_synth_shift = false;
    };

    LibretroCoreLoader* loader_;
    czx_host_t host_{};
    bool need_fullpath_ = false;
    retro_system_av_info av_info_{};
    std::string extensions_;
    bool is_applewin_ = false;
    bool is_vice_ = false;
    bool is_quasi88_ = false;
    bool is_np2kai_ = false;
    bool is_bluemsx_ = false;
    bool is_emuz700_ = false;

    std::string data_dir_;
    std::string system_dir_;
    std::string save_dir_;
    std::string media_path_;
    std::string media_label_ = "None";
    std::array<std::string, 2> drive_paths_;
    std::array<std::string, 2> drive_labels_{{"Empty", "Empty"}};
    std::string hdd_path_;

    bool reload_core_with_media(const std::string& target) {
        teardown_core();
        if (start_core(target.empty() ? nullptr : target.c_str())) {
            return true;
        }
        const std::string err = loader_->last_error() ? loader_->last_error() : "Failed to load media";
        (void)start_core(nullptr);
        loader_->set_last_error(err);
        return false;
    }

    std::string build_np2kai_boot_file() const {
        std::string cmd = "np2kai";
        if (!drive_paths_[0].empty()) {
            cmd += " \"" + drive_paths_[0] + "\"";
            if (!drive_paths_[1].empty()) {
                cmd += " \"" + drive_paths_[1] + "\"";
            }
        }
        if (!hdd_path_.empty()) {
            cmd += " \"" + hdd_path_ + "\"";
        }
        cmd += "\n";
        const fs::path p = fs::path(save_dir_) / "czx_np2kai.cmd";
        return write_text_file(p, cmd) ? p.string() : std::string{};
    }

    std::string build_dual_boot_file() const {
        if (is_np2kai_) {
            return build_np2kai_boot_file();
        }
        const std::string fname = is_applewin_ ? "czx_applewin (MD).m3u" : "czx_quasi88.m3u";
        const fs::path p = fs::path(save_dir_) / fname;
        const std::string body = drive_paths_[0] + "\n" + drive_paths_[1] + "\n";
        return write_text_file(p, body) ? p.string() : std::string{};
    }

    bool initialized_ = false;
    bool shutdown_requested_ = false;
    bool support_no_game_ = false;
    bool game_loaded_ = false;
    bool has_media_ = false;
    bool ejected_ = false;
    bool option_dirty_ = false;

    retro_pixel_format pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555;
    retro_keyboard_event_t keyboard_cb_ = nullptr;
    retro_disk_control_callback disk_ctrl_{};
    retro_disk_control_ext_callback disk_ctrl_ext_{};
    bool has_disk_ctrl_ = false;
    bool has_disk_ctrl_ext_ = false;

    std::map<std::string, std::string> options_;
    std::array<uint32_t, 2> joy_state_{0, 0};
    int32_t mouse_pending_dx_ = 0;
    int32_t mouse_pending_dy_ = 0;
    int16_t mouse_latched_dx_ = 0;
    int16_t mouse_latched_dy_ = 0;
    uint32_t mouse_buttons_ = 0;
    std::array<HeldKey, 256> active_keys_{};
    std::array<bool, RETROK_LAST> retro_key_down_{};
    bool phys_shift_held_ = false;
    int synthetic_shift_count_ = 0;
    uint32_t pulsed_retrok_ = RETROK_UNKNOWN;
    int pulsed_frames_ = 0;
    bool pulsed_emitted_ = false;
    int led_fdd_ = 0;
    int led_tape_ = 0;
    int led_qd_ = 0;

    void clear_leds() noexcept {
        led_fdd_ = 0;
        led_tape_ = 0;
        led_qd_ = 0;
    }

    void handle_led_state(int led, int state) noexcept {
        if (led == 1) {
            led_fdd_ = state;
        } else if (led == 2) {
            led_tape_ = state;
        } else if (led == 3) {
            led_qd_ = state;
        }
    }

    std::vector<uint16_t> cached_16_;
    std::vector<uint32_t> cached_32_;
    uint16_t cached_w_ = 0;
    uint16_t cached_h_ = 0;
    uint32_t cached_pitch_ = 0;
    czx_pixel_format_t cached_fmt_ = CZX_PIXEL_RGB565;

    std::vector<int16_t> audio_out_frame_;
    double audio_resample_pos_ = 0.0;
    int16_t audio_prev_l_ = 0;
    int16_t audio_prev_r_ = 0;
    bool audio_has_prev_ = false;

    std::string detect_default_bluemsx_machine() const {
        const fs::path sys_dir(system_dir_);
        const fs::path machines = sys_dir / "Machines";
        const fs::path shared = machines / "Shared Roms";
        std::vector<std::string> problems;

        // 1. Evaluate MSX tiers from highest to lowest (complete stock profile,
        //    else auto-generated partial-ROM profile using present Shared Roms).
        auto has_valid_shared_rom = [&](const char* fname, int rom_type, int start_page) -> bool {
            const std::string actual = find_rom_filename_ci(shared, fname);
            if (actual.empty()) return false;
            std::error_code ec;
            const uintmax_t sz = fs::file_size(shared / actual, ec);
            return !ec && check_bluemsx_rom_size(rom_type, start_page, sz);
        };
        std::error_code ec;

        if (validate_bluemsx_machine_roms(sys_dir, "MSX2+", problems)) {
            return "MSX2+";
        }
        if (has_valid_shared_rom("MSX2P.rom", 66, 0) &&
            has_valid_shared_rom("MSX2PEXT.rom", 20, 0) &&
            fs::is_regular_file(machines / "MSX2+" / "config.ini", ec) &&
            generate_czx_bluemsx_config(sys_dir, "MSX2+", "CZX - MSX2+")) {
            return "CZX - MSX2+";
        }
        if (validate_bluemsx_machine_roms(sys_dir, "MSX2", problems)) {
            return "MSX2";
        }
        if (has_valid_shared_rom("MSX2.rom", 66, 0) &&
            has_valid_shared_rom("MSX2EXT.rom", 20, 0) &&
            fs::is_regular_file(machines / "MSX2" / "config.ini", ec) &&
            generate_czx_bluemsx_config(sys_dir, "MSX2", "CZX - MSX2")) {
            return "CZX - MSX2";
        }
        if (validate_bluemsx_machine_roms(sys_dir, "MSX", problems)) {
            return "MSX";
        }
        if (has_valid_shared_rom("MSX.rom", 66, 0) &&
            fs::is_regular_file(machines / "MSX" / "config.ini", ec) &&
            generate_czx_bluemsx_config(sys_dir, "MSX", "CZX - MSX")) {
            return "CZX - MSX";
        }

        // 3. Complete user-populated specific machine directory (e.g. FS-A1WSX, HB-F1XDJ).
        std::vector<std::pair<int, std::string>> candidates;
        if (fs::is_directory(machines, ec)) {
            for (const auto& entry : fs::directory_iterator(machines, ec)) {
                if (!entry.is_directory(ec)) continue;
                const std::string name = entry.path().filename().string();
                if (!starts_with_ignore_case(name, "MSX")) continue;
                if (name.find("C-BIOS") != std::string::npos ||
                    starts_with_ignore_case(name, "CZX - ")) {
                    continue;
                }
                if (!validate_bluemsx_machine_roms(sys_dir, name, problems)) continue;
                int rank = 3;
                if (starts_with_ignore_case(name, "MSXturboR")) rank = 0;
                else if (starts_with_ignore_case(name, "MSX2+")) rank = 1;
                else if (starts_with_ignore_case(name, "MSX2")) rank = 2;
                candidates.emplace_back(rank, name);
            }
        }
        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end());
            return candidates.front().second;
        }

        return "MSX2+ - C-BIOS";
    }

    bool check_required_roms(std::string& err_out) const {
        if (is_applewin_) {
            static constexpr std::array<const char*, 3> kAppleRoms = {
                "Apple2e_Enhanced.rom",
                "Apple2e_Enhanced_Video.rom",
                "DISK2.rom",
            };
            std::vector<const char*> missing;
            const fs::path dir(system_dir_);
            for (const char* f : kAppleRoms) {
                if (!rom_file_exists(dir, f, true)) {
                    missing.push_back(f);
                }
            }
            if (!missing.empty()) {
                err_out = "Place Apple II ROMs in:\n" + system_dir_ + "/\nMissing:";
                for (const char* f : missing) {
                    err_out += "\n  ";
                    err_out += f;
                }
                return false;
            }
        } else if (is_vice_) {
            static constexpr std::array<std::pair<const char*, const char*>, 4> kViceRoms = {{
                {"C64", "kernal-901227-03.bin"},
                {"C64", "basic-901226-01.bin"},
                {"C64", "chargen-901225-01.bin"},
                {"DRIVES", "dos1541ii-251968-03.bin"},
            }};
            std::vector<std::string> missing;
            const fs::path vice_dir = fs::path(system_dir_) / "vice";
            for (const auto& [sub, f] : kViceRoms) {
                if (!rom_file_exists(vice_dir / sub, f, false)) {
                    missing.push_back(std::string(sub) + "/" + f);
                }
            }
            if (!missing.empty()) {
                err_out = "Place C64 ROMs in:\n" + vice_dir.string() + "/\nMissing:";
                for (const auto& m : missing) {
                    err_out += "\n  " + m;
                }
                return false;
            }
        } else if (is_quasi88_) {
            const fs::path q88_dir = fs::path(system_dir_) / "quasi88";
            if (!rom_file_exists(q88_dir, "N88.ROM", true)) {
                log(1, ("quasi88: N88.ROM not found in " + q88_dir.string() +
                        "; using built-in pseudo BIOS (optional real PC-88 ROMs: N88.ROM, N88_0..3.ROM, N88N.ROM, DISK.ROM, FONT.ROM, KANJI1.ROM)").c_str());
            }
        } else if (is_np2kai_) {
            const fs::path np2_dir = fs::path(system_dir_) / "np2kai";
            if (!rom_file_exists(np2_dir, "bios.rom", true)) {
                log(1, ("np2kai: bios.rom not found in " + np2_dir.string() +
                        "; using built-in HLE BIOS (optional real PC-98 ROMs: bios.rom, itf.rom, sound.rom, font.rom/font.bmp)").c_str());
            }
        } else if (is_bluemsx_) {
            const fs::path cbios_dir = fs::path(system_dir_) / "Machines" / "MSX2+ - C-BIOS";
            const bool has_real = (detect_default_bluemsx_machine() != "MSX2+ - C-BIOS");
            const bool has_cbios = rom_file_exists(cbios_dir, "config.ini", false) &&
                                   rom_file_exists(cbios_dir, "cbios_main_msx2+.rom", false);
            if (!has_real && !has_cbios) {
                err_out = "Place blueMSX Machines/ and Databases/ in:\n" + system_dir_ +
                          "/\n(Run install-on-device.sh to install open-source C-BIOS & Databases)";
                return false;
            }
        } else if (is_emuz700_) {
            static constexpr std::array<const char*, 7> kMzIplAliases = {
                "IPL.ROM", "1Z-009A.ROM", "1Z-009B.ROM",
                "1Z009A.ROM", "1Z009B.ROM", "MZ700IPL.ROM", "NEWMON7.ROM",
            };
            static constexpr std::array<const char*, 6> kMzFontAliases = {
                "FONT.ROM", "MZ700FON.JP", "MZ700FON.DAT",
                "MZ700FON.ROM", "CGROM.ROM", "MZ700FONT.ROM",
            };
            const fs::path mz_dir(system_dir_);
            auto any_exists = [&](const auto& list) {
                for (const char* f : list) {
                    if (rom_file_exists(mz_dir, f, true)) return true;
                }
                return false;
            };
            const bool has_ipl = any_exists(kMzIplAliases);
            const bool has_font = any_exists(kMzFontAliases);
            if (!has_ipl || !has_font) {
                err_out = "Place MZ-700 ROMs in:\n" + system_dir_ + "/\nMissing:";
                if (!has_ipl) {
                    err_out += "\n  IPL.ROM (or 1Z-009A/B, NEWMON7.ROM)";
                }
                if (!has_font) {
                    err_out += "\n  FONT.ROM (or MZ700FON.JP / MZ700FON.DAT)";
                }
                return false;
            }
        }
        return true;
    }

    void teardown_core() {
        g_active_instance = this;
        clear_held_inputs();
        clear_leds();
        if (loader_->is_library_open()) {
            const auto& syms = loader_->symbols();
            if (game_loaded_ && syms.retro_unload_game) {
                syms.retro_unload_game();
            }
            if (initialized_ && syms.retro_deinit) {
                syms.retro_deinit();
            }
        }
        game_loaded_ = false;
        initialized_ = false;
        support_no_game_ = false;
        shutdown_requested_ = false;
        has_disk_ctrl_ = false;
        has_disk_ctrl_ext_ = false;
        disk_ctrl_ = {};
        disk_ctrl_ext_ = {};
        keyboard_cb_ = nullptr;
        option_dirty_ = false;
        pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555;
        audio_resample_pos_ = 0.0;
        audio_prev_l_ = 0;
        audio_prev_r_ = 0;
        audio_has_prev_ = false;
        loader_->close_library();
    }

    bool start_core(const char* media_path) {
        g_active_instance = this;
        loader_->set_last_error({});
        shutdown_requested_ = false;
        clear_leds();

        std::string rom_err;
        if (!check_required_roms(rom_err)) {
            loader_->set_last_error(rom_err);
            log(3, rom_err.c_str());
            return false;
        }

        if (is_applewin_) {
            setenv("CZX_APPLE2_ROM_DIR", system_dir_.c_str(), 1);
        }

        if (!loader_->open_library()) {
            loader_->set_last_error("Failed to open core library");
            return false;
        }

        const auto& syms = loader_->symbols();
        options_.clear();
        seed_default_options();
        load_user_config();

        if (is_bluemsx_) {
            const auto it = options_.find("bluemsx_msxtype");
            const std::string msxtype = (it != options_.end()) ? it->second : std::string{};
            if (!msxtype.empty() && msxtype != "Auto") {
                std::vector<std::string> problems;
                BluemsxMachineInfo minfo{};
                if (!validate_bluemsx_machine_roms(fs::path(system_dir_), msxtype, problems, &minfo)) {
                    std::string msg = "blueMSX machine '" + msxtype + "' is missing required ROMs in:\n" +
                                      system_dir_ + "/\nMissing/Invalid:";
                    const size_t limit = std::min<size_t>(problems.size(), 6u);
                    for (size_t i = 0; i < limit; ++i) {
                        msg += "\n  " + problems[i];
                    }
                    if (problems.size() > limit) {
                        msg += "\n  ... (+" + std::to_string(problems.size() - limit) + " more)";
                    }
                    loader_->set_last_error(msg);
                    log(3, msg.c_str());
                    teardown_core();
                    return false;
                }
                if (media_path && media_path[0] != '\0' && is_msx_disk_ext(media_path) &&
                    (!minfo.has_disk_rom || minfo.fdc_count <= 0)) {
                    const std::string msg =
                        "MSX disk images (.dsk/.m3u) require a Disk BIOS ROM (e.g. PANASONICDISK.rom or PHILIPSDISK.rom) in:\n" +
                        system_dir_ + "/Machines/Shared Roms/";
                    loader_->set_last_error(msg);
                    log(3, msg.c_str());
                    teardown_core();
                    return false;
                }
                log(1, ("bluemsx: machine = " + msxtype).c_str());
            } else if (media_path && media_path[0] != '\0' && is_msx_disk_ext(media_path)) {
                log(2, "bluemsx: bluemsx_msxtype=Auto does not auto-enable MSX disk machines");
            }
        }

        syms.retro_set_environment(cb_env);
        syms.retro_set_video_refresh(cb_video);
        syms.retro_set_audio_sample(cb_audio_sample);
        syms.retro_set_audio_sample_batch(cb_audio_batch);
        syms.retro_set_input_poll(cb_input_poll);
        syms.retro_set_input_state(cb_input_state);

        syms.retro_init();
        initialized_ = true;

        if (media_path && media_path[0] != '\0') {
            std::vector<uint8_t> buf;
            retro_game_info info{};
            info.path = media_path;
            if (!need_fullpath_) {
                std::ifstream ifs(media_path, std::ios::binary | std::ios::ate);
                if (!ifs.is_open()) {
                    loader_->set_last_error("Failed to open media:\n" + std::string(media_path));
                    log(3, loader_->last_error());
                    teardown_core();
                    return false;
                }
                const auto sz = ifs.tellg();
                if (sz > 0) {
                    buf.resize(static_cast<size_t>(sz));
                    ifs.seekg(0);
                    ifs.read(reinterpret_cast<char*>(buf.data()), sz);
                }
                info.data = buf.data();
                info.size = buf.size();
            }

            if (!syms.retro_load_game(&info) || shutdown_requested_) {
                loader_->set_last_error("Failed to load media:\n" + std::string(media_path));
                log(3, loader_->last_error());
                teardown_core();
                return false;
            }

            game_loaded_ = true;
            has_media_ = true;
            ejected_ = false;
            media_path_ = media_path;
            media_label_ = fs::path(media_path).filename().string();
            refresh_av_info();
            return true;
        }

        if (support_no_game_) {
            if (!syms.retro_load_game(nullptr) || shutdown_requested_) {
                if (!loader_->last_error()) {
                    loader_->set_last_error("Core init failed.\nCheck external ROM files in:\n" + system_dir_);
                }
                log(3, loader_->last_error());
                teardown_core();
                return false;
            }
            game_loaded_ = true;
            has_media_ = false;
            ejected_ = false;
            media_path_.clear();
            media_label_ = "None";
            refresh_av_info();
            return true;
        }

        return true;
    }

    void log(int level, const char* msg) const {
        if (host_.log) {
            host_.log(host_.user_data, level, msg);
        } else if (msg) {
            std::fprintf(stderr, "[libretro] %s\n", msg);
        }
    }

    void seed_default_options() {
        if (is_applewin_) {
            options_["applewin_floppy_multidrive"] = "enabled";
        } else if (is_vice_) {
            if (loader_->id() == "vice_x64") {
                options_["vice_sid_engine"] = "FastSID";
            }
            options_["vice_resid_sampling"] = "fast";
            options_["vice_drive_true_emulation"] = "enabled";
            options_["vice_zoom_mode"] = "none";
            options_["vice_c64_model"] = "C64 PAL auto";
            options_["vice_joyport"] = "2";
            options_["vice_autoloadwarp"] = "enabled";
            options_["vice_datasette_hotkeys"] = "enabled";
            options_["vice_mapper_datasette_start"] = "RETROK_KP_PLUS";
            options_["vice_mapper_datasette_stop"] = "RETROK_KP_MINUS";
            options_["vice_mapper_datasette_rewind"] = "---";
            options_["vice_mapper_datasette_forward"] = "---";
            options_["vice_mapper_datasette_reset"] = "---";
        } else if (is_quasi88_) {
            options_["q88_basic_mode"] = "N88 V2";
            options_["q88_sub_cpu_mode"] = "2";
            options_["q88_cpu_clock"] = "4";
            options_["q88_sound_board"] = "OPNA";
            options_["q88_use_fdc_wait"] = "enabled";
            options_["q88_rumble"] = "disabled";
            options_["q88_screen_size"] = "full";
        } else if (is_np2kai_) {
            options_["np2kai_model"] = "PC-9801VX";
            options_["np2kai_clk_base"] = "2.4576 MHz";
            options_["np2kai_clk_mult"] = "4";
            options_["np2kai_SNDboard"] = "PC9801-86";
            options_["np2kai_keyboard"] = "Us";
            options_["np2kai_joymode"] = "Atari Joypad";
            options_["np2kai_inputmouse"] = "ON";
            options_["np2kai_keyrepeat"] = "OFF";
            options_["np2kai_skipline"] = "Full 255 lines";
        } else if (is_bluemsx_) {
            options_["bluemsx_msxtype"] = detect_default_bluemsx_machine();
            options_["bluemsx_vdp_synctype"] = "60Hz";
            options_["bluemsx_ym2413_enable"] = "enabled";
            options_["bluemsx_overscan"] = "disabled";
            options_["bluemsx_auto_rewind_cas"] = "ON";
        } else if (is_emuz700_) {
            options_["emuz700_joystick"] = "JOY-700";
            options_["emuz700_tape_sound"] = "disabled";
            options_["emuz700_tape_turbo"] = "enabled";
        }
    }

    void load_user_config() {
        const fs::path cfg_path = fs::path(data_dir_) / "config" / (loader_->id() + ".cfg");
        std::ifstream ifs(cfg_path);
        if (!ifs.is_open()) return;
        std::string line;
        while (std::getline(ifs, line)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto trim = [](std::string s) {
                const auto a = s.find_first_not_of(" \t\r\n\"");
                if (a == std::string::npos) return std::string{};
                const auto b = s.find_last_not_of(" \t\r\n\"");
                return s.substr(a, b - a + 1);
            };
            const std::string k = trim(line.substr(0, eq));
            const std::string v = trim(line.substr(eq + 1));
            if (!k.empty() && !v.empty()) {
                options_[k] = v;
            }
        }
    }

    void refresh_av_info() {
        if (loader_->symbols().retro_get_system_av_info) {
            loader_->symbols().retro_get_system_av_info(&av_info_);
            loader_->update_av_timing(av_info_.timing.fps, av_info_.timing.sample_rate);
        }
        if (loader_->symbols().retro_set_controller_port_device) {
            loader_->symbols().retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
            loader_->symbols().retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
        }
    }

    bool has_disk_control() const noexcept {
        return (has_disk_ctrl_ && disk_ctrl_.set_eject_state && disk_ctrl_.replace_image_index) ||
               (has_disk_ctrl_ext_ && disk_ctrl_ext_.set_eject_state && disk_ctrl_ext_.replace_image_index);
    }

    bool disk_set_eject_(bool eject) {
        if (has_disk_ctrl_ext_ && disk_ctrl_ext_.set_eject_state) {
            return disk_ctrl_ext_.set_eject_state(eject);
        }
        if (has_disk_ctrl_ && disk_ctrl_.set_eject_state) {
            return disk_ctrl_.set_eject_state(eject);
        }
        return false;
    }

    bool hot_swap_disk(const std::string& path) {
        auto get_num = [&]() -> unsigned {
            if (has_disk_ctrl_ext_ && disk_ctrl_ext_.get_num_images) return disk_ctrl_ext_.get_num_images();
            if (has_disk_ctrl_ && disk_ctrl_.get_num_images) return disk_ctrl_.get_num_images();
            return 0;
        };
        auto get_idx = [&]() -> unsigned {
            if (has_disk_ctrl_ext_ && disk_ctrl_ext_.get_image_index) return disk_ctrl_ext_.get_image_index();
            if (has_disk_ctrl_ && disk_ctrl_.get_image_index) return disk_ctrl_.get_image_index();
            return 0;
        };
        auto add_idx = [&]() -> bool {
            if (has_disk_ctrl_ext_ && disk_ctrl_ext_.add_image_index) return disk_ctrl_ext_.add_image_index();
            if (has_disk_ctrl_ && disk_ctrl_.add_image_index) return disk_ctrl_.add_image_index();
            return false;
        };
        auto replace_idx = [&](unsigned idx, const retro_game_info* info) -> bool {
            if (has_disk_ctrl_ext_ && disk_ctrl_ext_.replace_image_index) {
                return disk_ctrl_ext_.replace_image_index(idx, info);
            }
            if (has_disk_ctrl_ && disk_ctrl_.replace_image_index) {
                return disk_ctrl_.replace_image_index(idx, info);
            }
            return false;
        };
        auto set_idx = [&](unsigned idx) -> bool {
            if (has_disk_ctrl_ext_ && disk_ctrl_ext_.set_image_index) return disk_ctrl_ext_.set_image_index(idx);
            if (has_disk_ctrl_ && disk_ctrl_.set_image_index) return disk_ctrl_.set_image_index(idx);
            return false;
        };

        if (!disk_set_eject_(true)) {
            return false;
        }
        unsigned num = get_num();
        unsigned idx = get_idx();
        if (num == 0 && add_idx()) {
            num = 1;
            idx = 0;
        }
        if (idx >= num) idx = 0;

        retro_game_info info{};
        info.path = path.c_str();
        std::vector<uint8_t> buf;
        if (!need_fullpath_) {
            std::ifstream ifs(path, std::ios::binary | std::ios::ate);
            if (!ifs.is_open()) {
                disk_set_eject_(false);
                return false;
            }
            const auto sz = ifs.tellg();
            if (sz > 0) {
                buf.resize(static_cast<size_t>(sz));
                ifs.seekg(0);
                ifs.read(reinterpret_cast<char*>(buf.data()), sz);
            }
            info.data = buf.data();
            info.size = buf.size();
        }

        const bool ok = replace_idx(idx, &info) && set_idx(idx);
        disk_set_eject_(false);
        return ok;
    }

    void emit_retro_key(bool down, uint32_t retrok, uint32_t character, uint16_t mods) {
        if (retrok < retro_key_down_.size()) {
            retro_key_down_[retrok] = down;
        }
        if (keyboard_cb_) {
            keyboard_cb_(down, retrok, character, mods);
        }
    }

    void clear_held_inputs() {
        for (size_t i = 0; i < active_keys_.size(); ++i) {
            if (active_keys_[i].down) {
                emit_retro_key(false, active_keys_[i].retrok, 0, RETROKMOD_NONE);
                active_keys_[i] = HeldKey{};
            }
        }
        if (synthetic_shift_count_ > 0 && !phys_shift_held_) {
            emit_retro_key(false, RETROK_LSHIFT, 0, RETROKMOD_NONE);
        }
        synthetic_shift_count_ = 0;
        phys_shift_held_ = false;
        pulsed_retrok_ = RETROK_UNKNOWN;
        pulsed_frames_ = 0;
        pulsed_emitted_ = false;
        retro_key_down_.fill(false);
        joy_state_ = {0, 0};
        mouse_pending_dx_ = 0;
        mouse_pending_dy_ = 0;
        mouse_latched_dx_ = 0;
        mouse_latched_dy_ = 0;
        mouse_buttons_ = 0;
    }

    void handle_video(const void* data, unsigned width, unsigned height, size_t pitch) {
        if (!host_.video_frame) return;

        if (data != nullptr && width > 0 && height > 0) {
            cached_w_ = static_cast<uint16_t>(width);
            cached_h_ = static_cast<uint16_t>(height);

            if (pixel_format_ == RETRO_PIXEL_FORMAT_XRGB8888) {
                cached_fmt_ = CZX_PIXEL_XRGB8888;
                cached_pitch_ = static_cast<uint32_t>(width) * 4u;
                cached_32_.resize(static_cast<size_t>(width) * height);
                if (pitch == static_cast<size_t>(cached_pitch_)) {
                    std::memcpy(cached_32_.data(), data, static_cast<size_t>(cached_pitch_) * height);
                } else {
                    const auto* src = static_cast<const uint8_t*>(data);
                    for (unsigned y = 0; y < height; ++y) {
                        std::memcpy(cached_32_.data() + static_cast<size_t>(y) * width,
                                    src + static_cast<size_t>(y) * pitch,
                                    cached_pitch_);
                    }
                }
            } else {
                cached_fmt_ = CZX_PIXEL_RGB565;
                cached_pitch_ = static_cast<uint32_t>(width) * 2u;
                cached_16_.resize(static_cast<size_t>(width) * height);
                const auto* src = static_cast<const uint8_t*>(data);
                if (pixel_format_ == RETRO_PIXEL_FORMAT_RGB565) {
                    if (pitch == static_cast<size_t>(cached_pitch_)) {
                        std::memcpy(cached_16_.data(), data, static_cast<size_t>(cached_pitch_) * height);
                    } else {
                        for (unsigned y = 0; y < height; ++y) {
                            std::memcpy(cached_16_.data() + static_cast<size_t>(y) * width,
                                        src + static_cast<size_t>(y) * pitch,
                                        cached_pitch_);
                        }
                    }
                } else {
                    for (unsigned y = 0; y < height; ++y) {
                        const auto* srow = reinterpret_cast<const uint16_t*>(src + static_cast<size_t>(y) * pitch);
                        uint16_t* drow = cached_16_.data() + static_cast<size_t>(y) * width;
                        for (unsigned x = 0; x < width; ++x) {
                            drow[x] = rgb1555_to_rgb565(srow[x]);
                        }
                    }
                }
            }
        }

        if (cached_w_ == 0 || cached_h_ == 0) return;

        czx_frame_t frame{};
        frame.data = (cached_fmt_ == CZX_PIXEL_XRGB8888)
                         ? static_cast<const void*>(cached_32_.data())
                         : static_cast<const void*>(cached_16_.data());
        frame.pitch_bytes = cached_pitch_;
        frame.width = cached_w_;
        frame.height = cached_h_;
        frame.format = cached_fmt_;

        // De-double AppleWin's scanline-doubled 560x384 output to native 560x192 when rows repeat.
        if (is_applewin_ && cached_w_ == 560 && cached_h_ == 384) {
            auto rows_equal = [&](uint32_t y0, uint32_t y1) -> bool {
                const uint8_t* p0 = static_cast<const uint8_t*>(frame.data) + static_cast<size_t>(y0) * cached_pitch_;
                const uint8_t* p1 = static_cast<const uint8_t*>(frame.data) + static_cast<size_t>(y1) * cached_pitch_;
                return std::memcmp(p0, p1, cached_pitch_) == 0;
            };
            if (rows_equal(0, 1) && rows_equal(100, 101) && rows_equal(200, 201)) {
                frame.height = 192;
                frame.pitch_bytes = cached_pitch_ * 2u;
            }
        }

        if (is_applewin_ && frame.width == 560 && frame.height == 192) {
            frame.par_num = 1;
            frame.par_den = 2;
        } else if (is_vice_) {
            frame.par_num = 1;
            frame.par_den = 1;
            frame.flags = CZX_FRAME_HAS_BORDER;
            if (frame.width == 384 && frame.height == 272) {
                frame.crop_x = 32;
                frame.crop_y = 36;
                frame.crop_w = 320;
                frame.crop_h = 200;
            } else if (frame.width == 384 && frame.height == 247) {
                frame.crop_x = 32;
                frame.crop_y = 23;
                frame.crop_w = 320;
                frame.crop_h = 200;
            }
        } else if (is_bluemsx_) {
            frame.flags = CZX_FRAME_HAS_BORDER;
            if (frame.width == 544 && frame.height == 240) {
                frame.par_num = 1;
                frame.par_den = 2;
                frame.crop_x = 16;
                frame.crop_y = 14;
                frame.crop_w = 512;
                frame.crop_h = 212;
            } else if (frame.width == 272 && frame.height == 240) {
                frame.par_num = 1;
                frame.par_den = 1;
                frame.crop_x = 8;
                frame.crop_y = 14;
                frame.crop_w = 256;
                frame.crop_h = 212;
            } else {
                frame.par_num = 1;
                frame.par_den = 1;
            }
        } else if (is_emuz700_) {
            frame.par_num = 1;
            frame.par_den = 1;
        } else if (av_info_.geometry.aspect_ratio > 0.0f && frame.height > 0) {
            const double par = static_cast<double>(av_info_.geometry.aspect_ratio) *
                               static_cast<double>(frame.height) / static_cast<double>(frame.width);
            frame.par_num = static_cast<uint16_t>(std::max(1L, std::lround(par * 1000.0)));
            frame.par_den = 1000;
        } else {
            frame.par_num = 1;
            frame.par_den = 1;
        }

        host_.video_frame(host_.user_data, &frame);
    }

    void handle_audio(const int16_t* data, size_t frames) {
        if (!data || frames == 0) return;
        const double src_rate = (av_info_.timing.sample_rate > 1000.0)
                                    ? av_info_.timing.sample_rate
                                    : kTargetAudioSampleRateF;
        if (std::abs(src_rate - kTargetAudioSampleRateF) < 1.0) {
            audio_out_frame_.insert(audio_out_frame_.end(), data, data + frames * 2u);
            audio_prev_l_ = data[(frames - 1u) * 2u];
            audio_prev_r_ = data[(frames - 1u) * 2u + 1u];
            audio_has_prev_ = true;
            return;
        }

        if (!audio_has_prev_) {
            audio_prev_l_ = data[0];
            audio_prev_r_ = data[1];
            audio_has_prev_ = true;
            audio_resample_pos_ = 0.0;
        }

        const double step = src_rate / kTargetAudioSampleRateF;
        const double limit = static_cast<double>(frames) - 1.0;
        while (audio_resample_pos_ < limit) {
            double l0 = 0.0;
            double r0 = 0.0;
            double l1 = 0.0;
            double r1 = 0.0;
            double frac = 0.0;
            if (audio_resample_pos_ < 0.0) {
                l0 = static_cast<double>(audio_prev_l_);
                r0 = static_cast<double>(audio_prev_r_);
                l1 = static_cast<double>(data[0]);
                r1 = static_cast<double>(data[1]);
                frac = std::clamp(audio_resample_pos_ + 1.0, 0.0, 1.0);
            } else {
                const size_t i0 = static_cast<size_t>(audio_resample_pos_);
                const size_t i1 = i0 + 1u;
                l0 = static_cast<double>(data[i0 * 2u + 0u]);
                r0 = static_cast<double>(data[i0 * 2u + 1u]);
                l1 = static_cast<double>(data[i1 * 2u + 0u]);
                r1 = static_cast<double>(data[i1 * 2u + 1u]);
                frac = audio_resample_pos_ - static_cast<double>(i0);
            }
            const double l = l0 * (1.0 - frac) + l1 * frac;
            const double r = r0 * (1.0 - frac) + r1 * frac;
            audio_out_frame_.push_back(static_cast<int16_t>(std::clamp(std::lround(l), -32768L, 32767L)));
            audio_out_frame_.push_back(static_cast<int16_t>(std::clamp(std::lround(r), -32768L, 32767L)));
            audio_resample_pos_ += step;
        }
        audio_prev_l_ = data[(frames - 1u) * 2u];
        audio_prev_r_ = data[(frames - 1u) * 2u + 1u];
        audio_resample_pos_ -= static_cast<double>(frames);
    }

    int16_t query_input(unsigned port, unsigned device, unsigned, unsigned id) const {
        if (device == RETRO_DEVICE_JOYPAD) {
            if (port >= joy_state_.size()) return 0;
            const uint32_t b = joy_state_[port];
            uint16_t mask = 0;
            if (is_applewin_ || is_quasi88_ || is_bluemsx_ || is_emuz700_) {
                if (b & CZX_JOY_FIRE1) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
                if (b & CZX_JOY_FIRE2) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
            } else if (is_np2kai_) {
                if (b & CZX_JOY_FIRE1) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
                if (b & CZX_JOY_FIRE2) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_Y);
            } else {
                if (b & CZX_JOY_FIRE1) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
                if (b & CZX_JOY_FIRE2) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
            }
            if (b & CZX_JOY_UP)    mask |= (1u << RETRO_DEVICE_ID_JOYPAD_UP);
            if (b & CZX_JOY_DOWN)  mask |= (1u << RETRO_DEVICE_ID_JOYPAD_DOWN);
            if (b & CZX_JOY_LEFT)  mask |= (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
            if (b & CZX_JOY_RIGHT) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
            if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
                return static_cast<int16_t>(mask);
            }
            if (id < 16) {
                return (mask & (1u << id)) ? 1 : 0;
            }
            return 0;
        }
        if (device == RETRO_DEVICE_MOUSE) {
            if (port != 0) return 0;
            switch (id) {
                case RETRO_DEVICE_ID_MOUSE_X:      return mouse_latched_dx_;
                case RETRO_DEVICE_ID_MOUSE_Y:      return mouse_latched_dy_;
                case RETRO_DEVICE_ID_MOUSE_LEFT:   return (mouse_buttons_ & CZX_MOUSE_LEFT) ? 1 : 0;
                case RETRO_DEVICE_ID_MOUSE_RIGHT:  return (mouse_buttons_ & CZX_MOUSE_RIGHT) ? 1 : 0;
                case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (mouse_buttons_ & CZX_MOUSE_MIDDLE) ? 1 : 0;
                default: return 0;
            }
        }
        if (device == RETRO_DEVICE_KEYBOARD && id < retro_key_down_.size()) {
            return retro_key_down_[id] ? 1 : 0;
        }
        return 0;
    }

    bool handle_env(unsigned cmd, void* data) {
        switch (cmd) {
            case RETRO_ENVIRONMENT_GET_OVERSCAN:
            case RETRO_ENVIRONMENT_GET_CAN_DUPE:
                if (data) *static_cast<bool*>(data) = true;
                return true;

            case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
                if (data) *static_cast<const char**>(data) = system_dir_.c_str();
                return true;

            case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
                if (data) *static_cast<const char**>(data) = save_dir_.c_str();
                return true;

            case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
                if (data) *static_cast<const char**>(data) = loader_->path().c_str();
                return true;

            case RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY:
                if (data) *static_cast<const char**>(data) = data_dir_.c_str();
                return true;

            case RETRO_ENVIRONMENT_SHUTDOWN:
                shutdown_requested_ = true;
                return true;

            case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
                if (!data) return false;
                const auto fmt = *static_cast<const retro_pixel_format*>(data);
                if (fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                    fmt == RETRO_PIXEL_FORMAT_XRGB8888 ||
                    fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
                    pixel_format_ = fmt;
                    return true;
                }
                return false;
            }

            case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
                if (data) support_no_game_ = *static_cast<const bool*>(data);
                return true;

            case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
                if (data) keyboard_cb_ = static_cast<const retro_keyboard_callback*>(data)->callback;
                return true;

            case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
                if (data) *static_cast<unsigned*>(data) = 1;
                return true;

            case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
                if (data) {
                    disk_ctrl_ = *static_cast<const retro_disk_control_callback*>(data);
                    has_disk_ctrl_ = true;
                } else {
                    has_disk_ctrl_ = false;
                }
                return true;

            case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
                if (data) {
                    disk_ctrl_ext_ = *static_cast<const retro_disk_control_ext_callback*>(data);
                    has_disk_ctrl_ext_ = true;
                } else {
                    has_disk_ctrl_ext_ = false;
                }
                return true;

            case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
                if (data) static_cast<retro_log_callback*>(data)->log = cb_log;
                return true;

            case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
                return true;

            case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
                if (data) {
                    *static_cast<uint64_t*>(data) = (1ULL << RETRO_DEVICE_JOYPAD) |
                                                    (1ULL << RETRO_DEVICE_KEYBOARD) |
                                                    (1ULL << RETRO_DEVICE_MOUSE);
                }
                return true;

            case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
                if (data) *static_cast<unsigned*>(data) = 2;
                return true;

            case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
                if (data) *static_cast<unsigned*>(data) = 2;
                return true;

            case RETRO_ENVIRONMENT_SET_VARIABLES: {
                const auto* vars = static_cast<const retro_variable*>(data);
                while (vars && vars->key) {
                    if (options_.find(vars->key) == options_.end() && vars->value) {
                        const char* semi = std::strchr(vars->value, ';');
                        if (semi) {
                            const char* start = semi + 1;
                            while (*start == ' ') ++start;
                            const char* bar = std::strchr(start, '|');
                            options_[vars->key] = bar ? std::string(start, static_cast<size_t>(bar - start))
                                                      : std::string(start);
                        }
                    }
                    ++vars;
                }
                return true;
            }

            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
                auto register_defs = [&](const auto* defs) {
                    while (defs && defs->key) {
                        if (options_.find(defs->key) == options_.end()) {
                            const char* def = defs->default_value ? defs->default_value : defs->values[0].value;
                            if (def) options_[defs->key] = def;
                        }
                        ++defs;
                    }
                };
                if (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS) {
                    register_defs(static_cast<const retro_core_option_definition*>(data));
                } else if (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL) {
                    const auto* intl = static_cast<const retro_core_options_intl*>(data);
                    register_defs(intl ? intl->us : nullptr);
                } else if (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2) {
                    const auto* v2 = static_cast<const retro_core_options_v2*>(data);
                    register_defs(v2 ? v2->definitions : nullptr);
                } else {
                    const auto* intl = static_cast<const retro_core_options_v2_intl*>(data);
                    register_defs((intl && intl->us) ? intl->us->definitions : nullptr);
                }
                return true;
            }

            case RETRO_ENVIRONMENT_GET_VARIABLE: {
                auto* var = static_cast<retro_variable*>(data);
                if (!var || !var->key) return false;
                const auto it = options_.find(var->key);
                if (it == options_.end()) {
                    var->value = nullptr;
                    return false;
                }
                var->value = it->second.c_str();
                return true;
            }

            case RETRO_ENVIRONMENT_SET_VARIABLE: {
                const auto* var = static_cast<const retro_variable*>(data);
                if (var && var->key && var->value) {
                    options_[var->key] = var->value;
                    option_dirty_ = true;
                }
                return true;
            }

            case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
                if (data) {
                    *static_cast<bool*>(data) = option_dirty_;
                    option_dirty_ = false;
                }
                return true;

            case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
                if (data) {
                    av_info_ = *static_cast<const retro_system_av_info*>(data);
                    loader_->update_av_timing(av_info_.timing.fps, av_info_.timing.sample_rate);
                }
                return true;

            case RETRO_ENVIRONMENT_SET_GEOMETRY:
                if (data) {
                    av_info_.geometry = *static_cast<const retro_game_geometry*>(data);
                }
                return true;

            case RETRO_ENVIRONMENT_GET_LANGUAGE:
                if (data) *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
                return true;

            case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
                if (data) *static_cast<unsigned*>(data) = 1;
                return true;

            case RETRO_ENVIRONMENT_GET_LED_INTERFACE:
                if (data) {
                    static_cast<retro_led_interface*>(data)->set_led_state = cb_led_state;
                }
                return true;

            case RETRO_ENVIRONMENT_SET_MESSAGE:
            case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
            case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
            case RETRO_ENVIRONMENT_SET_ROTATION:
            case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
            case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
                if (cmd == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE && data) {
                    *static_cast<int*>(data) = 3;
                }
                return true;

            default:
                return false;
        }
    }

    static bool RETRO_CALLCONV cb_env(unsigned cmd, void* data) {
        return g_active_instance ? g_active_instance->handle_env(cmd, data) : false;
    }
    static void RETRO_CALLCONV cb_video(const void* data, unsigned w, unsigned h, size_t pitch) {
        if (g_active_instance) g_active_instance->handle_video(data, w, h, pitch);
    }
    static void RETRO_CALLCONV cb_audio_sample(int16_t left, int16_t right) {
        if (g_active_instance) {
            const int16_t pair[2] = {left, right};
            g_active_instance->handle_audio(pair, 1);
        }
    }
    static size_t RETRO_CALLCONV cb_audio_batch(const int16_t* data, size_t frames) {
        if (g_active_instance) g_active_instance->handle_audio(data, frames);
        return frames;
    }
    static void RETRO_CALLCONV cb_input_poll(void) {
        if (g_active_instance) g_active_instance->latch_mouse_poll();
    }
    static int16_t RETRO_CALLCONV cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
        return g_active_instance ? g_active_instance->query_input(port, device, index, id) : 0;
    }
    static void RETRO_CALLCONV cb_log(enum retro_log_level level, const char* fmt, ...) {
        if (!g_active_instance || !fmt) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        const size_t len = std::strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        g_active_instance->log(static_cast<int>(level), buf);
    }
    static void RETRO_CALLCONV cb_led_state(int led, int state) {
        if (g_active_instance) g_active_instance->handle_led_state(led, state);
    }
};

const czx_core_desc_t* lr_get_desc(const czx_core_api_t* api) {
    const auto* loader = static_cast<const LibretroCoreLoader*>(api ? api->api_data : nullptr);
    return loader ? loader->desc() : nullptr;
}

czx_core_instance_t* lr_create(const czx_core_api_t* api, const czx_host_t* host) {
    auto* loader = static_cast<LibretroCoreLoader*>(api ? api->api_data : nullptr);
    if (!loader) return nullptr;
    auto inst = std::make_unique<LibretroCoreInstance>(loader, host, loader->extensions());
    if (!inst->init()) {
        return nullptr;
    }
    return reinterpret_cast<czx_core_instance_t*>(inst.release());
}

void lr_destroy(czx_core_instance_t* inst) {
    delete reinterpret_cast<LibretroCoreInstance*>(inst);
}

void lr_reset(czx_core_instance_t* inst, bool hard) {
    if (inst) reinterpret_cast<LibretroCoreInstance*>(inst)->reset(hard);
}

bool lr_load_media(czx_core_instance_t* inst, const char* path) {
    return inst && path && reinterpret_cast<LibretroCoreInstance*>(inst)->load_media(path);
}

bool lr_eject_media(czx_core_instance_t* inst) {
    return inst && reinterpret_cast<LibretroCoreInstance*>(inst)->eject_media();
}

void lr_run_frame(czx_core_instance_t* inst) {
    if (inst) reinterpret_cast<LibretroCoreInstance*>(inst)->run_frame();
}

void lr_on_key(czx_core_instance_t* inst, const czx_key_event_t* ev) {
    if (inst && ev) reinterpret_cast<LibretroCoreInstance*>(inst)->on_key(*ev);
}

void lr_on_joystick(czx_core_instance_t* inst, uint32_t port, uint32_t buttons) {
    if (inst) reinterpret_cast<LibretroCoreInstance*>(inst)->on_joystick(port, buttons);
}

void lr_on_mouse(czx_core_instance_t* inst, int32_t dx, int32_t dy, uint32_t buttons) {
    if (inst) reinterpret_cast<LibretroCoreInstance*>(inst)->on_mouse(dx, dy, buttons);
}

bool lr_set_option(czx_core_instance_t* inst, const char* key, const char* value) {
    return inst && reinterpret_cast<LibretroCoreInstance*>(inst)->set_option(key, value);
}

const char* lr_get_extensions(const czx_core_instance_t* inst) {
    return inst ? reinterpret_cast<const LibretroCoreInstance*>(inst)->get_extensions() : nullptr;
}

const char* lr_get_media_label(const czx_core_instance_t* inst) {
    return inst ? reinterpret_cast<const LibretroCoreInstance*>(inst)->get_media_label() : "None";
}

void lr_set_data_dir(czx_core_instance_t* inst, const char* data_dir) {
    if (inst && data_dir) reinterpret_cast<LibretroCoreInstance*>(inst)->set_data_dir(data_dir);
}

const char* lr_get_last_error(const czx_core_api_t* api) {
    const auto* loader = static_cast<const LibretroCoreLoader*>(api ? api->api_data : nullptr);
    return loader ? loader->last_error() : nullptr;
}

uint32_t lr_get_drive_count(const czx_core_instance_t* inst) {
    return inst ? reinterpret_cast<const LibretroCoreInstance*>(inst)->get_drive_count() : 1u;
}

const char* lr_get_drive_name(const czx_core_instance_t* inst, uint32_t drive) {
    return inst ? reinterpret_cast<const LibretroCoreInstance*>(inst)->get_drive_name(drive) : "Media";
}

const char* lr_get_drive_media_label(const czx_core_instance_t* inst, uint32_t drive) {
    return inst ? reinterpret_cast<const LibretroCoreInstance*>(inst)->get_drive_media_label(drive) : "Empty";
}

bool lr_load_media_in_drive(czx_core_instance_t* inst, uint32_t drive,
                            const char* path, const char* display_label) {
    return inst && path &&
           reinterpret_cast<LibretroCoreInstance*>(inst)->load_media_in_drive(drive, path, display_label);
}

bool lr_load_dual_media(czx_core_instance_t* inst,
                        const char* path0, const char* label0,
                        const char* path1, const char* label1) {
    return inst &&
           reinterpret_cast<LibretroCoreInstance*>(inst)->load_dual_media(path0, label0, path1, label1);
}

bool lr_eject_drive(czx_core_instance_t* inst, uint32_t drive) {
    return inst && reinterpret_cast<LibretroCoreInstance*>(inst)->eject_drive(drive);
}

bool lr_get_activity(const czx_core_instance_t* inst, czx_activity_t* out) {
    return inst && reinterpret_cast<const LibretroCoreInstance*>(inst)->get_activity(out);
}

}  // namespace

LibretroCoreLoader::LibretroCoreLoader() = default;

LibretroCoreLoader::~LibretroCoreLoader() {
    close_library();
}

void LibretroCoreLoader::close_library() noexcept {
    syms_.reset();
    if (dl_) {
        dlclose(dl_);
        dl_ = nullptr;
    }
}

bool LibretroCoreLoader::open_library() {
    close_library();
    void* dl = dlopen(so_path_.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!dl) {
        std::fprintf(stderr, "czx: dlopen(%s) failed: %s\n", so_path_.c_str(), dlerror());
        return false;
    }

    auto syms = std::make_unique<Symbols>();
    auto resolve = [&](auto& fn, const char* name, bool required) -> bool {
        using FnType = std::remove_reference_t<decltype(fn)>;
        fn = reinterpret_cast<FnType>(dlsym(dl, name));
        if (required && !fn) {
            std::fprintf(stderr, "czx: missing symbol %s in %s\n", name, so_path_.c_str());
            return false;
        }
        return true;
    };

    const bool ok =
        resolve(syms->retro_api_version, "retro_api_version", true) &&
        resolve(syms->retro_init, "retro_init", true) &&
        resolve(syms->retro_deinit, "retro_deinit", true) &&
        resolve(syms->retro_get_system_info, "retro_get_system_info", true) &&
        resolve(syms->retro_get_system_av_info, "retro_get_system_av_info", true) &&
        resolve(syms->retro_set_environment, "retro_set_environment", true) &&
        resolve(syms->retro_set_video_refresh, "retro_set_video_refresh", true) &&
        resolve(syms->retro_set_audio_sample, "retro_set_audio_sample", true) &&
        resolve(syms->retro_set_audio_sample_batch, "retro_set_audio_sample_batch", true) &&
        resolve(syms->retro_set_input_poll, "retro_set_input_poll", true) &&
        resolve(syms->retro_set_input_state, "retro_set_input_state", true) &&
        resolve(syms->retro_reset, "retro_reset", true) &&
        resolve(syms->retro_run, "retro_run", true) &&
        resolve(syms->retro_load_game, "retro_load_game", true) &&
        resolve(syms->retro_unload_game, "retro_unload_game", true) &&
        resolve(syms->retro_set_controller_port_device, "retro_set_controller_port_device", false);

    if (!ok || syms->retro_api_version() != RETRO_API_VERSION) {
        dlclose(dl);
        return false;
    }

    dl_ = dl;
    syms_ = std::move(syms);
    return true;
}

void LibretroCoreLoader::update_av_timing(double fps, double) {
    if (fps > 1.0 && fps < 240.0) {
        desc_.fps_num = static_cast<uint32_t>(std::lround(fps * 1000.0));
        desc_.fps_den = 1000u;
    }
    desc_.audio_sample_rate = kTargetAudioSampleRate;
}

bool LibretroCoreLoader::load(const std::string& so_path) {
    close_library();
    loaded_ = false;
    so_path_ = so_path;

    if (!open_library()) {
        return false;
    }

    retro_system_info sys_info{};
    syms_->retro_get_system_info(&sys_info);

    need_fullpath_ = sys_info.need_fullpath;
    id_ = derive_core_id(so_path);
    display_name_ = derive_display_name(id_, sys_info.library_name);
    extensions_ = sys_info.valid_extensions ? sys_info.valid_extensions : "";
    close_library();

    desc_.api_version = CZX_CORE_API_VERSION;
    desc_.id = id_.c_str();
    desc_.name = display_name_.c_str();
    desc_.fps_num = 60;
    desc_.fps_den = 1;
    desc_.audio_sample_rate = kTargetAudioSampleRate;

    api_ = czx_core_api_t{
        this,
        lr_get_desc,
        lr_create,
        lr_destroy,
        lr_reset,
        lr_load_media,
        lr_eject_media,
        lr_run_frame,
        lr_on_key,
        lr_on_joystick,
        lr_set_option,
        nullptr,
        nullptr,
        nullptr,
        lr_get_extensions,
        lr_get_media_label,
        lr_set_data_dir,
        lr_get_last_error,
        lr_get_drive_count,
        lr_get_drive_name,
        lr_get_drive_media_label,
        lr_load_media_in_drive,
        lr_load_dual_media,
        lr_eject_drive,
        lr_on_mouse,
        lr_get_activity,
    };

    loaded_ = true;
    return true;
}

}  // namespace czx::core
