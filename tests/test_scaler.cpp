#include "video/hud.hpp"
#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace czx::video;

#define CHECK(expr)                                                                 \
    do {                                                                            \
        if (!(expr)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {

void test_layouts() {
    // 1. Apple II 40-col: 280x192, PAR 1:1
    czx_frame_t a2_40{};
    a2_40.width = 280;
    a2_40.height = 192;
    a2_40.par_num = 1;
    a2_40.par_den = 1;

    {
        ScalerConfig cfg{DisplayMode::NativePan, AspectPolicy::ParAware, false, 100, 100};
        const auto l = Scaler::compute_layout(a2_40, cfg);
        CHECK(l.dst_x == 20 && l.dst_w == 280);
        CHECK(l.dst_y == 0 && l.dst_h == 170);
        CHECK(l.max_pan_x == 0 && l.pan_x == 0);
        CHECK(l.max_pan_y == 22 && l.pan_y == 22);
    }
    {
        ScalerConfig cfg{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0};
        const auto l = Scaler::compute_layout(a2_40, cfg);
        CHECK(l.src_w == 280 && l.dst_w == 280 && l.dst_x == 20);
        CHECK(l.src_h == 192 && l.dst_h == 170 && l.dst_y == 0);
    }
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0};
        const auto l = Scaler::compute_layout(a2_40, cfg);
        CHECK(l.dst_w == 248 && l.dst_h == 170 && l.dst_x == 36);
    }
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::Crt4x3, false, 0, 0};
        const auto l = Scaler::compute_layout(a2_40, cfg);
        CHECK(l.dst_w == 227 && l.dst_h == 170);
    }

    // 2. Apple II 80-col: 560x192, PAR 1:2
    czx_frame_t a2_80{};
    a2_80.width = 560;
    a2_80.height = 192;
    a2_80.par_num = 1;
    a2_80.par_den = 2;

    {
        ScalerConfig cfg{DisplayMode::NativePan, AspectPolicy::ParAware, false, 999, 999};
        const auto l = Scaler::compute_layout(a2_80, cfg);
        CHECK(l.max_pan_x == 240 && l.pan_x == 240);
        CHECK(l.max_pan_y == 22 && l.pan_y == 22);
        CHECK(l.dst_w == 320 && l.dst_h == 170);
    }
    {
        ScalerConfig cfg{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0};
        const auto l = Scaler::compute_layout(a2_80, cfg);
        CHECK(l.dst_w == 320 && l.dst_h == 170);
    }
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0};
        const auto l = Scaler::compute_layout(a2_80, cfg);
        CHECK(l.dst_w == 248 && l.dst_h == 170 && l.dst_x == 36);
    }

    // 3. C64 PAL Border: 384x272 with crop 32,36,320,200 + CZX_FRAME_HAS_BORDER
    czx_frame_t c64{};
    c64.width = 384;
    c64.height = 272;
    c64.crop_x = 32;
    c64.crop_y = 36;
    c64.crop_w = 320;
    c64.crop_h = 200;
    c64.flags = CZX_FRAME_HAS_BORDER;

    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Auto};
        const auto l = Scaler::compute_layout(c64, cfg);
        CHECK(l.src_x == 4 && l.src_y == 36 && l.src_w == 376 && l.src_h == 200);
        CHECK(l.dst_w == 320 && l.dst_h == 170 && l.dst_x == 0);
    }
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black};
        const auto l = Scaler::compute_layout(c64, cfg);
        CHECK(l.src_x == 32 && l.src_y == 36 && l.src_w == 320 && l.src_h == 200);
        CHECK(l.dst_w == 272 && l.dst_h == 170 && l.dst_x == 24);
    }
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, true, 0, 0, MarginFill::Auto};
        const auto l = Scaler::compute_layout(c64, cfg);
        CHECK(l.src_x == 0 && l.src_y == 0 && l.src_w == 384 && l.src_h == 272);
        CHECK(l.dst_w == 240 && l.dst_h == 170 && l.dst_x == 40);
    }

    // 4. Small frame must never upscale in AspectFit
    czx_frame_t small{};
    small.width = 160;
    small.height = 120;
    {
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0};
        const auto l = Scaler::compute_layout(small, cfg);
        CHECK(l.dst_w == 160 && l.dst_h == 120);
    }
}

void test_pixel_accuracy_and_resampling() {
    Scaler scaler;
    std::vector<uint16_t> out(kLcdWidth * kLcdHeight, 0);

    // Pure white 560x192 XRGB8888 downscaled to 248x170 with MarginFill::Black keeps black pillarboxes
    {
        std::vector<uint32_t> src(560u * 192u, 0x00FFFFFFu);
        czx_frame_t f{src.data(), 560u * 4u, 560, 192, 0, 0, 0, 0, 1, 2, CZX_PIXEL_XRGB8888, 0};
        ScalerConfig cfg{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black};
        const auto l = scaler.render(f, cfg, out.data());
        CHECK(l.dst_x == 36 && l.dst_w == 248);
        CHECK(out[0] == 0x0000);  // left pillarbox black
        CHECK(out[36] == 0xFFFF); // first active pixel pure white
        CHECK(out[36 + 247] == 0xFFFF);
        CHECK(out[36 + 248] == 0x0000); // right pillarbox black
    }

    // Channel ordering check: ABGR8888 pure red (0xFF0000FF) vs XRGB8888 pure red (0x00FF0000)
    {
        std::vector<uint32_t> src_abgr(320u * 200u, 0xFF0000FFu);
        czx_frame_t f{src_abgr.data(), 320u * 4u, 320, 200, 0, 0, 0, 0, 1, 1, CZX_PIXEL_ABGR8888, 0};
        ScalerConfig cfg{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0};
        scaler.render(f, cfg, out.data());
        CHECK(out[10 * kLcdWidth + 10] == pack_rgb565(255, 0, 0));
    }

    // NativePan exact pixel lookup at pan=(10, 15)
    {
        std::vector<uint16_t> src565(640u * 400u, 0x0000);
        src565[15u * 640u + 10u] = pack_rgb565(0, 255, 0);
        czx_frame_t f{src565.data(), 640u * 2u, 640, 400, 0, 0, 0, 0, 1, 1, CZX_PIXEL_RGB565, 0};
        ScalerConfig cfg{DisplayMode::NativePan, AspectPolicy::ParAware, false, 10, 15, MarginFill::Auto};
        scaler.render(f, cfg, out.data());
        CHECK(out[0] == pack_rgb565(0, 255, 0));
    }

    // 4-corner average margin fill for non-bordered frames
    {
        auto rgb = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t {
            return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
        };
        std::vector<uint32_t> src(280u * 192u, 0);
        src[0] = rgb(40, 0, 0);
        src[279] = rgb(0, 80, 0);
        src[191u * 280u + 0u] = rgb(0, 0, 120);
        src[191u * 280u + 279u] = rgb(200, 120, 40);
        const uint16_t expected_bg = pack_rgb565(60, 50, 40);

        czx_frame_t f{src.data(), 280u * 4u, 280, 192, 0, 0, 0, 0, 1, 1, CZX_PIXEL_XRGB8888, 0};
        ScalerConfig cfg_auto{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Auto};
        const auto la = scaler.render(f, cfg_auto, out.data());
        CHECK(la.dst_x == 36 && la.dst_w == 248);
        CHECK(out[0] == expected_bg);
        CHECK(out[35] == expected_bg);
        CHECK(out[36 + 248] == expected_bg);
        CHECK(out[kLcdWidth - 1] == expected_bg);
        CHECK(out[85 * kLcdWidth + 0] == expected_bg);

        ScalerConfig cfg_black = cfg_auto;
        cfg_black.margin_fill = MarginFill::Black;
        scaler.render(f, cfg_black, out.data());
        CHECK(out[0] == 0x0000 && out[kLcdWidth - 1] == 0x0000);
    }

    // Hardware-bordered frame: horizontal border expansion & 4-corner average margin fill
    {
        auto abgr = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t {
            return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
        };
        const uint32_t border_c = abgr(108, 94, 181);
        const uint32_t inner_c = abgr(53, 40, 121);
        std::vector<uint32_t> src(384u * 272u, border_c);
        for (int y = 36; y < 236; ++y) {
            for (int x = 32; x < 352; ++x) {
                src[static_cast<size_t>(y) * 384u + static_cast<size_t>(x)] = inner_c;
            }
        }
        czx_frame_t f{src.data(), 384u * 4u, 384, 272, 32, 36, 320, 200, 1, 1, CZX_PIXEL_ABGR8888, CZX_FRAME_HAS_BORDER};

        ScalerConfig cfg_auto{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Auto};
        const auto l_crop = scaler.render(f, cfg_auto, out.data());
        CHECK(l_crop.dst_x == 0 && l_crop.dst_w == 320 && l_crop.src_x == 4 && l_crop.src_w == 376);
        CHECK(out[85 * kLcdWidth + 0] == pack_rgb565(108, 94, 181));
        CHECK(out[85 * kLcdWidth + 319] == pack_rgb565(108, 94, 181));
        CHECK(out[85 * kLcdWidth + 160] == pack_rgb565(53, 40, 121));

        ScalerConfig cfg_black = cfg_auto;
        cfg_black.margin_fill = MarginFill::Black;
        const auto l_black = scaler.render(f, cfg_black, out.data());
        CHECK(l_black.dst_x == 24 && l_black.dst_w == 272 && l_black.src_x == 32 && l_black.src_w == 320);
        CHECK(out[85 * kLcdWidth + 0] == 0x0000 && out[85 * kLcdWidth + 319] == 0x0000);

        // Distinct raster bars per half in Show Full mode fill margins with 4-corner average (no edge stretching)
        for (int y = 0; y < 272; ++y) {
            const uint32_t lc = (y < 136) ? abgr(255, 0, 0) : abgr(0, 0, 255);
            const uint32_t rc = (y < 136) ? abgr(0, 255, 0) : abgr(255, 255, 0);
            for (int x = 0; x < 32; ++x) src[static_cast<size_t>(y) * 384u + static_cast<size_t>(x)] = lc;
            for (int x = 352; x < 384; ++x) src[static_cast<size_t>(y) * 384u + static_cast<size_t>(x)] = rc;
        }
        ScalerConfig cfg_full{DisplayMode::AspectFit, AspectPolicy::ParAware, true, 0, 0, MarginFill::Auto};
        const auto l_full = scaler.render(f, cfg_full, out.data());
        const uint16_t expected_avg = pack_rgb565(128, 128, 64);
        CHECK(l_full.dst_x == 40 && l_full.dst_w == 240);
        CHECK(out[10 * kLcdWidth + 0] == expected_avg);
        CHECK(out[10 * kLcdWidth + 319] == expected_avg);
        CHECK(out[160 * kLcdWidth + 0] == expected_avg);
        CHECK(out[160 * kLcdWidth + 319] == expected_avg);
    }

    // MSX active crop (272x240, crop 8,14,256,212, CZX_FRAME_HAS_BORDER):
    // margins must use the outer hardware border color, NOT active-area edges or corners.
    {
        const uint16_t border_c = pack_rgb565(0, 128, 192);
        const uint16_t inner_c = pack_rgb565(0, 0, 64);
        const uint16_t edge_c = pack_rgb565(255, 0, 0);
        const uint16_t corner_c = pack_rgb565(255, 255, 0);
        std::vector<uint16_t> src(272u * 240u, border_c);
        for (int y = 14; y < 226; ++y) {
            for (int x = 8; x < 264; ++x) {
                src[static_cast<size_t>(y) * 272u + static_cast<size_t>(x)] = inner_c;
            }
        }
        for (int y = 30; y <= 160; ++y) {
            src[static_cast<size_t>(y) * 272u + 8u] = edge_c;
            src[static_cast<size_t>(y) * 272u + 263u] = edge_c;
        }
        src[14u * 272u + 8u] = corner_c;
        src[14u * 272u + 263u] = corner_c;
        src[225u * 272u + 8u] = corner_c;
        src[225u * 272u + 263u] = corner_c;

        czx_frame_t f{src.data(), 272u * 2u, 272, 240, 8, 14, 256, 212, 1, 1, CZX_PIXEL_RGB565, CZX_FRAME_HAS_BORDER};

        for (DisplayMode mode : {DisplayMode::NativePan, DisplayMode::OverflowFit}) {
            ScalerConfig cfg{mode, AspectPolicy::ParAware, false, 0, 0, MarginFill::Auto};
            const auto l = scaler.render(f, cfg, out.data());
            CHECK(l.dst_x == 32 && l.dst_w == 256);
            CHECK(out[80 * kLcdWidth + 32] == edge_c);
            CHECK(out[80 * kLcdWidth + 287] == edge_c);
            CHECK(out[0 * kLcdWidth + 0] == border_c);
            CHECK(out[0 * kLcdWidth + 31] == border_c);
            CHECK(out[0 * kLcdWidth + 288] == border_c);
            CHECK(out[0 * kLcdWidth + 319] == border_c);
            CHECK(out[80 * kLcdWidth + 0] == border_c);
            CHECK(out[80 * kLcdWidth + 31] == border_c);
            CHECK(out[80 * kLcdWidth + 288] == border_c);
            CHECK(out[80 * kLcdWidth + 319] == border_c);
            CHECK(out[169 * kLcdWidth + 0] == border_c);
            CHECK(out[169 * kLcdWidth + 319] == border_c);
        }

        ScalerConfig cfg_black{DisplayMode::NativePan, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black};
        scaler.render(f, cfg_black, out.data());
        CHECK(out[80 * kLcdWidth + 0] == 0x0000);
        CHECK(out[80 * kLcdWidth + 319] == 0x0000);
    }
}

void test_hud_render() {
    std::vector<uint16_t> fb(kLcdWidth * kLcdHeight, 0);
    ViewportLayout layout{};
    layout.active_w = 560;
    layout.active_h = 192;
    layout.max_pan_x = 240;
    layout.max_pan_y = 22;

    HudState hud{};
    hud.alt_held = true;
    hud.display_mode = DisplayMode::NativePan;
    render_hud(fb.data(), hud, layout);

    // Bottom HUD bar (y=145..169) should have drawn pixels over slot 4 (x=50)
    bool has_hud_pixels = false;
    for (int x = 30; x < 70; ++x) {
        if (fb[155 * kLcdWidth + x] != 0) {
            has_hud_pixels = true;
            break;
        }
    }
    CHECK(has_hud_pixels);
}

void verify_against_float_oracle(const czx_frame_t& frame, const ScalerConfig& cfg) {
    Scaler scaler;
    std::vector<uint16_t> out(kLcdWidth * kLcdHeight, 0);
    const auto l = scaler.render(frame, cfg, out.data());

    const auto* base = static_cast<const uint8_t*>(frame.data);
    for (int dy = 0; dy < l.dst_h; ++dy) {
        const double sy0 = static_cast<double>(dy) * static_cast<double>(l.src_h) / static_cast<double>(l.dst_h);
        const double sy1 = static_cast<double>(dy + 1) * static_cast<double>(l.src_h) / static_cast<double>(l.dst_h);
        const int y0 = std::clamp(static_cast<int>(std::floor(sy0)), 0, l.src_h - 1);
        const int y1 = std::clamp(static_cast<int>(std::ceil(sy1)) - 1, y0, l.src_h - 1);
        for (int dx = 0; dx < l.dst_w; ++dx) {
            const double sx0 = static_cast<double>(dx) * static_cast<double>(l.src_w) / static_cast<double>(l.dst_w);
            const double sx1 = static_cast<double>(dx + 1) * static_cast<double>(l.src_w) / static_cast<double>(l.dst_w);
            const int x0 = std::clamp(static_cast<int>(std::floor(sx0)), 0, l.src_w - 1);
            const int x1 = std::clamp(static_cast<int>(std::ceil(sx1)) - 1, x0, l.src_w - 1);

            double sum_r = 0.0;
            double sum_g = 0.0;
            double sum_b = 0.0;
            double sum_w = 0.0;
            for (int sy = y0; sy <= y1; ++sy) {
                const double wy = std::min(sy1, static_cast<double>(sy + 1)) - std::max(sy0, static_cast<double>(sy));
                const uint8_t* srow = base + static_cast<size_t>(l.src_y + sy) * frame.pitch_bytes;
                for (int sx = x0; sx <= x1; ++sx) {
                    const double wx =
                        std::min(sx1, static_cast<double>(sx + 1)) - std::max(sx0, static_cast<double>(sx));
                    const double w = wx * wy;
                    uint8_t r = 0, g = 0, b = 0;
                    read_pixel_rgb888(srow, static_cast<uint32_t>(l.src_x + sx), frame.format, r, g, b);
                    sum_r += w * static_cast<double>(r);
                    sum_g += w * static_cast<double>(g);
                    sum_b += w * static_cast<double>(b);
                    sum_w += w;
                }
            }
            const int exp_r5 = static_cast<int>(std::clamp(std::lround(sum_r / sum_w), 0L, 255L)) >> 3;
            const int exp_g6 = static_cast<int>(std::clamp(std::lround(sum_g / sum_w), 0L, 255L)) >> 2;
            const int exp_b5 = static_cast<int>(std::clamp(std::lround(sum_b / sum_w), 0L, 255L)) >> 3;

            const uint16_t got = out[(l.dst_y + dy) * kLcdWidth + (l.dst_x + dx)];
            const int got_r5 = (got >> 11) & 0x1F;
            const int got_g6 = (got >> 5) & 0x3F;
            const int got_b5 = got & 0x1F;
            CHECK(std::abs(got_r5 - exp_r5) <= 1);
            CHECK(std::abs(got_g6 - exp_g6) <= 1);
            CHECK(std::abs(got_b5 - exp_b5) <= 1);
        }
    }
}

void test_float_oracle_all_formats() {
    auto fill_rgb565 = [](int w, int h) {
        std::vector<uint16_t> buf(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t r = static_cast<uint8_t>((x * 5 + y * 3) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((x * 2 + y * 7) & 0xFF);
                const uint8_t b = static_cast<uint8_t>((x * 9 - y * 2) & 0xFF);
                buf[static_cast<size_t>(y) * w + x] = pack_rgb565(r, g, b);
            }
        }
        return buf;
    };
    auto fill_xrgb = [](int w, int h) {
        std::vector<uint32_t> buf(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t r = static_cast<uint32_t>((x * 7 + y * 3) & 0xFF);
                const uint32_t g = static_cast<uint32_t>((x * 4 + y * 11) & 0xFF);
                const uint32_t b = static_cast<uint32_t>((x * 13 + y * 5) & 0xFF);
                buf[static_cast<size_t>(y) * w + x] = (r << 16) | (g << 8) | b;
            }
        }
        return buf;
    };
    auto fill_abgr = [](int w, int h) {
        std::vector<uint32_t> buf(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t r = static_cast<uint32_t>((x * 3 + y * 9) & 0xFF);
                const uint32_t g = static_cast<uint32_t>((x * 8 + y * 6) & 0xFF);
                const uint32_t b = static_cast<uint32_t>((x * 11 + y * 2) & 0xFF);
                buf[static_cast<size_t>(y) * w + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
            }
        }
        return buf;
    };

    // 640x400 RGB565 -> 320x170 (OverflowFit 2:1 X) and 272x170 (AspectFit general)
    {
        auto buf = fill_rgb565(640, 400);
        czx_frame_t f{buf.data(), 640u * 2u, 640, 400, 0, 0, 0, 0, 1, 1, CZX_PIXEL_RGB565, 0};
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});
    }
    // 560x192 XRGB8888 -> 320x170 and 248x170
    {
        auto buf = fill_xrgb(560, 192);
        czx_frame_t f{buf.data(), 560u * 4u, 560, 192, 0, 0, 0, 0, 1, 2, CZX_PIXEL_XRGB8888, 0};
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::AspectFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});
    }
    // 384x272 ABGR8888 (crop 32,36,320,200) -> 320x170 (1:1 X + Y) and 240x170 (full)
    {
        auto buf = fill_abgr(384, 272);
        czx_frame_t f{buf.data(), 384u * 4u, 384, 272, 32, 36, 320, 200, 1, 1, CZX_PIXEL_ABGR8888, CZX_FRAME_HAS_BORDER};
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Auto});
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::AspectFit, AspectPolicy::ParAware, true, 0, 0, MarginFill::Black});
    }
    // 280x192 XRGB8888 -> 227x170 (CRT 4:3, non-multiple-of-16 tail)
    {
        auto buf = fill_xrgb(280, 192);
        czx_frame_t f{buf.data(), 280u * 4u, 280, 192, 0, 0, 0, 0, 1, 1, CZX_PIXEL_XRGB8888, 0};
        verify_against_float_oracle(f, ScalerConfig{DisplayMode::AspectFit, AspectPolicy::Crt4x3, false, 0, 0, MarginFill::Black});
    }
    // X-only resample: 640x160 RGB565 -> 320x160 (2:1) and 500x160 -> 320x160 (general)
    {
        auto buf21 = fill_rgb565(640, 160);
        czx_frame_t f21{buf21.data(), 640u * 2u, 640, 160, 0, 0, 0, 0, 1, 1, CZX_PIXEL_RGB565, 0};
        verify_against_float_oracle(f21, ScalerConfig{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});

        auto buf_g = fill_rgb565(500, 160);
        czx_frame_t fg{buf_g.data(), 500u * 2u, 500, 160, 0, 0, 0, 0, 1, 1, CZX_PIXEL_RGB565, 0};
        verify_against_float_oracle(fg, ScalerConfig{DisplayMode::OverflowFit, AspectPolicy::ParAware, false, 0, 0, MarginFill::Black});
    }
}

}  // namespace

int main() {
    test_layouts();
    test_pixel_accuracy_and_resampling();
    test_hud_render();
    test_float_oracle_all_formats();
    std::printf("test_scaler: all assertions passed\n");
    return 0;
}
