#include "core/libretro_core.hpp"

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
#include <vector>

namespace fs = std::filesystem;

namespace czx::core {

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
    if (lib_name && lib_name[0] != '\0') return lib_name;
    return id;
}

bool is_floppy_disk_ext(const std::string& path) {
    const std::string ext = to_lower_copy(fs::path(path).extension().string());
    return ext == ".dsk" || ext == ".do" || ext == ".po" || ext == ".nib" ||
           ext == ".woz" || ext == ".2mg" || ext == ".2img" || ext == ".hdv" ||
           ext == ".d64" || ext == ".g64" || ext == ".x64" || ext == ".d71" ||
           ext == ".d81" || ext == ".d82";
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
    LibretroCoreInstance(LibretroCoreLoader* loader, const czx_host_t* host,
                         const retro_system_info& sys_info, std::string extensions)
        : loader_(loader), sys_info_(sys_info), extensions_(std::move(extensions)) {
        if (host) host_ = *host;
        is_applewin_ = (loader_->id().find("applewin") != std::string::npos);
        is_vice_ = (loader_->id().find("vice") != std::string::npos);
        if (const char* dd = std::getenv("CZX_DATA_DIR"); dd && dd[0] != '\0') {
            set_data_dir(dd);
        } else if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
            set_data_dir((fs::path(home) / "czx").string());
        } else {
            set_data_dir("/tmp/czx");
        }
    }

    ~LibretroCoreInstance() {
        const auto& syms = loader_->symbols();
        if (game_loaded_ && syms.retro_unload_game) {
            syms.retro_unload_game();
            game_loaded_ = false;
        }
        if (initialized_ && syms.retro_deinit) {
            syms.retro_deinit();
            initialized_ = false;
        }
        if (g_active_instance == this) {
            g_active_instance = nullptr;
        }
    }

    bool init() {
        g_active_instance = this;
        const auto& syms = loader_->symbols();
        loader_->set_last_error({});
        shutdown_requested_ = false;

        std::string rom_err;
        if (!check_required_roms(rom_err)) {
            loader_->set_last_error(rom_err);
            log(3, rom_err.c_str());
            return false;
        }

        if (is_applewin_) {
            setenv("CZX_APPLE2_ROM_DIR", system_dir_.c_str(), 1);
        }

        seed_default_options();
        load_user_config();

        syms.retro_set_environment(cb_env);
        syms.retro_set_video_refresh(cb_video);
        syms.retro_set_audio_sample(cb_audio_sample);
        syms.retro_set_audio_sample_batch(cb_audio_batch);
        syms.retro_set_input_poll(cb_input_poll);
        syms.retro_set_input_state(cb_input_state);

        syms.retro_init();
        initialized_ = true;

        if (support_no_game_) {
            if (!syms.retro_load_game(nullptr) || shutdown_requested_) {
                if (!loader_->last_error()) {
                    loader_->set_last_error("Core init failed.\nCheck external ROM files in:\n" + system_dir_);
                }
                log(3, loader_->last_error());
                return false;
            }
            game_loaded_ = true;
            refresh_av_info();
        }
        return true;
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
        if (loader_->symbols().retro_reset) {
            loader_->symbols().retro_reset();
        }
    }

    bool load_media(const std::string& path) {
        g_active_instance = this;
        if (path.empty()) return false;

        // Hot-swap floppy images when already running and core exposes disk control.
        if (has_media_ && has_disk_control() && is_floppy_disk_ext(path)) {
            if (hot_swap_disk(path)) {
                media_path_ = path;
                media_label_ = fs::path(path).filename().string();
                ejected_ = false;
                return true;
            }
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
        if (!sys_info_.need_fullpath) {
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
        if (!game_loaded_) return;
        audio_out_frame_.clear();
        loader_->symbols().retro_run();
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

private:
    struct HeldKey {
        bool down = false;
        uint32_t retrok = RETROK_UNKNOWN;
        bool used_synth_shift = false;
    };

    LibretroCoreLoader* loader_;
    czx_host_t host_{};
    retro_system_info sys_info_{};
    retro_system_av_info av_info_{};
    std::string extensions_;
    bool is_applewin_ = false;
    bool is_vice_ = false;

    std::string data_dir_;
    std::string system_dir_;
    std::string save_dir_;
    std::string media_path_;
    std::string media_label_ = "None";

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
    std::array<HeldKey, 256> active_keys_{};
    std::array<bool, RETROK_LAST> retro_key_down_{};
    bool phys_shift_held_ = false;
    int synthetic_shift_count_ = 0;

    std::vector<uint16_t> cached_16_;
    std::vector<uint32_t> cached_32_;
    uint16_t cached_w_ = 0;
    uint16_t cached_h_ = 0;
    uint32_t cached_pitch_ = 0;
    czx_pixel_format_t cached_fmt_ = CZX_PIXEL_RGB565;

    std::vector<int16_t> audio_out_frame_;
    double audio_resample_pos_ = 0.0;

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
        if (is_vice_) {
            options_["vice_resid_sampling"] = "fast";
            options_["vice_drive_true_emulation"] = "enabled";
            options_["vice_zoom_mode"] = "none";
            options_["vice_c64_model"] = "C64 PAL auto";
            options_["vice_joyport"] = "2";
            options_["vice_autoloadwarp"] = "enabled";
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
        if (!sys_info_.need_fullpath) {
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
        retro_key_down_.fill(false);
        joy_state_ = {0, 0};
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
                const auto* src = static_cast<const uint8_t*>(data);
                for (unsigned y = 0; y < height; ++y) {
                    std::memcpy(cached_32_.data() + static_cast<size_t>(y) * width,
                                src + static_cast<size_t>(y) * pitch,
                                static_cast<size_t>(width) * 4u);
                }
            } else {
                cached_fmt_ = CZX_PIXEL_RGB565;
                cached_pitch_ = static_cast<uint32_t>(width) * 2u;
                cached_16_.resize(static_cast<size_t>(width) * height);
                const auto* src = static_cast<const uint8_t*>(data);
                if (pixel_format_ == RETRO_PIXEL_FORMAT_RGB565) {
                    for (unsigned y = 0; y < height; ++y) {
                        std::memcpy(cached_16_.data() + static_cast<size_t>(y) * width,
                                    src + static_cast<size_t>(y) * pitch,
                                    static_cast<size_t>(width) * 2u);
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
                                    : 44100.0;
        if (std::abs(src_rate - 44100.0) < 1.0) {
            audio_out_frame_.insert(audio_out_frame_.end(), data, data + frames * 2u);
            return;
        }

        const double step = src_rate / 44100.0;
        while (audio_resample_pos_ < static_cast<double>(frames)) {
            const size_t i0 = static_cast<size_t>(audio_resample_pos_);
            const size_t i1 = std::min(i0 + 1, frames - 1);
            const double frac = audio_resample_pos_ - static_cast<double>(i0);
            const double l = static_cast<double>(data[i0 * 2 + 0]) * (1.0 - frac) +
                             static_cast<double>(data[i1 * 2 + 0]) * frac;
            const double r = static_cast<double>(data[i0 * 2 + 1]) * (1.0 - frac) +
                             static_cast<double>(data[i1 * 2 + 1]) * frac;
            audio_out_frame_.push_back(static_cast<int16_t>(std::clamp(std::lround(l), -32768L, 32767L)));
            audio_out_frame_.push_back(static_cast<int16_t>(std::clamp(std::lround(r), -32768L, 32767L)));
            audio_resample_pos_ += step;
        }
        audio_resample_pos_ -= static_cast<double>(frames);
    }

    int16_t query_input(unsigned port, unsigned device, unsigned, unsigned id) const {
        if (device == RETRO_DEVICE_JOYPAD) {
            if (port >= joy_state_.size()) return 0;
            const uint32_t b = joy_state_[port];
            uint16_t mask = 0;
            if (b & CZX_JOY_FIRE1) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_B);
            if (b & CZX_JOY_FIRE2) mask |= (1u << RETRO_DEVICE_ID_JOYPAD_A);
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
                if (data) *static_cast<uint64_t*>(data) = (1ULL << RETRO_DEVICE_JOYPAD) | (1ULL << RETRO_DEVICE_KEYBOARD);
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

            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
                const auto* defs = static_cast<const retro_core_option_definition*>(data);
                while (defs && defs->key) {
                    if (options_.find(defs->key) == options_.end()) {
                        const char* def = defs->default_value ? defs->default_value : defs->values[0].value;
                        if (def) options_[defs->key] = def;
                    }
                    ++defs;
                }
                return true;
            }

            case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
                const auto* v2 = static_cast<const retro_core_options_v2*>(data);
                const auto* defs = v2 ? v2->definitions : nullptr;
                while (defs && defs->key) {
                    if (options_.find(defs->key) == options_.end()) {
                        const char* def = defs->default_value ? defs->default_value : defs->values[0].value;
                        if (def) options_[defs->key] = def;
                    }
                    ++defs;
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

            case RETRO_ENVIRONMENT_SET_MESSAGE:
            case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
            case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
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
    static void RETRO_CALLCONV cb_input_poll(void) {}
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
};

const czx_core_desc_t* lr_get_desc(const czx_core_api_t* api) {
    const auto* loader = static_cast<const LibretroCoreLoader*>(api ? api->api_data : nullptr);
    return loader ? loader->desc() : nullptr;
}

czx_core_instance_t* lr_create(const czx_core_api_t* api, const czx_host_t* host) {
    auto* loader = static_cast<LibretroCoreLoader*>(api ? api->api_data : nullptr);
    if (!loader) return nullptr;
    auto inst = std::make_unique<LibretroCoreInstance>(
        loader, host, loader->system_info(), loader->extensions());
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

}  // namespace

LibretroCoreLoader::LibretroCoreLoader() = default;

LibretroCoreLoader::~LibretroCoreLoader() {
    if (dl_) {
        dlclose(dl_);
        dl_ = nullptr;
    }
}

void LibretroCoreLoader::update_av_timing(double fps, double) {
    if (fps > 1.0 && fps < 240.0) {
        desc_.fps_num = static_cast<uint32_t>(std::lround(fps * 1000.0));
        desc_.fps_den = 1000u;
    }
    desc_.audio_sample_rate = 44100u;
}

bool LibretroCoreLoader::load(const std::string& so_path) {
    if (dl_) {
        dlclose(dl_);
        dl_ = nullptr;
    }
    loaded_ = false;
    so_path_ = so_path;

    void* dl = dlopen(so_path.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!dl) {
        std::fprintf(stderr, "czx: dlopen(%s) failed: %s\n", so_path.c_str(), dlerror());
        return false;
    }

    auto syms = std::make_unique<Symbols>();
    auto resolve = [&](auto& fn, const char* name, bool required) -> bool {
        using FnType = std::remove_reference_t<decltype(fn)>;
        fn = reinterpret_cast<FnType>(dlsym(dl, name));
        if (required && !fn) {
            std::fprintf(stderr, "czx: missing symbol %s in %s\n", name, so_path.c_str());
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

    sys_info_ = {};
    syms->retro_get_system_info(&sys_info_);

    id_ = derive_core_id(so_path);
    display_name_ = derive_display_name(id_, sys_info_.library_name);
    extensions_ = sys_info_.valid_extensions ? sys_info_.valid_extensions : "";
    // Copy library strings safely out of core pointer if needed: store in desc_ using our owned strings.
    dl_ = dl;
    syms_ = std::move(syms);

    desc_.api_version = CZX_CORE_API_VERSION;
    desc_.id = id_.c_str();
    desc_.name = display_name_.c_str();
    desc_.fps_num = 60;
    desc_.fps_den = 1;
    desc_.audio_sample_rate = 44100;

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
    };

    loaded_ = true;
    return true;
}

}  // namespace czx::core
