#include "core/g800_core.hpp"

#include "g800_bridge.h"
#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace czx::core {

namespace {

constexpr int kLcdScreenW = 320;
constexpr int kLcdScreenH = 170;
constexpr uint32_t kAudioOutRate = 48000u;
constexpr size_t kAudioOutFrames = 800u;  // 48000 Hz / 60 fps

enum StatusIndex : int {
    kStatusRun = 0,
    kStatusPro = 1,
    kStatusText = 2,
    kStatusCasl = 3,
    kStatusStat = 4,
    kStatus2ndF = 5,
    kStatusM = 6,
    kStatusCaps = 7,
    kStatusKana = 8,
    kStatusSyo = 9,
    kStatusDe = 10,
    kStatusG = 11,
    kStatusRad = 12,
    kStatusConst = 13,
    kStatusPrint = 14,
    kStatusBusy = 15,
    kStatusBatt = 16,
    kStatusE = 17,
};

constexpr std::array<const char*, 3> kPresetNames = {{
    "PC-G850 (144x48)",
    "PC-G815 (144x32)",
    "PC-E200 (120x32)",
}};

uint16_t blend_rgb565(uint8_t r0, uint8_t g0, uint8_t b0,
                      uint8_t r1, uint8_t g1, uint8_t b1,
                      int num, int den) noexcept {
    const int n = std::clamp(num, 0, den);
    const int c0 = den - n;
    const uint8_t r = static_cast<uint8_t>((r0 * c0 + r1 * n) / den);
    const uint8_t g = static_cast<uint8_t>((g0 * c0 + g1 * n) / den);
    const uint8_t b = static_cast<uint8_t>((b0 * c0 + b1 * n) / den);
    return czx::video::pack_rgb565(r, g, b);
}

class G800CoreInstance;
G800CoreInstance* g_active_g800 = nullptr;

class G800CoreInstance {
public:
    explicit G800CoreInstance(const czx_host_t* host)
        : fb_(static_cast<size_t>(kLcdScreenW * kLcdScreenH), 0),
          audio_out_(kAudioOutFrames * 2u, 0) {
        if (host) host_ = *host;
        if (const char* dd = std::getenv("CZX_DATA_DIR"); dd && dd[0] != '\0') {
            data_dir_ = dd;
        } else if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
            data_dir_ = (fs::path(home) / "czx").string();
        } else {
            data_dir_ = "/tmp/czx";
        }
        if (host_.initial_media && host_.initial_media[0] != '\0') {
            initial_media_ = host_.initial_media;
            media_label_ = fs::path(initial_media_).filename().string();
        }
    }

    ~G800CoreInstance() {
        stop_session();
        if (g_active_g800 == this) {
            g_active_g800 = nullptr;
        }
    }

    bool init() {
        ensure_dirs();
        return start_session(initial_media_, false);
    }

    void set_data_dir(const std::string& dir) {
        if (dir.empty() || dir == data_dir_) return;
        data_dir_ = dir;
        ensure_dirs();
        (void)start_session(initial_media_, false);
    }

    void reset(bool hard) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            key_queue_.clear();
            held_keys_.fill(0);
            held_native_keys_.fill(false);
            joy_state_ = 0;
        }
        (void)start_session(initial_media_, hard);
    }

    uint32_t get_preset_count() const noexcept {
        return static_cast<uint32_t>(kPresetNames.size());
    }

    const char* get_preset_name(uint32_t index) const noexcept {
        return (index < kPresetNames.size()) ? kPresetNames[index] : nullptr;
    }

    bool select_preset(uint32_t index) {
        const uint32_t next = index % static_cast<uint32_t>(kPresetNames.size());
        if (next == model_index_ && emu_thread_.joinable()) {
            return true;
        }
        model_index_ = next;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            key_queue_.clear();
            held_keys_.fill(0);
            held_native_keys_.fill(false);
            joy_state_ = 0;
        }
        return start_session(initial_media_, false);
    }

    bool load_media(const std::string& path) {
        if (path.empty()) return false;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) return false;

        initial_media_ = path;
        media_label_ = fs::path(path).filename().string();
        {
            std::lock_guard<std::mutex> lock(mtx_);
            pending_media_ = path;
            has_pending_media_ = true;
        }
        return true;
    }

    const char* get_media_label() const noexcept {
        return media_label_.c_str();
    }

    bool get_activity(czx_activity_t* out) const noexcept {
        if (!out) return false;
        out->flags = (last_sio_mode_ != 0) ? static_cast<uint32_t>(CZX_ACTIVITY_SIO) : 0u;
        out->tape_percent = -1;
        out->drive_mask = 0u;
        return out->flags != 0u;
    }

    void on_key(const czx_key_event_t& ev) {
        if (ev.native_key != 0u) {
            const size_t nidx = static_cast<size_t>(ev.native_key);
            if (nidx >= held_native_keys_.size()) return;
            const uint16_t gkey = static_cast<uint16_t>(ev.native_key);
            if (ev.pressed) {
                if (ev.repeat) return;
                std::lock_guard<std::mutex> lock(mtx_);
                if (!held_native_keys_[nidx]) {
                    held_native_keys_[nidx] = true;
                    key_queue_.push_back({gkey, 1});
                }
            } else {
                std::lock_guard<std::mutex> lock(mtx_);
                if (held_native_keys_[nidx]) {
                    held_native_keys_[nidx] = false;
                    key_queue_.push_back({gkey, 0});
                }
            }
            return;
        }

        const size_t idx = static_cast<size_t>(ev.key);
        if (idx >= held_keys_.size()) return;

        if (ev.pressed) {
            if (ev.repeat) return;
            const uint16_t gkey = czx_g800_map_key(static_cast<int>(ev.key), ev.mods, ev.codepoint);
            if (gkey == 0u) return;
            std::lock_guard<std::mutex> lock(mtx_);
            if (held_keys_[idx] != 0u && held_keys_[idx] != gkey) {
                key_queue_.push_back({held_keys_[idx], 0});
            }
            held_keys_[idx] = gkey;
            key_queue_.push_back({gkey, 1});
        } else {
            std::lock_guard<std::mutex> lock(mtx_);
            const uint16_t gkey = held_keys_[idx];
            if (gkey != 0u) {
                held_keys_[idx] = 0u;
                key_queue_.push_back({gkey, 0});
            }
        }
    }

    void on_joystick(uint32_t port, uint32_t buttons) {
        if (port != 0u) return;
        constexpr std::array<uint32_t, 6> kBits = {{
            CZX_JOY_UP, CZX_JOY_DOWN, CZX_JOY_LEFT, CZX_JOY_RIGHT, CZX_JOY_FIRE1, CZX_JOY_FIRE2,
        }};
        std::lock_guard<std::mutex> lock(mtx_);
        const uint32_t diff = joy_state_ ^ buttons;
        if (diff == 0u) return;
        for (const uint32_t bit : kBits) {
            if ((diff & bit) != 0u) {
                const uint16_t gkey = czx_g800_map_joy_key(static_cast<int>(bit));
                if (gkey != 0u) {
                    key_queue_.push_back({gkey, (buttons & bit) != 0u ? 1 : 0});
                }
            }
        }
        joy_state_ = buttons;
    }

    void run_frame() {
        czx_g800_lcd_snapshot_t lcd{};
        std::vector<uint8_t> sound_raw;
        bool have_frame = false;

        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [&]() { return frame_ready_ || worker_exited_ || stop_requested_; });
            if (frame_ready_) {
                lcd = published_lcd_;
                sound_raw = std::move(published_sound_);
                published_sound_.clear();
                frame_ready_ = false;
                have_frame = true;
            }
            if (!worker_exited_ && !stop_requested_) {
                ++step_budget_;
                cv_.notify_all();
            }
        }

        if (!have_frame) return;

        last_sio_mode_ = lcd.sio_mode;
        render_lcd(lcd);

        if (host_.video_frame) {
            czx_frame_t frame{};
            frame.data = fb_.data();
            frame.pitch_bytes = static_cast<uint32_t>(kLcdScreenW * 2);
            frame.width = static_cast<uint16_t>(kLcdScreenW);
            frame.height = static_cast<uint16_t>(kLcdScreenH);
            frame.crop_x = 0;
            frame.crop_y = 0;
            frame.crop_w = 0;
            frame.crop_h = 0;
            frame.par_num = 1;
            frame.par_den = 1;
            frame.format = CZX_PIXEL_RGB565;
            frame.flags = 0;
            host_.video_frame(host_.user_data, &frame);
        }

        if (host_.audio_samples) {
            resample_buzzer(sound_raw);
            host_.audio_samples(host_.user_data, audio_out_.data(), kAudioOutFrames);
        }
    }

    void worker_on_frame(const czx_g800_lcd_snapshot_t* lcd, const uint8_t* sound_buf, int sound_len) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (lcd) {
            published_lcd_ = *lcd;
        }
        if (sound_buf && sound_len > 0) {
            published_sound_.assign(sound_buf, sound_buf + sound_len);
        } else {
            published_sound_.clear();
        }
        frame_ready_ = true;
        cv_.notify_all();
    }

    int worker_wait_next_frame() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&]() { return step_budget_ > 0 || stop_requested_; });
        if (stop_requested_) {
            return 0;
        }
        --step_budget_;
        return 1;
    }

    int worker_pop_key(uint16_t* gkey_out, int* pressed_out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (key_queue_.empty()) return 0;
        const auto ev = key_queue_.front();
        key_queue_.pop_front();
        if (gkey_out) *gkey_out = ev.gkey;
        if (pressed_out) *pressed_out = ev.pressed;
        return 1;
    }

    int worker_pop_media(char* path_out, int path_cap) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!has_pending_media_ || path_cap <= 1) return 0;
        has_pending_media_ = false;
        const size_t n = std::min(pending_media_.size(), static_cast<size_t>(path_cap - 1));
        std::memcpy(path_out, pending_media_.data(), n);
        path_out[n] = '\0';
        pending_media_.clear();
        return 1;
    }

    void worker_log(int level, const char* msg) const {
        if (host_.log) {
            host_.log(host_.user_data, level, msg);
        }
    }

private:
    struct QueuedKey {
        uint16_t gkey = 0;
        int pressed = 0;
    };

    void ensure_dirs() const {
        std::error_code ec;
        const fs::path root(data_dir_);
        fs::create_directories(root / "roms" / "g800", ec);
        fs::create_directories(root / "media" / "g800", ec);
        fs::create_directories(root / "saves", ec);
    }

    static bool has_base_rom_ci(const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return false;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
            std::string name = entry.path().filename().string();
            for (char& ch : name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if ((name == "base.bin" || name == "base.txt") && entry.file_size(ec) > 0) {
                return true;
            }
        }
        return false;
    }

    std::string resolve_rom_dir() const {
        const fs::path base_dir = fs::path(data_dir_) / "roms" / "g800";
        const char* sub = (model_index_ == 1u) ? "g815" : (model_index_ == 2u) ? "e200" : "g850";
        const fs::path model_dir = base_dir / sub;
        if (has_base_rom_ci(model_dir)) {
            return model_dir.string();
        }
        return base_dir.string();
    }

    bool start_session(const std::string& media_path, bool cold_boot) {
        stop_session();
        g_active_g800 = this;

        const char* tag = (model_index_ == 1u) ? "g815" : (model_index_ == 2u) ? "e200" : "g850";
        const std::string rom_dir = resolve_rom_dir();
        const std::string ram_path_sim = (fs::path(data_dir_) / "saves" / (std::string("g800_") + tag + ".ram")).string();
        const std::string ram_path_rom =
            (fs::path(data_dir_) / "saves" / (std::string("g800_") + tag + "_rom.ram")).string();
        const std::string basic_path = (fs::path(data_dir_) / "saves" / (std::string("g800_") + tag + "_basic.txt")).string();
        const int model = static_cast<int>(model_index_);

        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_requested_ = false;
            worker_exited_ = false;
            frame_ready_ = false;
            step_budget_ = 0;
            has_pending_media_ = false;
            pending_media_.clear();
            last_sio_mode_ = 0;
            dc_prev_in_ = 0.0f;
            dc_prev_out_ = 0.0f;
        }

        emu_thread_ = std::thread([this, model, rom_dir, ram_path_sim, ram_path_rom, basic_path, media_path, cold_boot]() {
            (void)czx_g800_session_run(model,
                                       rom_dir.c_str(),
                                       ram_path_sim.c_str(),
                                       ram_path_rom.c_str(),
                                       basic_path.c_str(),
                                       media_path.empty() ? nullptr : media_path.c_str(),
                                       cold_boot ? 1 : 0);
            std::lock_guard<std::mutex> lock(mtx_);
            worker_exited_ = true;
            cv_.notify_all();
        });

        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::milliseconds(500), [&]() {
            return frame_ready_ || worker_exited_;
        });
        return !worker_exited_ || frame_ready_;
    }

    void stop_session() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_requested_ = true;
            cv_.notify_all();
        }
        if (emu_thread_.joinable()) {
            emu_thread_.join();
        }
    }

    void resample_buzzer(const std::vector<uint8_t>& src) {
        if (src.empty()) {
            std::fill(audio_out_.begin(), audio_out_.end(), 0);
            return;
        }
        const size_t n = src.size();
        const double scale = static_cast<double>(n) / static_cast<double>(kAudioOutFrames);
        for (size_t i = 0; i < kAudioOutFrames; ++i) {
            const double pos = static_cast<double>(i) * scale;
            const size_t i0 = std::min(static_cast<size_t>(pos), n - 1u);
            const size_t i1 = std::min(i0 + 1u, n - 1u);
            const float frac = static_cast<float>(pos - static_cast<double>(i0));
            const float s0 = static_cast<float>(static_cast<int8_t>(src[i0]));
            const float s1 = static_cast<float>(static_cast<int8_t>(src[i1]));
            const float x = (s0 + (s1 - s0) * frac) * 180.0f;
            const float y = x - dc_prev_in_ + 0.992f * dc_prev_out_;
            dc_prev_in_ = x;
            dc_prev_out_ = y;
            const int32_t clamped = std::clamp(static_cast<int32_t>(std::lround(y)), -32767, 32767);
            const int16_t s16 = static_cast<int16_t>(clamped);
            audio_out_[i * 2u + 0u] = s16;
            audio_out_[i * 2u + 1u] = s16;
        }
    }

    void render_lcd(const czx_g800_lcd_snapshot_t& lcd) {
        using namespace czx::video;

        const uint16_t bezel = pack_rgb565(26, 32, 28);
        const uint16_t rim = pack_rgb565(132, 160, 118);

        int voltage = 0x0f;
        if ((lcd.lcd_black && lcd.lcd_white) || lcd.lcd_disabled) {
            voltage = 0;
        } else if (lcd.lcd_black) {
            voltage = 32;
        } else if (lcd.lcd_white) {
            voltage = 0;
        } else {
            voltage = std::clamp<int>(static_cast<int>(lcd.lcd_contrast) + (lcd.lcd_dark ? 2 : 0) + 12, 0, 32);
        }

        const uint8_t off_r = lcd.lcd_reverse ? 10 : 176;
        const uint8_t off_g = lcd.lcd_reverse ? 36 : 210;
        const uint8_t off_b = lcd.lcd_reverse ? 20 : 156;
        const uint8_t on_r = lcd.lcd_reverse ? 204 : 12;
        const uint8_t on_g = lcd.lcd_reverse ? 236 : 40;
        const uint8_t on_b = lcd.lcd_reverse ? 184 : 22;

        const uint16_t lcd_back = pack_rgb565(202, 232, 180);
        const uint16_t lcd_off = blend_rgb565(202, 232, 180, off_r, off_g, off_b, voltage, 32);
        const uint16_t lcd_on = blend_rgb565(202, 232, 180, on_r, on_g, on_b, voltage, 32);

        std::fill(fb_.begin(), fb_.end(), bezel);
        Rgb565Canvas canvas(fb_.data(), kLcdScreenW, kLcdScreenH);
        canvas.draw_rect(6, 4, 308, 162, rim);
        canvas.fill_rect(7, 5, 306, 160, lcd_back);

        // Top pocket-computer status annunciators.
        auto draw_status = [&](int x, int y, const char* label, int bit_idx) {
            const bool active = (lcd.status_bits & (1u << bit_idx)) != 0u;
            canvas.draw_text(x, y, label, active ? lcd_on : lcd_off);
        };
        draw_status(16, 10, "BUSY", kStatusBusy);
        draw_status(50, 10, "2ndF", kStatus2ndF);
        draw_status(84, 10, "CAPS", kStatusCaps);
        draw_status(118, 10, "KANA", kStatusKana);
        draw_status(152, 10, "SML", kStatusSyo);
        draw_status(180, 10, "D", kStatusDe);
        draw_status(188, 10, "G", kStatusG);
        draw_status(196, 10, "RAD", kStatusRad);
        draw_status(224, 10, "CONST", kStatusConst);
        draw_status(264, 10, "M", kStatusM);
        draw_status(278, 10, "BAT", kStatusBatt);
        canvas.draw_hline(14, 21, 292, lcd_off);

        // Main dot-matrix at crisp 2x integer scale (24 cols x 6..8 rows).
        const int cols = std::clamp(lcd.lcd_cols, 1, 24);
        const int rows = std::clamp(lcd.lcd_rows, 1, 6);
        const int cw = std::clamp(lcd.cell_width, 1, 6);
        const int ch = std::clamp(lcd.cell_height, 1, 8);
        const int vrows = std::max(1, lcd.vram_rows);
        const int vw = std::max(1, lcd.vram_width);
        const int top_row = lcd.lcd_top / 8;
        const int shift = lcd.lcd_top % 8;

        const int mat_w = cols * 6 * 2;
        const int mat_h = rows * 8 * 2;
        const int x0 = (kLcdScreenW - mat_w) / 2;
        const int y0 = (rows <= 4) ? 49 : 31;

        canvas.fill_rect(x0 - 2, y0 - 2, mat_w + 4, mat_h + 4, lcd_back);

        for (int row = 0; row < rows; ++row) {
            const int vr0 = (row + top_row) % vrows;
            const int vr1 = (vr0 + 1) % vrows;
            for (int col = 0; col < cols; ++col) {
                for (int cx = 0; cx < cw; ++cx) {
                    const size_t off0 = static_cast<size_t>(vr0 * vw + col * cw + cx);
                    const size_t off1 = static_cast<size_t>(vr1 * vw + col * cw + cx);
                    const uint8_t b0 = (off0 < sizeof(lcd.vram)) ? lcd.vram[off0] : 0u;
                    const uint8_t b1 = (shift != 0 && off1 < sizeof(lcd.vram)) ? lcd.vram[off1] : 0u;
                    const uint8_t pat = static_cast<uint8_t>((b0 >> shift) | ((shift != 0) ? (b1 << (8 - shift)) : 0));
                    const int px = x0 + (col * 6 + cx) * 2;
                    for (int bit = 0; bit < ch; ++bit) {
                        const uint16_t c = ((pat & (1u << bit)) != 0u) ? lcd_on : lcd_off;
                        const int py = y0 + (row * 8 + bit) * 2;
                        fb_[static_cast<size_t>(py) * kLcdScreenW + static_cast<size_t>(px)] = c;
                        fb_[static_cast<size_t>(py) * kLcdScreenW + static_cast<size_t>(px) + 1u] = c;
                        fb_[static_cast<size_t>(py + 1) * kLcdScreenW + static_cast<size_t>(px)] = c;
                        fb_[static_cast<size_t>(py + 1) * kLcdScreenW + static_cast<size_t>(px) + 1u] = c;
                    }
                }
            }
        }

        // Bottom pocket-computer mode & runtime indicators.
        canvas.draw_hline(14, 140, 292, lcd_off);
        draw_status(16, 146, "RUN", kStatusRun);
        draw_status(42, 146, "PRO", kStatusPro);
        draw_status(68, 146, "TEXT", kStatusText);
        draw_status(100, 146, "CASL", kStatusCasl);
        draw_status(132, 146, "STAT", kStatusStat);
        draw_status(164, 146, "PRT", kStatusPrint);

        if (lcd.sio_mode != 0) {
            canvas.draw_text(192, 146, "SIO", lcd_on);
        }
        const char* mtag = (model_index_ == 1u) ? "G815" : (model_index_ == 2u) ? "E200" : "G850";
        const char* state_tag = (lcd.lcd_disabled || lcd.cpu_halted) ? "OFF" : (lcd.use_rom ? "ROM" : "SIM");
        char badge[24];
        std::snprintf(badge, sizeof(badge), "[%s %s]", mtag, state_tag);
        const int badge_x = 306 - static_cast<int>(std::strlen(badge)) * 6;
        const uint16_t badge_col = pack_rgb565(36, 68, 44);
        canvas.draw_text(badge_x, 146, badge, badge_col);
    }

    czx_host_t host_{};
    std::string data_dir_;
    std::string initial_media_;
    std::string media_label_ = "None";
    uint32_t model_index_ = 0;

    std::vector<uint16_t> fb_;
    std::vector<int16_t> audio_out_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread emu_thread_;
    bool stop_requested_ = false;
    bool worker_exited_ = true;
    bool frame_ready_ = false;
    int step_budget_ = 0;
    czx_g800_lcd_snapshot_t published_lcd_{};
    std::vector<uint8_t> published_sound_;

    std::deque<QueuedKey> key_queue_;
    std::array<uint16_t, 256> held_keys_{};
    std::array<bool, 256> held_native_keys_{};
    uint32_t joy_state_ = 0;
    std::string pending_media_;
    bool has_pending_media_ = false;
    int last_sio_mode_ = 0;

    float dc_prev_in_ = 0.0f;
    float dc_prev_out_ = 0.0f;
};

const czx_core_desc_t kG800Desc = {
    CZX_CORE_API_VERSION,
    "g800",
    "PC-G850 (g800)",
    60,
    1,
    kAudioOutRate,
};

const czx_core_desc_t* g800_get_desc(const czx_core_api_t*) {
    return &kG800Desc;
}

czx_core_instance_t* g800_create(const czx_core_api_t*, const czx_host_t* host) {
    auto inst = std::make_unique<G800CoreInstance>(host);
    if (!inst->init()) {
        return nullptr;
    }
    return reinterpret_cast<czx_core_instance_t*>(inst.release());
}

void g800_destroy(czx_core_instance_t* inst) {
    delete reinterpret_cast<G800CoreInstance*>(inst);
}

void g800_reset(czx_core_instance_t* inst, bool hard) {
    if (inst) reinterpret_cast<G800CoreInstance*>(inst)->reset(hard);
}

bool g800_load_media(czx_core_instance_t* inst, const char* path) {
    return inst && path && reinterpret_cast<G800CoreInstance*>(inst)->load_media(path);
}

void g800_run_frame(czx_core_instance_t* inst) {
    if (inst) reinterpret_cast<G800CoreInstance*>(inst)->run_frame();
}

void g800_on_key(czx_core_instance_t* inst, const czx_key_event_t* ev) {
    if (inst && ev) reinterpret_cast<G800CoreInstance*>(inst)->on_key(*ev);
}

void g800_on_joystick(czx_core_instance_t* inst, uint32_t port, uint32_t buttons) {
    if (inst) reinterpret_cast<G800CoreInstance*>(inst)->on_joystick(port, buttons);
}

uint32_t g800_get_preset_count(const czx_core_instance_t* inst) {
    return inst ? reinterpret_cast<const G800CoreInstance*>(inst)->get_preset_count()
                : static_cast<uint32_t>(kPresetNames.size());
}

const char* g800_get_preset_name(const czx_core_instance_t* inst, uint32_t index) {
    if (inst) {
        return reinterpret_cast<const G800CoreInstance*>(inst)->get_preset_name(index);
    }
    return (index < kPresetNames.size()) ? kPresetNames[index] : nullptr;
}

bool g800_select_preset(czx_core_instance_t* inst, uint32_t index) {
    return inst && reinterpret_cast<G800CoreInstance*>(inst)->select_preset(index);
}

const char* g800_get_extensions(const czx_core_api_t*) {
    return "ihx|hex|bas|txt";
}

const char* g800_get_media_label(const czx_core_instance_t* inst) {
    return inst ? reinterpret_cast<const G800CoreInstance*>(inst)->get_media_label() : "None";
}

void g800_set_data_dir(czx_core_instance_t* inst, const char* dir) {
    if (inst && dir) reinterpret_cast<G800CoreInstance*>(inst)->set_data_dir(dir);
}

bool g800_get_activity(const czx_core_instance_t* inst, czx_activity_t* out) {
    return inst && reinterpret_cast<const G800CoreInstance*>(inst)->get_activity(out);
}

const czx_core_api_t kG800Api = {
    nullptr,
    g800_get_desc,
    g800_create,
    g800_destroy,
    g800_reset,
    g800_load_media,
    nullptr,
    g800_run_frame,
    g800_on_key,
    g800_on_joystick,
    nullptr,
    g800_get_preset_count,
    g800_get_preset_name,
    g800_select_preset,
    g800_get_extensions,
    g800_get_media_label,
    g800_set_data_dir,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    g800_get_activity,
    nullptr,
};

extern "C" void czx_g800_host_on_frame(const czx_g800_lcd_snapshot_t* lcd,
                                       const uint8_t* sound_buf,
                                       int sound_len) {
    if (g_active_g800) {
        g_active_g800->worker_on_frame(lcd, sound_buf, sound_len);
    }
}

extern "C" int czx_g800_host_wait_next_frame(void) {
    return g_active_g800 ? g_active_g800->worker_wait_next_frame() : 0;
}

extern "C" int czx_g800_host_pop_key(uint16_t* gkey_out, int* pressed_out) {
    return g_active_g800 ? g_active_g800->worker_pop_key(gkey_out, pressed_out) : 0;
}

extern "C" int czx_g800_host_pop_media(char* path_out, int path_cap) {
    return g_active_g800 ? g_active_g800->worker_pop_media(path_out, path_cap) : 0;
}

extern "C" void czx_g800_host_log(int level, const char* msg) {
    if (g_active_g800) {
        g_active_g800->worker_log(level, msg);
    }
}

}  // namespace

const czx_core_api_t* get_g800_core_api() {
    return &kG800Api;
}

}  // namespace czx::core
