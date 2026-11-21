#include "video/scaler.hpp"

#include "video/pixel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

}  // namespace

void Scaler::AxisTable::ensure(int src, int dst) {
    if (src_len == src && dst_len == dst) {
        return;
    }
    src_len = src;
    dst_len = dst;
    spans.resize(static_cast<size_t>(dst));
    taps.clear();

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
        const uint32_t tap_start = static_cast<uint32_t>(taps.size());
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
            const int q = static_cast<int>(std::floor(raw_weights[k] * 4096.0 + 0.5));
            q_sum += q;
            taps.push_back(Tap{
                static_cast<uint16_t>(i0 + static_cast<int>(k)),
                static_cast<uint16_t>(std::max(0, q)),
            });
        }

        // Guarantee exact Q12 unity sum (4096) so pure white never dims or overflows.
        if (!raw_weights.empty()) {
            const int diff = 4096 - q_sum;
            Tap& best = taps[tap_start + static_cast<uint32_t>(max_rel)];
            const int adjusted = std::clamp(static_cast<int>(best.weight_q12) + diff, 0, 4096);
            best.weight_q12 = static_cast<uint16_t>(adjusted);
        }

        spans[static_cast<size_t>(o)] = AxisSpan{
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

    if (layout.src_w == layout.dst_w && layout.src_h == layout.dst_h) {
        // Fast path 1: 1:1 unscaled copy (used by NativePan and non-overflowing axes).
        for (int dy = 0; dy < layout.dst_h; ++dy) {
            const uint8_t* src_row = base + static_cast<size_t>(layout.src_y + dy) * pitch + src_x_bytes;
            uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
            if (frame.format == CZX_PIXEL_RGB565) {
                std::memcpy(dst_row, src_row, static_cast<size_t>(layout.dst_w) * sizeof(uint16_t));
            } else {
                for (int dx = 0; dx < layout.dst_w; ++dx) {
                    dst_row[dx] = read_pixel_rgb565(src_row, static_cast<uint32_t>(dx), frame.format);
                }
            }
        }
    } else if (layout.src_w == layout.dst_w) {
        // Fast path 2: X is 1:1, resample Y only (typical OverflowFit for Apple II 40-col & C64).
        y_table_.ensure(layout.src_h, layout.dst_h);
        for (int dy = 0; dy < layout.dst_h; ++dy) {
            const AxisSpan span = y_table_.spans[static_cast<size_t>(dy)];
            uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
            for (int dx = 0; dx < layout.dst_w; ++dx) {
                uint32_t acc_r = 2048;
                uint32_t acc_g = 2048;
                uint32_t acc_b = 2048;
                for (uint16_t k = 0; k < span.count; ++k) {
                    const Tap tap = y_table_.taps[span.offset + k];
                    const uint8_t* src_row =
                        base + static_cast<size_t>(layout.src_y + tap.src_idx) * pitch + src_x_bytes;
                    uint8_t r = 0, g = 0, b = 0;
                    read_pixel_rgb888(src_row, static_cast<uint32_t>(dx), frame.format, r, g, b);
                    acc_r += static_cast<uint32_t>(r) * tap.weight_q12;
                    acc_g += static_cast<uint32_t>(g) * tap.weight_q12;
                    acc_b += static_cast<uint32_t>(b) * tap.weight_q12;
                }
                dst_row[dx] = pack_rgb565(
                    static_cast<uint8_t>(acc_r >> 12),
                    static_cast<uint8_t>(acc_g >> 12),
                    static_cast<uint8_t>(acc_b >> 12));
            }
        }
    } else {
        // General separable 2-pass area resampling: X (src_w -> dst_w), then Y (src_h -> dst_h).
        x_table_.ensure(layout.src_w, layout.dst_w);
        const size_t tmp_stride = static_cast<size_t>(layout.dst_w) * 3u;
        tmp_rgb_.resize(static_cast<size_t>(layout.src_h) * tmp_stride);

        for (int sy = 0; sy < layout.src_h; ++sy) {
            const uint8_t* src_row = base + static_cast<size_t>(layout.src_y + sy) * pitch + src_x_bytes;
            uint8_t* tmp_row = tmp_rgb_.data() + static_cast<size_t>(sy) * tmp_stride;
            for (int dx = 0; dx < layout.dst_w; ++dx) {
                const AxisSpan span = x_table_.spans[static_cast<size_t>(dx)];
                uint32_t acc_r = 2048;
                uint32_t acc_g = 2048;
                uint32_t acc_b = 2048;
                for (uint16_t k = 0; k < span.count; ++k) {
                    const Tap tap = x_table_.taps[span.offset + k];
                    uint8_t r = 0, g = 0, b = 0;
                    read_pixel_rgb888(src_row, tap.src_idx, frame.format, r, g, b);
                    acc_r += static_cast<uint32_t>(r) * tap.weight_q12;
                    acc_g += static_cast<uint32_t>(g) * tap.weight_q12;
                    acc_b += static_cast<uint32_t>(b) * tap.weight_q12;
                }
                tmp_row[dx * 3 + 0] = static_cast<uint8_t>(acc_r >> 12);
                tmp_row[dx * 3 + 1] = static_cast<uint8_t>(acc_g >> 12);
                tmp_row[dx * 3 + 2] = static_cast<uint8_t>(acc_b >> 12);
            }
        }

        if (layout.src_h == layout.dst_h) {
            for (int dy = 0; dy < layout.dst_h; ++dy) {
                const uint8_t* tmp_row = tmp_rgb_.data() + static_cast<size_t>(dy) * tmp_stride;
                uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
                for (int dx = 0; dx < layout.dst_w; ++dx) {
                    dst_row[dx] = pack_rgb565(tmp_row[dx * 3 + 0], tmp_row[dx * 3 + 1], tmp_row[dx * 3 + 2]);
                }
            }
        } else {
            y_table_.ensure(layout.src_h, layout.dst_h);
            for (int dy = 0; dy < layout.dst_h; ++dy) {
                const AxisSpan span = y_table_.spans[static_cast<size_t>(dy)];
                uint16_t* dst_row = dst_rgb565 + (layout.dst_y + dy) * kLcdWidth + layout.dst_x;
                for (int dx = 0; dx < layout.dst_w; ++dx) {
                    uint32_t acc_r = 2048;
                    uint32_t acc_g = 2048;
                    uint32_t acc_b = 2048;
                    for (uint16_t k = 0; k < span.count; ++k) {
                        const Tap tap = y_table_.taps[span.offset + k];
                        const uint8_t* tmp_row = tmp_rgb_.data() + static_cast<size_t>(tap.src_idx) * tmp_stride;
                        acc_r += static_cast<uint32_t>(tmp_row[dx * 3 + 0]) * tap.weight_q12;
                        acc_g += static_cast<uint32_t>(tmp_row[dx * 3 + 1]) * tap.weight_q12;
                        acc_b += static_cast<uint32_t>(tmp_row[dx * 3 + 2]) * tap.weight_q12;
                    }
                    dst_row[dx] = pack_rgb565(
                        static_cast<uint8_t>(acc_r >> 12),
                        static_cast<uint8_t>(acc_g >> 12),
                        static_cast<uint8_t>(acc_b >> 12));
                }
            }
        }
    }

    fill_margins(frame, cfg, layout, dst_rgb565);
    return layout;
}

}  // namespace czx::video
