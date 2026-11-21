#include "video/scaler.hpp"

#include "video/pixel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace czx::video {

const char* display_mode_name(DisplayMode mode) noexcept {
    switch (mode) {
        case DisplayMode::NativePan:
            return "1:1 Pan";
        case DisplayMode::OverflowFit:
            return "Axis Fit";
        case DisplayMode::AspectFit:
            return "Aspect Fit";
    }
    return "Unknown";
}

const char* display_mode_short(DisplayMode mode) noexcept {
    switch (mode) {
        case DisplayMode::NativePan:
            return "1:1";
        case DisplayMode::OverflowFit:
            return "FIT";
        case DisplayMode::AspectFit:
            return "ASP";
    }
    return "?";
}

const char* aspect_policy_name(AspectPolicy policy) noexcept {
    switch (policy) {
        case AspectPolicy::ParAware:
            return "PAR";
        case AspectPolicy::Crt4x3:
            return "CRT 4:3";
    }
    return "Unknown";
}

const char* margin_fill_name(MarginFill fill) noexcept {
    switch (fill) {
        case MarginFill::Auto:
            return "Auto";
        case MarginFill::Black:
            return "Black";
    }
    return "Unknown";
}

namespace {

void fill_margins(const czx_frame_t& frame, const ScalerConfig& cfg,
                  const ViewportLayout& layout, uint16_t* dst_rgb565) {
    if (layout.dst_x == 0 && layout.dst_y == 0 &&
        layout.dst_w == kLcdWidth && layout.dst_h == kLcdHeight) {
        return;
    }

    const int rx = layout.dst_x + layout.dst_w;
    if (cfg.margin_fill == MarginFill::Auto && (frame.flags & CZX_FRAME_HAS_BORDER) != 0) {
        for (int y = layout.dst_y; y < layout.dst_y + layout.dst_h; ++y) {
            uint16_t* row = dst_rgb565 + y * kLcdWidth;
            if (layout.dst_x > 0) {
                std::fill_n(row, layout.dst_x, row[layout.dst_x]);
            }
            if (rx < kLcdWidth) {
                std::fill_n(row + rx, kLcdWidth - rx, row[rx - 1]);
            }
        }
        const size_t row_bytes = static_cast<size_t>(kLcdWidth) * sizeof(uint16_t);
        const uint16_t* top_ref = dst_rgb565 + layout.dst_y * kLcdWidth;
        for (int y = 0; y < layout.dst_y; ++y) {
            std::memcpy(dst_rgb565 + y * kLcdWidth, top_ref, row_bytes);
        }
        const uint16_t* bot_ref = dst_rgb565 + (layout.dst_y + layout.dst_h - 1) * kLcdWidth;
        for (int y = layout.dst_y + layout.dst_h; y < kLcdHeight; ++y) {
            std::memcpy(dst_rgb565 + y * kLcdWidth, bot_ref, row_bytes);
        }
        return;
    }

    uint16_t bg = 0;
    if (cfg.margin_fill == MarginFill::Auto && frame.data != nullptr &&
        layout.src_w > 0 && layout.src_h > 0 &&
        layout.src_x + layout.src_w <= static_cast<int>(frame.width) &&
        layout.src_y + layout.src_h <= static_cast<int>(frame.height)) {
        const auto* base = static_cast<const uint8_t*>(frame.data);
        const uint32_t x0 = static_cast<uint32_t>(layout.src_x);
        const uint32_t x1 = static_cast<uint32_t>(layout.src_x + layout.src_w - 1);
        const uint8_t* r0 = base + static_cast<size_t>(layout.src_y) * frame.pitch_bytes;
        const uint8_t* r1 = base + static_cast<size_t>(layout.src_y + layout.src_h - 1) * frame.pitch_bytes;
        const uint8_t* rows[4] = {r0, r0, r1, r1};
        const uint32_t cols[4] = {x0, x1, x0, x1};
        uint32_t sum_r = 0;
        uint32_t sum_g = 0;
        uint32_t sum_b = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t r = 0, g = 0, b = 0;
            read_pixel_rgb888(rows[i], cols[i], frame.format, r, g, b);
            sum_r += r;
            sum_g += g;
            sum_b += b;
        }
        bg = pack_rgb565(
            static_cast<uint8_t>((sum_r + 2u) / 4u),
            static_cast<uint8_t>((sum_g + 2u) / 4u),
            static_cast<uint8_t>((sum_b + 2u) / 4u));
    }

    for (int y = 0; y < layout.dst_y; ++y) {
        std::fill_n(dst_rgb565 + y * kLcdWidth, kLcdWidth, bg);
    }
    for (int y = layout.dst_y; y < layout.dst_y + layout.dst_h; ++y) {
        uint16_t* row = dst_rgb565 + y * kLcdWidth;
        if (layout.dst_x > 0) std::fill_n(row, layout.dst_x, bg);
        if (rx < kLcdWidth) std::fill_n(row + rx, kLcdWidth - rx, bg);
    }
    for (int y = layout.dst_y + layout.dst_h; y < kLcdHeight; ++y) {
        std::fill_n(dst_rgb565 + y * kLcdWidth, kLcdWidth, bg);
    }
}

void unpack_row_planar(const uint8_t* src_row, int len, czx_pixel_format_t fmt,
                       uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) noexcept {
    int i = 0;
    switch (fmt) {
        case CZX_PIXEL_RGB565: {
            const auto* src16 = reinterpret_cast<const uint16_t*>(src_row);
#if defined(__aarch64__)
            for (; i + 8 <= len; i += 8) {
                const uint16x8_t v = vld1q_u16(src16 + i);
                const uint8x8_t r5 = vmovn_u16(vshrq_n_u16(v, 11));
                const uint8x8_t g6 = vmovn_u16(vandq_u16(vshrq_n_u16(v, 5), vdupq_n_u16(0x3F)));
                const uint8x8_t b5 = vmovn_u16(vandq_u16(v, vdupq_n_u16(0x1F)));
                vst1_u8(out_r + i, vorr_u8(vshl_n_u8(r5, 3), vshr_n_u8(r5, 2)));
                vst1_u8(out_g + i, vorr_u8(vshl_n_u8(g6, 2), vshr_n_u8(g6, 4)));
                vst1_u8(out_b + i, vorr_u8(vshl_n_u8(b5, 3), vshr_n_u8(b5, 2)));
            }
#endif
            for (; i < len; ++i) {
                unpack_rgb565(src16[i], out_r[i], out_g[i], out_b[i]);
            }
            break;
        }
        case CZX_PIXEL_XRGB8888: {
#if defined(__aarch64__)
            for (; i + 16 <= len; i += 16) {
                const uint8x16x4_t v = vld4q_u8(src_row + static_cast<size_t>(i) * 4u);
                vst1q_u8(out_r + i, v.val[2]);
                vst1q_u8(out_g + i, v.val[1]);
                vst1q_u8(out_b + i, v.val[0]);
            }
#endif
            const auto* src32 = reinterpret_cast<const uint32_t*>(src_row);
            for (; i < len; ++i) {
                const uint32_t p = src32[i];
                out_r[i] = static_cast<uint8_t>((p >> 16) & 0xFFu);
                out_g[i] = static_cast<uint8_t>((p >> 8) & 0xFFu);
                out_b[i] = static_cast<uint8_t>(p & 0xFFu);
            }
            break;
        }
        case CZX_PIXEL_ABGR8888: {
#if defined(__aarch64__)
            for (; i + 16 <= len; i += 16) {
                const uint8x16x4_t v = vld4q_u8(src_row + static_cast<size_t>(i) * 4u);
                vst1q_u8(out_r + i, v.val[0]);
                vst1q_u8(out_g + i, v.val[1]);
                vst1q_u8(out_b + i, v.val[2]);
            }
#endif
            const auto* src32 = reinterpret_cast<const uint32_t*>(src_row);
            for (; i < len; ++i) {
                const uint32_t p = src32[i];
                out_r[i] = static_cast<uint8_t>(p & 0xFFu);
                out_g[i] = static_cast<uint8_t>((p >> 8) & 0xFFu);
                out_b[i] = static_cast<uint8_t>((p >> 16) & 0xFFu);
            }
            break;
        }
    }
}

void downsample_row_2to1_planar(const uint8_t* src_row, int dst_w, czx_pixel_format_t fmt,
                                uint8_t* out_r, uint8_t* out_g, uint8_t* out_b,
                                uint8_t* tmp_r, uint8_t* tmp_g, uint8_t* tmp_b) noexcept {
    if (fmt == CZX_PIXEL_RGB565) {
        const auto* src16 = reinterpret_cast<const uint16_t*>(src_row);
        int dx = 0;
#if defined(__aarch64__)
        for (; dx + 8 <= dst_w; dx += 8) {
            const uint16x8x2_t v = vld2q_u16(src16 + dx * 2);
            const uint8x8_t r0_5 = vmovn_u16(vshrq_n_u16(v.val[0], 11));
            const uint8x8_t r1_5 = vmovn_u16(vshrq_n_u16(v.val[1], 11));
            const uint8x8_t g0_6 = vmovn_u16(vandq_u16(vshrq_n_u16(v.val[0], 5), vdupq_n_u16(0x3F)));
            const uint8x8_t g1_6 = vmovn_u16(vandq_u16(vshrq_n_u16(v.val[1], 5), vdupq_n_u16(0x3F)));
            const uint8x8_t b0_5 = vmovn_u16(vandq_u16(v.val[0], vdupq_n_u16(0x1F)));
            const uint8x8_t b1_5 = vmovn_u16(vandq_u16(v.val[1], vdupq_n_u16(0x1F)));

            const uint8x8_t r0 = vorr_u8(vshl_n_u8(r0_5, 3), vshr_n_u8(r0_5, 2));
            const uint8x8_t r1 = vorr_u8(vshl_n_u8(r1_5, 3), vshr_n_u8(r1_5, 2));
            const uint8x8_t g0 = vorr_u8(vshl_n_u8(g0_6, 2), vshr_n_u8(g0_6, 4));
            const uint8x8_t g1 = vorr_u8(vshl_n_u8(g1_6, 2), vshr_n_u8(g1_6, 4));
            const uint8x8_t b0 = vorr_u8(vshl_n_u8(b0_5, 3), vshr_n_u8(b0_5, 2));
            const uint8x8_t b1 = vorr_u8(vshl_n_u8(b1_5, 3), vshr_n_u8(b1_5, 2));

            vst1_u8(out_r + dx, vrhadd_u8(r0, r1));
            vst1_u8(out_g + dx, vrhadd_u8(g0, g1));
            vst1_u8(out_b + dx, vrhadd_u8(b0, b1));
        }
#endif
        for (; dx < dst_w; ++dx) {
            uint8_t r0 = 0, g0 = 0, b0 = 0, r1 = 0, g1 = 0, b1 = 0;
            unpack_rgb565(src16[dx * 2], r0, g0, b0);
            unpack_rgb565(src16[dx * 2 + 1], r1, g1, b1);
            out_r[dx] = static_cast<uint8_t>((static_cast<unsigned>(r0) + r1 + 1u) >> 1);
            out_g[dx] = static_cast<uint8_t>((static_cast<unsigned>(g0) + g1 + 1u) >> 1);
            out_b[dx] = static_cast<uint8_t>((static_cast<unsigned>(b0) + b1 + 1u) >> 1);
        }
        return;
    }

    unpack_row_planar(src_row, dst_w * 2, fmt, tmp_r, tmp_g, tmp_b);
    int dx = 0;
#if defined(__aarch64__)
    for (; dx + 16 <= dst_w; dx += 16) {
        const uint8x16x2_t vr = vld2q_u8(tmp_r + dx * 2);
        const uint8x16x2_t vg = vld2q_u8(tmp_g + dx * 2);
        const uint8x16x2_t vb = vld2q_u8(tmp_b + dx * 2);
        vst1q_u8(out_r + dx, vrhaddq_u8(vr.val[0], vr.val[1]));
        vst1q_u8(out_g + dx, vrhaddq_u8(vg.val[0], vg.val[1]));
        vst1q_u8(out_b + dx, vrhaddq_u8(vb.val[0], vb.val[1]));
    }
#endif
    for (; dx < dst_w; ++dx) {
        out_r[dx] = static_cast<uint8_t>((static_cast<unsigned>(tmp_r[dx * 2]) + tmp_r[dx * 2 + 1] + 1u) >> 1);
        out_g[dx] = static_cast<uint8_t>((static_cast<unsigned>(tmp_g[dx * 2]) + tmp_g[dx * 2 + 1] + 1u) >> 1);
        out_b[dx] = static_cast<uint8_t>((static_cast<unsigned>(tmp_b[dx * 2]) + tmp_b[dx * 2 + 1] + 1u) >> 1);
    }
}

void accum_row_y(uint16_t* acc_r, uint16_t* acc_g, uint16_t* acc_b,
                 const uint8_t* row_r, const uint8_t* row_g, const uint8_t* row_b,
                 int dst_w, uint8_t wy) noexcept {
    int dx = 0;
#if defined(__aarch64__)
    const uint8x8_t vwy = vdup_n_u8(wy);
    for (; dx + 16 <= dst_w; dx += 16) {
        const uint8x16_t vr = vld1q_u8(row_r + dx);
        const uint8x16_t vg = vld1q_u8(row_g + dx);
        const uint8x16_t vb = vld1q_u8(row_b + dx);

        vst1q_u16(acc_r + dx, vmlal_u8(vld1q_u16(acc_r + dx), vget_low_u8(vr), vwy));
        vst1q_u16(acc_r + dx + 8, vmlal_u8(vld1q_u16(acc_r + dx + 8), vget_high_u8(vr), vwy));
        vst1q_u16(acc_g + dx, vmlal_u8(vld1q_u16(acc_g + dx), vget_low_u8(vg), vwy));
        vst1q_u16(acc_g + dx + 8, vmlal_u8(vld1q_u16(acc_g + dx + 8), vget_high_u8(vg), vwy));
        vst1q_u16(acc_b + dx, vmlal_u8(vld1q_u16(acc_b + dx), vget_low_u8(vb), vwy));
        vst1q_u16(acc_b + dx + 8, vmlal_u8(vld1q_u16(acc_b + dx + 8), vget_high_u8(vb), vwy));
    }
#endif
    const uint16_t w = wy;
    for (; dx < dst_w; ++dx) {
        acc_r[dx] = static_cast<uint16_t>(acc_r[dx] + static_cast<uint16_t>(row_r[dx]) * w);
        acc_g[dx] = static_cast<uint16_t>(acc_g[dx] + static_cast<uint16_t>(row_g[dx]) * w);
        acc_b[dx] = static_cast<uint16_t>(acc_b[dx] + static_cast<uint16_t>(row_b[dx]) * w);
    }
}

void pack_acc_rgb565(const uint16_t* acc_r, const uint16_t* acc_g, const uint16_t* acc_b,
                     int dst_w, uint16_t* dst_row) noexcept {
    int dx = 0;
#if defined(__aarch64__)
    for (; dx + 8 <= dst_w; dx += 8) {
        const uint16x8_t vr = vshlq_n_u16(vshrq_n_u16(vld1q_u16(acc_r + dx), 10), 11);
        const uint16x8_t vg = vshlq_n_u16(vshrq_n_u16(vld1q_u16(acc_g + dx), 9), 5);
        const uint16x8_t vb = vshrq_n_u16(vld1q_u16(acc_b + dx), 10);
        vst1q_u16(dst_row + dx, vorrq_u16(vorrq_u16(vr, vg), vb));
    }
#endif
    for (; dx < dst_w; ++dx) {
        dst_row[dx] = static_cast<uint16_t>(
            ((static_cast<uint32_t>(acc_r[dx]) >> 10) << 11) |
            ((static_cast<uint32_t>(acc_g[dx]) >> 9) << 5) |
            (static_cast<uint32_t>(acc_b[dx]) >> 10));
    }
}

void pack_planar_rgb565(const uint8_t* r, const uint8_t* g, const uint8_t* b,
                        int dst_w, uint16_t* dst_row) noexcept {
    int dx = 0;
#if defined(__aarch64__)
    for (; dx + 8 <= dst_w; dx += 8) {
        const uint16x8_t vr = vshlq_n_u16(vshrq_n_u16(vmovl_u8(vld1_u8(r + dx)), 3), 11);
        const uint16x8_t vg = vshlq_n_u16(vshrq_n_u16(vmovl_u8(vld1_u8(g + dx)), 2), 5);
        const uint16x8_t vb = vshrq_n_u16(vmovl_u8(vld1_u8(b + dx)), 3);
        vst1q_u16(dst_row + dx, vorrq_u16(vorrq_u16(vr, vg), vb));
    }
#endif
    for (; dx < dst_w; ++dx) {
        dst_row[dx] = pack_rgb565(r[dx], g[dx], b[dx]);
    }
}

}  // namespace

void Scaler::AxisTable::ensure(int src, int dst) {
    if (src_len == src && dst_len == dst) {
        return;
    }
    src_len = src;
    dst_len = dst;
    spans.resize(static_cast<size_t>(dst));
    weights_q7.clear();

    if (src <= 0 || dst <= 0) {
        return;
    }

    std::vector<double> raw_weights;
    for (int o = 0; o < dst; ++o) {
        const double s0 = static_cast<double>(o) * static_cast<double>(src) / static_cast<double>(dst);
        const double s1 = static_cast<double>(o + 1) * static_cast<double>(src) / static_cast<double>(dst);
        const double span_len = s1 - s0;

        int i0 = static_cast<int>(std::floor(s0));
        int i1 = static_cast<int>(std::ceil(s1)) - 1;
        i0 = std::clamp(i0, 0, src - 1);
        i1 = std::clamp(i1, i0, src - 1);

        raw_weights.clear();
        const uint32_t tap_start = static_cast<uint32_t>(weights_q7.size());
        double max_w = -1.0;
        int max_rel = 0;

        for (int i = i0; i <= i1; ++i) {
            const double ov = std::min(s1, static_cast<double>(i + 1)) - std::max(s0, static_cast<double>(i));
            const double w = (ov > 0.0 && span_len > 0.0) ? (ov / span_len) : 0.0;
            if (w > max_w) {
                max_w = w;
                max_rel = static_cast<int>(raw_weights.size());
            }
            raw_weights.push_back(w);
        }

        int q_sum = 0;
        for (size_t k = 0; k < raw_weights.size(); ++k) {
            const int q = static_cast<int>(std::floor(raw_weights[k] * 128.0 + 0.5));
            const int clamped = std::clamp(q, 0, 128);
            q_sum += clamped;
            weights_q7.push_back(static_cast<uint8_t>(clamped));
        }

        // Guarantee exact Q7 unity sum (128) so pure white never dims or overflows.
        if (!raw_weights.empty()) {
            const int diff = 128 - q_sum;
            uint8_t& best = weights_q7[tap_start + static_cast<uint32_t>(max_rel)];
            best = static_cast<uint8_t>(std::clamp(static_cast<int>(best) + diff, 0, 128));
        }

        spans[static_cast<size_t>(o)] = AxisSpan{
            static_cast<uint16_t>(i0),
            tap_start,
            static_cast<uint16_t>(raw_weights.size()),
        };
    }
}

ViewportLayout Scaler::compute_layout(const czx_frame_t& frame, const ScalerConfig& cfg) {
    ViewportLayout out{};
    if (frame.width == 0 || frame.height == 0) {
        return out;
    }

    int active_x = 0;
    int active_y = 0;
    int active_w = frame.width;
    int active_h = frame.height;

    if (!cfg.show_borders && frame.crop_w > 0 && frame.crop_h > 0 &&
        static_cast<int>(frame.crop_x) + static_cast<int>(frame.crop_w) <= static_cast<int>(frame.width) &&
        static_cast<int>(frame.crop_y) + static_cast<int>(frame.crop_h) <= static_cast<int>(frame.height)) {
        active_x = frame.crop_x;
        active_y = frame.crop_y;
        active_w = frame.crop_w;
        active_h = frame.crop_h;
    }

    out.active_w = active_w;
    out.active_h = active_h;

    switch (cfg.mode) {
        case DisplayMode::NativePan: {
            if (active_w <= kLcdWidth) {
                out.src_x = active_x;
                out.src_w = active_w;
                out.dst_w = active_w;
                out.dst_x = (kLcdWidth - active_w) / 2;
                out.max_pan_x = 0;
                out.pan_x = 0;
            } else {
                out.max_pan_x = active_w - kLcdWidth;
                out.pan_x = std::clamp(cfg.pan_x, 0, out.max_pan_x);
                out.src_x = active_x + out.pan_x;
                out.src_w = kLcdWidth;
                out.dst_x = 0;
                out.dst_w = kLcdWidth;
            }

            if (active_h <= kLcdHeight) {
                out.src_y = active_y;
                out.src_h = active_h;
                out.dst_h = active_h;
                out.dst_y = (kLcdHeight - active_h) / 2;
                out.max_pan_y = 0;
                out.pan_y = 0;
            } else {
                out.max_pan_y = active_h - kLcdHeight;
                out.pan_y = std::clamp(cfg.pan_y, 0, out.max_pan_y);
                out.src_y = active_y + out.pan_y;
                out.src_h = kLcdHeight;
                out.dst_y = 0;
                out.dst_h = kLcdHeight;
            }
            break;
        }
        case DisplayMode::OverflowFit: {
            out.src_x = active_x;
            out.src_y = active_y;
            out.src_w = active_w;
            out.src_h = active_h;
            out.dst_w = std::min(active_w, kLcdWidth);
            out.dst_h = std::min(active_h, kLcdHeight);
            out.dst_x = (kLcdWidth - out.dst_w) / 2;
            out.dst_y = (kLcdHeight - out.dst_h) / 2;
            break;
        }
        case DisplayMode::AspectFit: {
            out.src_x = active_x;
            out.src_y = active_y;
            out.src_w = active_w;
            out.src_h = active_h;

            double eff_w = static_cast<double>(active_w);
            double eff_h = static_cast<double>(active_h);
            if (cfg.aspect_policy == AspectPolicy::ParAware) {
                const double par_num = frame.par_num ? static_cast<double>(frame.par_num) : 1.0;
                const double par_den = frame.par_den ? static_cast<double>(frame.par_den) : 1.0;
                eff_w = static_cast<double>(active_w) * par_num / par_den;
            } else {
                eff_w = static_cast<double>(active_h) * (4.0 / 3.0);
            }

            const double scale = std::min({
                1.0,
                static_cast<double>(kLcdWidth) / eff_w,
                static_cast<double>(kLcdHeight) / eff_h,
            });
            out.dst_w = std::clamp(static_cast<int>(std::lround(eff_w * scale)), 1, kLcdWidth);
            out.dst_h = std::clamp(static_cast<int>(std::lround(eff_h * scale)), 1, kLcdHeight);
            out.dst_x = (kLcdWidth - out.dst_w) / 2;
            out.dst_y = (kLcdHeight - out.dst_h) / 2;

            if (cfg.margin_fill == MarginFill::Auto && (frame.flags & CZX_FRAME_HAS_BORDER) != 0 &&
                !cfg.show_borders && out.dst_w < kLcdWidth) {
                const int avail_l = active_x;
                const int avail_r = static_cast<int>(frame.width) - (active_x + active_w);
                const int pad_l = (kLcdWidth - out.dst_w) / 2;
                const int pad_r = kLcdWidth - out.dst_w - pad_l;
                const double px_per_src_x = static_cast<double>(out.dst_w) / static_cast<double>(active_w);
                if ((avail_l > 0 || avail_r > 0) && px_per_src_x > 0.0) {
                    const int ext_dst_l = std::min(pad_l, static_cast<int>(std::floor(avail_l * px_per_src_x + 0.5)));
                    const int ext_dst_r = std::min(pad_r, static_cast<int>(std::floor(avail_r * px_per_src_x + 0.5)));
                    const int ext_src_l = std::min(
                        avail_l, static_cast<int>(std::lround(static_cast<double>(ext_dst_l) / px_per_src_x)));
                    const int ext_src_r = std::min(
                        avail_r, static_cast<int>(std::lround(static_cast<double>(ext_dst_r) / px_per_src_x)));
                    out.src_x = active_x - ext_src_l;
                    out.src_w = active_w + ext_src_l + ext_src_r;
                    out.dst_w = out.dst_w + ext_dst_l + ext_dst_r;
                    out.dst_x = pad_l - ext_dst_l;
                }
            }
            break;
        }
    }

    return out;
}

ViewportLayout Scaler::render(const czx_frame_t& frame, const ScalerConfig& cfg, uint16_t* dst_rgb565) {
    const ViewportLayout layout = compute_layout(frame, cfg);
    if (!frame.data || layout.src_w <= 0 || layout.src_h <= 0 || layout.dst_w <= 0 || layout.dst_h <= 0) {
        std::memset(dst_rgb565, 0, static_cast<size_t>(kLcdWidth * kLcdHeight) * sizeof(uint16_t));
        return layout;
    }

    const auto* base = static_cast<const uint8_t*>(frame.data);
    const uint32_t pitch = frame.pitch_bytes;
    const int bpp = (frame.format == CZX_PIXEL_RGB565) ? 2 : 4;
    const size_t src_x_bytes = static_cast<size_t>(layout.src_x) * static_cast<size_t>(bpp);

    auto resample_row_x = [&](const uint8_t* src_row, uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
        if (layout.src_w == layout.dst_w) {
            unpack_row_planar(src_row, layout.dst_w, frame.format, out_r, out_g, out_b);
            return;
        }
        if (layout.src_w == layout.dst_w * 2) {
            downsample_row_2to1_planar(src_row, layout.dst_w, frame.format,
                                       out_r, out_g, out_b,
                                       src_r_.data(), src_g_.data(), src_b_.data());
            return;
        }
        uint8_t* sr = src_r_.data();
        uint8_t* sg = src_g_.data();
        uint8_t* sb = src_b_.data();
        unpack_row_planar(src_row, layout.src_w, frame.format, sr, sg, sb);
        for (int dx = 0; dx < layout.dst_w; ++dx) {
            const AxisSpan sp = x_table_.spans[static_cast<size_t>(dx)];
            const uint8_t* w = x_table_.weights_q7.data() + sp.tap_offset;
            const int sx = sp.src_start;
            if (sp.count == 1u) {
                out_r[dx] = sr[sx];
                out_g[dx] = sg[sx];
                out_b[dx] = sb[sx];
            } else if (sp.count == 2u) {
                const uint32_t w0 = w[0];
                const uint32_t w1 = w[1];
                out_r[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sr[sx]) * w0 +
                                                  static_cast<uint32_t>(sr[sx + 1]) * w1 + 64u) >> 7);
                out_g[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sg[sx]) * w0 +
                                                  static_cast<uint32_t>(sg[sx + 1]) * w1 + 64u) >> 7);
                out_b[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sb[sx]) * w0 +
                                                  static_cast<uint32_t>(sb[sx + 1]) * w1 + 64u) >> 7);
            } else if (sp.count == 3u) {
                const uint32_t w0 = w[0];
                const uint32_t w1 = w[1];
                const uint32_t w2 = w[2];
                out_r[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sr[sx]) * w0 +
                                                  static_cast<uint32_t>(sr[sx + 1]) * w1 +
                                                  static_cast<uint32_t>(sr[sx + 2]) * w2 + 64u) >> 7);
                out_g[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sg[sx]) * w0 +
                                                  static_cast<uint32_t>(sg[sx + 1]) * w1 +
                                                  static_cast<uint32_t>(sg[sx + 2]) * w2 + 64u) >> 7);
                out_b[dx] = static_cast<uint8_t>((static_cast<uint32_t>(sb[sx]) * w0 +
                                                  static_cast<uint32_t>(sb[sx + 1]) * w1 +
                                                  static_cast<uint32_t>(sb[sx + 2]) * w2 + 64u) >> 7);
            } else {
                uint32_t ar = 64;
                uint32_t ag = 64;
                uint32_t ab = 64;
                for (uint16_t k = 0; k < sp.count; ++k) {
                    const uint32_t wk = w[k];
                    ar += static_cast<uint32_t>(sr[sx + k]) * wk;
                    ag += static_cast<uint32_t>(sg[sx + k]) * wk;
                    ab += static_cast<uint32_t>(sb[sx + k]) * wk;
                }
                out_r[dx] = static_cast<uint8_t>(ar >> 7);
                out_g[dx] = static_cast<uint8_t>(ag >> 7);
                out_b[dx] = static_cast<uint8_t>(ab >> 7);
            }
        }
    };

    if (layout.src_w == layout.dst_w && layout.src_h == layout.dst_h) {
        // Fast path 1: 1:1 unscaled copy with format dispatch hoisted outside the row loop.
        switch (frame.format) {
            case CZX_PIXEL_RGB565: {
                const size_t copy_bytes = static_cast<size_t>(layout.dst_w) * sizeof(uint16_t);
                for (int dy = 0; dy < layout.dst_h; ++dy) {
                    const uint8_t* src_row = base + static_cast<size_t>(layout.src_y + dy) * pitch + src_x_bytes;
                    uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
                    std::memcpy(dst_row, src_row, copy_bytes);
                }
                break;
            }
            case CZX_PIXEL_XRGB8888: {
                for (int dy = 0; dy < layout.dst_h; ++dy) {
                    const auto* s32 = reinterpret_cast<const uint32_t*>(
                        base + static_cast<size_t>(layout.src_y + dy) * pitch + src_x_bytes);
                    uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
                    for (int dx = 0; dx < layout.dst_w; ++dx) {
                        const uint32_t p = s32[dx];
                        dst_row[dx] = pack_rgb565(static_cast<uint8_t>((p >> 16) & 0xFFu),
                                                  static_cast<uint8_t>((p >> 8) & 0xFFu),
                                                  static_cast<uint8_t>(p & 0xFFu));
                    }
                }
                break;
            }
            case CZX_PIXEL_ABGR8888: {
                for (int dy = 0; dy < layout.dst_h; ++dy) {
                    const auto* s32 = reinterpret_cast<const uint32_t*>(
                        base + static_cast<size_t>(layout.src_y + dy) * pitch + src_x_bytes);
                    uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
                    for (int dx = 0; dx < layout.dst_w; ++dx) {
                        const uint32_t p = s32[dx];
                        dst_row[dx] = pack_rgb565(static_cast<uint8_t>(p & 0xFFu),
                                                  static_cast<uint8_t>((p >> 8) & 0xFFu),
                                                  static_cast<uint8_t>((p >> 16) & 0xFFu));
                    }
                }
                break;
            }
        }
    } else if (layout.src_h == layout.dst_h) {
        if (layout.src_w != layout.dst_w && layout.src_w != layout.dst_w * 2) {
            x_table_.ensure(layout.src_w, layout.dst_w);
        }
        const size_t row_buf_len = static_cast<size_t>(layout.src_w);
        if (src_r_.size() < row_buf_len) {
            src_r_.resize(row_buf_len);
            src_g_.resize(row_buf_len);
            src_b_.resize(row_buf_len);
        }
        for (int dy = 0; dy < layout.dst_h; ++dy) {
            const uint8_t* src_row = base + static_cast<size_t>(layout.src_y + dy) * pitch + src_x_bytes;
            resample_row_x(src_row, cur_r_, cur_g_, cur_b_);
            uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
            pack_planar_rgb565(cur_r_, cur_g_, cur_b_, layout.dst_w, dst_row);
        }
    } else {
        if (layout.src_w != layout.dst_w && layout.src_w != layout.dst_w * 2) {
            x_table_.ensure(layout.src_w, layout.dst_w);
        }
        y_table_.ensure(layout.src_h, layout.dst_h);

        const size_t row_buf_len = static_cast<size_t>(layout.src_w);
        if (src_r_.size() < row_buf_len) {
            src_r_.resize(row_buf_len);
            src_g_.resize(row_buf_len);
            src_b_.resize(row_buf_len);
        }

        int cached_sy = -1;
        for (int dy = 0; dy < layout.dst_h; ++dy) {
            const AxisSpan ysp = y_table_.spans[static_cast<size_t>(dy)];
            const uint8_t* wy_ptr = y_table_.weights_q7.data() + ysp.tap_offset;
            std::fill_n(acc_r_, layout.dst_w, uint16_t{64});
            std::fill_n(acc_g_, layout.dst_w, uint16_t{64});
            std::fill_n(acc_b_, layout.dst_w, uint16_t{64});

            for (uint16_t k = 0; k < ysp.count; ++k) {
                const int sy = static_cast<int>(ysp.src_start) + k;
                const uint8_t wy = wy_ptr[k];
                const uint8_t* rr = nullptr;
                const uint8_t* rg = nullptr;
                const uint8_t* rb = nullptr;
                if (sy == cached_sy) {
                    rr = prev_r_;
                    rg = prev_g_;
                    rb = prev_b_;
                } else {
                    const uint8_t* src_row = base + static_cast<size_t>(layout.src_y + sy) * pitch + src_x_bytes;
                    if (k + 1u == ysp.count) {
                        resample_row_x(src_row, prev_r_, prev_g_, prev_b_);
                        cached_sy = sy;
                        rr = prev_r_;
                        rg = prev_g_;
                        rb = prev_b_;
                    } else {
                        resample_row_x(src_row, cur_r_, cur_g_, cur_b_);
                        rr = cur_r_;
                        rg = cur_g_;
                        rb = cur_b_;
                    }
                }
                accum_row_y(acc_r_, acc_g_, acc_b_, rr, rg, rb, layout.dst_w, wy);
            }

            uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
            pack_acc_rgb565(acc_r_, acc_g_, acc_b_, layout.dst_w, dst_row);
        }
    }

    fill_margins(frame, cfg, layout, dst_rgb565);
    return layout;
}

}  // namespace czx::video
