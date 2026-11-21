#pragma once

#include "czx/core.h"

#include <cstdint>
#include <vector>

namespace czx::video {

inline constexpr int kLcdWidth = 320;
inline constexpr int kLcdHeight = 170;

enum class DisplayMode {
    NativePan = 0,    // 1:1 unscaled pixels with 4-way viewport panning
    OverflowFit = 1,  // Shrink only overflowing axes to fit LCD; keep non-overflowing 1:1
    AspectFit = 2,    // Shrink preserving aspect ratio (no upscaling)
};

enum class AspectPolicy {
    ParAware = 0,     // Respect core pixel aspect ratio (par_num:par_den)
    Crt4x3 = 1,       // Normalize target display aspect ratio to 4:3 CRT
};

struct ViewportLayout {
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = 0;
    int dst_h = 0;
    int pan_x = 0;
    int pan_y = 0;
    int max_pan_x = 0;
    int max_pan_y = 0;
    int active_w = 0;
    int active_h = 0;
};

struct ScalerConfig {
    DisplayMode mode = DisplayMode::OverflowFit;
    AspectPolicy aspect_policy = AspectPolicy::ParAware;
    bool show_borders = false;
    int pan_x = 0;
    int pan_y = 0;
};

class Scaler {
public:
    static ViewportLayout compute_layout(const czx_frame_t& frame, const ScalerConfig& cfg);

    // Scales/crops `frame` according to `cfg` into a 320x170 RGB565 destination buffer.
    // Returns the computed layout (with clamped pan coordinates).
    ViewportLayout render(const czx_frame_t& frame, const ScalerConfig& cfg, uint16_t* dst_rgb565);

private:
    struct Tap {
        uint16_t src_idx;
        uint16_t weight_q12;
    };

    struct AxisSpan {
        uint32_t offset;
        uint16_t count;
    };

    struct AxisTable {
        int src_len = 0;
        int dst_len = 0;
        std::vector<AxisSpan> spans;
        std::vector<Tap> taps;

        void ensure(int src, int dst);
    };

    AxisTable x_table_;
    AxisTable y_table_;
    std::vector<uint8_t> tmp_rgb_;
};

const char* display_mode_name(DisplayMode mode) noexcept;
const char* display_mode_short(DisplayMode mode) noexcept;
const char* aspect_policy_name(AspectPolicy policy) noexcept;

}  // namespace czx::video
