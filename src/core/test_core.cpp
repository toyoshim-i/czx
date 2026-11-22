#include "core/test_core.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace czx::core {

namespace {

struct Preset {
    const char* name;
    uint16_t width;
    uint16_t height;
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t crop_w;
    uint16_t crop_h;
    uint16_t par_num;
    uint16_t par_den;
    czx_pixel_format_t format;
    bool c64_theme;
};

constexpr std::array<Preset, 5> kPresets = {{
    {"Apple II 40-col", 280, 192, 0, 0, 0, 0, 1, 1, CZX_PIXEL_XRGB8888, false},
    {"Apple II 80-col", 560, 192, 0, 0, 0, 0, 1, 2, CZX_PIXEL_XRGB8888, false},
    {"C64 Active", 320, 200, 0, 0, 0, 0, 1, 1, CZX_PIXEL_ABGR8888, true},
    {"C64 PAL Border", 384, 272, 32, 36, 320, 200, 1, 1, CZX_PIXEL_ABGR8888, true},
    {"Hi-Res 640x400", 640, 400, 0, 0, 0, 0, 1, 1, CZX_PIXEL_RGB565, false},
}};

struct ColorRgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct TestCoreState {
    czx_host_t host{};
    uint32_t preset_idx = 0;
    uint64_t frame_num = 0;
    uint32_t joy_buttons = 0;
    int last_key = 0;
    uint32_t last_cp = 0;
    uint32_t last_mods = 0;
    std::string typed_line = "READY.";
    int click_samples_left = 0;

    std::vector<uint16_t> buf565;
    std::vector<uint32_t> buf32;
    std::vector<int16_t> audio_buf;

    void set_px(int x, int y, const Preset& p, ColorRgb c) {
        if (x < 0 || y < 0 || x >= p.width || y >= p.height) return;
        const size_t idx = static_cast<size_t>(y) * p.width + static_cast<size_t>(x);
        switch (p.format) {
            case CZX_PIXEL_RGB565:
                buf565[idx] = czx::video::pack_rgb565(c.r, c.g, c.b);
                break;
            case CZX_PIXEL_XRGB8888:
                buf32[idx] = (static_cast<uint32_t>(c.r) << 16) |
                             (static_cast<uint32_t>(c.g) << 8) |
                             static_cast<uint32_t>(c.b);
                break;
            case CZX_PIXEL_ABGR8888:
                buf32[idx] = 0xFF000000u |
                             (static_cast<uint32_t>(c.b) << 16) |
                             (static_cast<uint32_t>(c.g) << 8) |
                             static_cast<uint32_t>(c.r);
                break;
        }
    }

    void fill_rect(int x, int y, int w, int h, const Preset& p, ColorRgb c) {
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min<int>(p.width, x + w);
        const int y1 = std::min<int>(p.height, y + h);
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                set_px(px, py, p, c);
            }
        }
    }

    void draw_rect(int x, int y, int w, int h, const Preset& p, ColorRgb c) {
        fill_rect(x, y, w, 1, p, c);
        fill_rect(x, y + h - 1, w, 1, p, c);
        fill_rect(x, y, 1, h, p, c);
        fill_rect(x + w - 1, y, 1, h, p, c);
    }

    void draw_text(int x, int y, const char* s, const Preset& p, ColorRgb c, int scale = 1) {
        if (!s) return;
        int cx = x;
        for (const char* q = s; *q; ++q) {
            const uint8_t* cols = czx::video::glyph_columns_5x7(*q);
            for (int col = 0; col < 5; ++col) {
                for (int row = 0; row < 7; ++row) {
                    if ((cols[col] & (1u << row)) != 0) {
                        fill_rect(cx + col * scale, y + row * scale, scale, scale, p, c);
                    }
                }
            }
            cx += 6 * scale;
        }
    }
};

const czx_core_desc_t kDesc = {
    CZX_CORE_API_VERSION,
    "test_pattern",
    "CZX Resolution & Input Pattern",
    60,
    1,
    48000,
};

const czx_core_desc_t* test_get_desc(const czx_core_api_t*) {
    return &kDesc;
}

czx_core_instance_t* test_create(const czx_core_api_t*, const czx_host_t* host) {
    auto* st = new TestCoreState();
    if (host) st->host = *host;
    return reinterpret_cast<czx_core_instance_t*>(st);
}

void test_destroy(czx_core_instance_t* inst) {
    delete reinterpret_cast<TestCoreState*>(inst);
}

void test_reset(czx_core_instance_t* inst, bool hard) {
    auto* st = reinterpret_cast<TestCoreState*>(inst);
    if (!st) return;
    st->frame_num = 0;
    st->joy_buttons = 0;
    if (hard) {
        st->typed_line = "READY.";
        st->last_key = 0;
        st->last_cp = 0;
    }
}

bool test_load_media(czx_core_instance_t*, const char*) {
    return true;
}

void test_run_frame(czx_core_instance_t* inst) {
    auto* st = reinterpret_cast<TestCoreState*>(inst);
    if (!st) return;

    const Preset& p = kPresets[st->preset_idx];
    const size_t total = static_cast<size_t>(p.width) * p.height;
    if (p.format == CZX_PIXEL_RGB565) {
        st->buf565.resize(total);
    } else {
        st->buf32.resize(total);
    }

    const ColorRgb bg = p.c64_theme ? ColorRgb{53, 40, 121} : ColorRgb{8, 18, 12};
    const ColorRgb border_bg = p.c64_theme ? ColorRgb{108, 94, 181} : ColorRgb{20, 48, 30};
    const ColorRgb fg = p.c64_theme ? ColorRgb{136, 126, 224} : ColorRgb{96, 255, 128};
    const ColorRgb hi = ColorRgb{255, 245, 160};
    const ColorRgb white = ColorRgb{255, 255, 255};
    const ColorRgb red = ColorRgb{255, 80, 80};

    st->fill_rect(0, 0, p.width, p.height, p, p.crop_w > 0 ? border_bg : bg);

    const int ax = p.crop_w > 0 ? p.crop_x : 0;
    const int ay = p.crop_w > 0 ? p.crop_y : 0;
    const int aw = p.crop_w > 0 ? p.crop_w : p.width;
    const int ah = p.crop_w > 0 ? p.crop_h : p.height;

    if (p.crop_w > 0) {
        st->fill_rect(ax, ay, aw, ah, p, bg);
        st->draw_text(8, 8, "PAL BORDER REGION", p, white, 1);
    }

    // Active boundary ticks & corner pixels (for checking exact 1:1 / crop edges)
    st->draw_rect(ax, ay, aw, ah, p, fg);
    st->fill_rect(ax, ay, 5, 5, p, red);
    st->fill_rect(ax + aw - 5, ay, 5, 5, p, red);
    st->fill_rect(ax, ay + ah - 5, 5, 5, p, red);
    st->fill_rect(ax + aw - 5, ay + ah - 5, 5, 5, p, red);

    char title[96];
    std::snprintf(title, sizeof(title), "%s (%ux%u PAR %u:%u)",
                  p.name, p.width, p.height, p.par_num, p.par_den);
    st->draw_text(ax + 10, ay + 10, title, p, hi, 1);

    // 1px vertical line test pattern to demonstrate separable area resampling sharpness
    const int stripe_y = ay + 24;
    for (int x = ax + 10; x < ax + aw - 10; x += 2) {
        st->fill_rect(x, stripe_y, 1, 10, p, white);
    }

    // 80-column readability test line
    st->draw_text(ax + 10, ay + 40,
                  "10 PRINT \"CARDPUTER ZERO CZX\": REM 0123456789 !@#$%^&*()_+[]{};:'\",./<>?",
                  p, fg, 1);

    // Live input readout
    char info[96];
    const char cp_ch = (st->last_cp >= 32 && st->last_cp < 127)
                           ? static_cast<char>(st->last_cp)
                           : '.';
    std::snprintf(info, sizeof(info), "KEY=%d CP=0x%02X('%c') MODS=0x%X JOY=0x%02X",
                  st->last_key, st->last_cp, cp_ch, st->last_mods, st->joy_buttons);
    st->draw_text(ax + 10, ay + 56, info, p, white, 1);
    st->draw_text(ax + 10, ay + 70, st->typed_line.c_str(), p, hi, 1);

    // Moving square for frame pacing / smoothness verification
    const int span_x = std::max(1, aw - 36);
    const int box_x = ax + 10 + static_cast<int>((st->frame_num * 3u) % static_cast<uint64_t>(span_x));
    const int box_y = ay + ah - 32;
    st->fill_rect(box_x, box_y, 16, 16, p, hi);

    // Bottom edge marker text (tests vertical pan / overflow bounds)
    char bottom_msg[64];
    std::snprintf(bottom_msg, sizeof(bottom_msg), "BOTTOM ROW y=%d  FRAME=%llu",
                  ay + ah - 10, static_cast<unsigned long long>(st->frame_num));
    st->draw_text(ax + 10, ay + ah - 12, bottom_msg, p, fg, 1);

    czx_frame_t frame{};
    frame.data = (p.format == CZX_PIXEL_RGB565)
                     ? static_cast<const void*>(st->buf565.data())
                     : static_cast<const void*>(st->buf32.data());
    frame.pitch_bytes = static_cast<uint32_t>(p.width) *
                        static_cast<uint32_t>(p.format == CZX_PIXEL_RGB565 ? 2u : 4u);
    frame.width = p.width;
    frame.height = p.height;
    frame.crop_x = p.crop_x;
    frame.crop_y = p.crop_y;
    frame.crop_w = p.crop_w;
    frame.crop_h = p.crop_h;
    frame.par_num = p.par_num;
    frame.par_den = p.par_den;
    frame.format = p.format;
    frame.flags = (p.c64_theme && p.crop_w > 0) ? static_cast<uint32_t>(CZX_FRAME_HAS_BORDER) : 0u;

    if (st->host.video_frame) {
        st->host.video_frame(st->host.user_data, &frame);
    }

    constexpr size_t kAudioFrames = 48000 / 60;
    st->audio_buf.resize(kAudioFrames * 2u);
    for (size_t i = 0; i < kAudioFrames; ++i) {
        int16_t s = 0;
        if (st->click_samples_left > 0) {
            const int phase = (500 - st->click_samples_left);
            s = ((phase / 25) & 1) ? 1800 : -1800;
            --st->click_samples_left;
        }
        st->audio_buf[i * 2 + 0] = s;
        st->audio_buf[i * 2 + 1] = s;
    }
    if (st->host.audio_samples) {
        st->host.audio_samples(st->host.user_data, st->audio_buf.data(), kAudioFrames);
    }

    ++st->frame_num;
}

void test_on_key(czx_core_instance_t* inst, const czx_key_event_t* ev) {
    auto* st = reinterpret_cast<TestCoreState*>(inst);
    if (!st || !ev || !ev->pressed) return;

    st->last_key = static_cast<int>(ev->key);
    st->last_cp = ev->codepoint;
    st->last_mods = ev->mods;
    if (!ev->repeat) {
        st->click_samples_left = 400;
    }

    if (ev->key == CZX_KEY_BACKSPACE) {
        if (!st->typed_line.empty()) st->typed_line.pop_back();
    } else if (ev->key == CZX_KEY_ENTER) {
        st->typed_line.clear();
    } else if (ev->codepoint >= 32 && ev->codepoint < 127) {
        if (st->typed_line.size() >= 38) {
            st->typed_line.erase(st->typed_line.begin());
        }
        st->typed_line.push_back(static_cast<char>(ev->codepoint));
    }
}

void test_on_joystick(czx_core_instance_t* inst, uint32_t port, uint32_t buttons) {
    auto* st = reinterpret_cast<TestCoreState*>(inst);
    if (!st || port != 0) return;
    st->joy_buttons = buttons;
}

bool test_set_option(czx_core_instance_t*, const char*, const char*) {
    return true;
}

uint32_t test_get_preset_count(const czx_core_instance_t*) {
    return static_cast<uint32_t>(kPresets.size());
}

const char* test_get_preset_name(const czx_core_instance_t*, uint32_t index) {
    if (index >= kPresets.size()) return "";
    return kPresets[index].name;
}

bool test_select_preset(czx_core_instance_t* inst, uint32_t index) {
    auto* st = reinterpret_cast<TestCoreState*>(inst);
    if (!st || index >= kPresets.size()) return false;
    st->preset_idx = index;
    return true;
}

const czx_core_api_t kTestCoreApi = {
    nullptr,
    test_get_desc,
    test_create,
    test_destroy,
    test_reset,
    test_load_media,
    nullptr,
    test_run_frame,
    test_on_key,
    test_on_joystick,
    test_set_option,
    test_get_preset_count,
    test_get_preset_name,
    test_select_preset,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

const czx_core_api_t* get_test_core_api() noexcept {
    return &kTestCoreApi;
}

}  // namespace czx::core
