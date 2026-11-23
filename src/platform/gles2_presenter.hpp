#pragma once

#include "platform/platform.hpp"

#include <cstdint>

namespace czx::platform {

class Gles2Presenter {
public:
    using GlGetProcAddressFn = void* (*)(const char* sym);

    Gles2Presenter() = default;
    ~Gles2Presenter();

    Gles2Presenter(const Gles2Presenter&) = delete;
    Gles2Presenter& operator=(const Gles2Presenter&) = delete;

    bool init(GlGetProcAddressFn get_proc);
    void cleanup();

    void* get_proc_address(const char* sym) const;
    bool hw_ensure_fbo(int width, int height, bool depth, bool stencil);
    uintptr_t hw_fbo() const noexcept { return static_cast<uintptr_t>(hw_fbo_); }

    void present_lcd(int out_w, int out_h, const uint16_t* rgb565);
    void present_native(int out_w, int out_h, const NativeFrameView& view);
    void present_hw(int out_w, int out_h, const NativeHwFrameView& view);

private:
    struct Procs;

    bool compile_shaders();
    void ensure_lcd_texture();
    void ensure_overlay_texture();
    void ensure_native_texture(int width, int height);
    void begin_present(int out_w, int out_h, uint32_t clear_xrgb);
    void end_present();
    void draw_textured_quad(uint32_t tex,
                            int dst_x,
                            int dst_y,
                            int dst_w,
                            int dst_h,
                            int out_w,
                            int out_h,
                            float u_max,
                            float v_max,
                            float u_clamp_max,
                            float v_clamp_max,
                            bool bottom_left_origin,
                            bool swizzle_rb,
                            bool force_opaque);
    void draw_solid_rect(int x, int y, int w, int h, int out_w, int out_h, float r, float g, float b, float a);
    void draw_overlay(int out_w, int out_h, const uint32_t* overlay_argb, bool dim_outside);

    void* fallback_gles_dl_ = nullptr;
    GlGetProcAddressFn get_proc_ = nullptr;
    Procs* gl_ = nullptr;
    bool initialized_ = false;
    int max_tex_size_ = 2048;

    uint32_t prog_tex_ = 0;
    int u_tex_ = -1;
    int u_swizzle_rb_ = -1;
    int u_force_opaque_ = -1;
    int u_uv_max_ = -1;

    uint32_t prog_solid_ = 0;
    int u_color_ = -1;

    uint32_t vbo_quad_ = 0;

    uint32_t tex_lcd_ = 0;
    uint32_t tex_native_ = 0;
    int native_tex_w_ = 0;
    int native_tex_h_ = 0;
    uint32_t tex_overlay_ = 0;

    uint32_t hw_fbo_ = 0;
    uint32_t hw_tex_ = 0;
    uint32_t hw_depth_rbo_ = 0;
    int hw_tex_w_ = 0;
    int hw_tex_h_ = 0;
    bool hw_has_depth_ = false;
};

}  // namespace czx::platform
