#include "video/hud.hpp"
#include "video/pixel.hpp"
#include "video/scaler.hpp"

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

    // Hardware-bordered frame: horizontal border expansion & per-scanline edge extension
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

        // Distinct raster bars per half in Show Full mode extend to LCD left/right edges
        for (int y = 0; y < 272; ++y) {
            const uint32_t lc = (y < 136) ? abgr(255, 0, 0) : abgr(0, 0, 255);
            const uint32_t rc = (y < 136) ? abgr(0, 255, 0) : abgr(255, 255, 0);
            for (int x = 0; x < 32; ++x) src[static_cast<size_t>(y) * 384u + static_cast<size_t>(x)] = lc;
            for (int x = 352; x < 384; ++x) src[static_cast<size_t>(y) * 384u + static_cast<size_t>(x)] = rc;
        }
        ScalerConfig cfg_full{DisplayMode::AspectFit, AspectPolicy::ParAware, true, 0, 0, MarginFill::Auto};
        const auto l_full = scaler.render(f, cfg_full, out.data());
        CHECK(l_full.dst_x == 40 && l_full.dst_w == 240);
        CHECK(out[10 * kLcdWidth + 0] == pack_rgb565(255, 0, 0));
        CHECK(out[10 * kLcdWidth + 319] == pack_rgb565(0, 255, 0));
        CHECK(out[160 * kLcdWidth + 0] == pack_rgb565(0, 0, 255));
        CHECK(out[160 * kLcdWidth + 319] == pack_rgb565(255, 255, 0));
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

}  // namespace

int main() {
    test_layouts();
    test_pixel_accuracy_and_resampling();
    test_hud_render();
    std::printf("test_scaler: all assertions passed\n");
    return 0;
}
