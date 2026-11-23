#include "platform/gles2_presenter.hpp"

#include "video/scaler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace czx::platform {

namespace {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLsizeiptr = ptrdiff_t;

constexpr GLenum kGlFalse = 0;
constexpr GLenum kGlTrue = 1;
constexpr GLenum kGlNoError = 0;
constexpr GLenum kGlTriangleStrip = 0x0005;
constexpr GLenum kGlUnsignedByte = 0x1401;
constexpr GLenum kGlUnsignedShort565 = 0x8363;
constexpr GLenum kGlFloat = 0x1406;
constexpr GLenum kGlTexture2d = 0x0DE1;
constexpr GLenum kGlTexture0 = 0x84C0;
constexpr GLenum kGlTextureMagFilter = 0x2800;
constexpr GLenum kGlTextureMinFilter = 0x2801;
constexpr GLenum kGlTextureWrapS = 0x2802;
constexpr GLenum kGlTextureWrapT = 0x2803;
constexpr GLint kGlNearest = 0x2600;
constexpr GLint kGlClampToEdge = 0x812F;
constexpr GLenum kGlBlend = 0x0BE2;
constexpr GLenum kGlSrcAlpha = 0x0302;
constexpr GLenum kGlOneMinusSrcAlpha = 0x0303;
constexpr GLenum kGlDepthTest = 0x0B71;
constexpr GLenum kGlCullFace = 0x0B44;
constexpr GLenum kGlScissorTest = 0x0C11;
constexpr GLenum kGlStencilTest = 0x0B90;
constexpr GLenum kGlDither = 0x0BD0;
constexpr GLenum kGlRgb = 0x1907;
constexpr GLenum kGlRgba = 0x1908;
constexpr GLenum kGlUnpackAlignment = 0x0CF5;
constexpr GLenum kGlMaxTextureSize = 0x0D33;
constexpr GLenum kGlColorBufferBit = 0x00004000;
constexpr GLenum kGlArrayBuffer = 0x8892;
constexpr GLenum kGlDynamicDraw = 0x88E8;
constexpr GLenum kGlFragmentShader = 0x8B30;
constexpr GLenum kGlVertexShader = 0x8B31;
constexpr GLenum kGlCompileStatus = 0x8B81;
constexpr GLenum kGlLinkStatus = 0x8B82;
constexpr GLenum kGlFramebuffer = 0x8D40;
constexpr GLenum kGlRenderbuffer = 0x8D41;
constexpr GLenum kGlDepthComponent16 = 0x81A5;
constexpr GLenum kGlColorAttachment0 = 0x8CE0;
constexpr GLenum kGlDepthAttachment = 0x8D00;
constexpr GLenum kGlFramebufferComplete = 0x8CD5;

template <typename Fn>
bool load_proc(Gles2Presenter::GlGetProcAddressFn get_proc, void* fallback_dl, const char* name, Fn& out) {
    static_assert(sizeof(Fn) == sizeof(void*));
    void* sym = get_proc ? get_proc(name) : nullptr;
    if (!sym && fallback_dl) {
        sym = dlsym(fallback_dl, name);
    }
    if (!sym) {
        out = nullptr;
        return false;
    }
    std::memcpy(&out, &sym, sizeof(out));
    return true;
}

constexpr const char* kTexVs =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

constexpr const char* kTexFs =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_swizzle_rb;\n"
    "uniform float u_force_opaque;\n"
    "uniform vec2 u_uv_max;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, min(v_uv, u_uv_max));\n"
    "    if (u_swizzle_rb > 0.5) {\n"
    "        c = c.bgra;\n"
    "    }\n"
    "    if (u_force_opaque > 0.5) {\n"
    "        c.a = 1.0;\n"
    "    }\n"
    "    gl_FragColor = c;\n"
    "}\n";

constexpr const char* kSolidVs =
    "attribute vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

constexpr const char* kSolidFs =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

}  // namespace

struct Gles2Presenter::Procs {
    void (*glActiveTexture)(GLenum) = nullptr;
    void (*glAttachShader)(GLuint, GLuint) = nullptr;
    void (*glBindAttribLocation)(GLuint, GLuint, const char*) = nullptr;
    void (*glBindBuffer)(GLenum, GLuint) = nullptr;
    void (*glBindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*glBindRenderbuffer)(GLenum, GLuint) = nullptr;
    void (*glBindTexture)(GLenum, GLuint) = nullptr;
    void (*glBlendFunc)(GLenum, GLenum) = nullptr;
    void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    GLenum (*glCheckFramebufferStatus)(GLenum) = nullptr;
    void (*glClear)(GLbitfield) = nullptr;
    void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;
    void (*glCompileShader)(GLuint) = nullptr;
    GLuint (*glCreateProgram)() = nullptr;
    GLuint (*glCreateShader)(GLenum) = nullptr;
    void (*glDeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*glDeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*glDeleteProgram)(GLuint) = nullptr;
    void (*glDeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
    void (*glDeleteShader)(GLuint) = nullptr;
    void (*glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*glDisable)(GLenum) = nullptr;
    void (*glDisableVertexAttribArray)(GLuint) = nullptr;
    void (*glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*glEnable)(GLenum) = nullptr;
    void (*glEnableVertexAttribArray)(GLuint) = nullptr;
    void (*glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*glGenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*glGenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*glGenRenderbuffers)(GLsizei, GLuint*) = nullptr;
    void (*glGenTextures)(GLsizei, GLuint*) = nullptr;
    GLenum (*glGetError)() = nullptr;
    void (*glGetIntegerv)(GLenum, GLint*) = nullptr;
    void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (*glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (*glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    GLint (*glGetUniformLocation)(GLuint, const char*) = nullptr;
    void (*glLinkProgram)(GLuint) = nullptr;
    void (*glPixelStorei)(GLenum, GLint) = nullptr;
    void (*glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*glTexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (*glUniform1f)(GLint, GLfloat) = nullptr;
    void (*glUniform1i)(GLint, GLint) = nullptr;
    void (*glUniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*glUseProgram)(GLuint) = nullptr;
    void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
};

void compute_hw_fbo_dimensions(uint32_t base_w,
                               uint32_t base_h,
                               uint32_t max_w,
                               uint32_t max_h,
                               uint32_t& out_w,
                               uint32_t& out_h) noexcept {
    uint32_t rw = (base_w > 0u) ? base_w : max_w;
    uint32_t rh = (base_h > 0u) ? base_h : max_h;
    if (rw == 0u) rw = 320u;
    if (rh == 0u) rh = 240u;
    out_w = std::clamp<uint32_t>(rw, 64u, 2048u);
    out_h = std::clamp<uint32_t>(rh, 64u, 2048u);
}

Gles2Presenter::~Gles2Presenter() {
    cleanup();
}

bool Gles2Presenter::init(GlGetProcAddressFn get_proc, bool prefer_rgb565) {
    cleanup();
    get_proc_ = get_proc;
    prefer_rgb565_ = prefer_rgb565;
    fallback_gles_dl_ = dlopen("libGLESv2.so.2", RTLD_LAZY);
    if (!fallback_gles_dl_) {
        fallback_gles_dl_ = dlopen("libGLESv2.so", RTLD_LAZY);
    }

    auto* p = new Procs();
    const bool ok =
        load_proc(get_proc_, fallback_gles_dl_, "glActiveTexture", p->glActiveTexture) &&
        load_proc(get_proc_, fallback_gles_dl_, "glAttachShader", p->glAttachShader) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBindAttribLocation", p->glBindAttribLocation) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBindBuffer", p->glBindBuffer) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBindFramebuffer", p->glBindFramebuffer) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBindRenderbuffer", p->glBindRenderbuffer) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBindTexture", p->glBindTexture) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBlendFunc", p->glBlendFunc) &&
        load_proc(get_proc_, fallback_gles_dl_, "glBufferData", p->glBufferData) &&
        load_proc(get_proc_, fallback_gles_dl_, "glCheckFramebufferStatus", p->glCheckFramebufferStatus) &&
        load_proc(get_proc_, fallback_gles_dl_, "glClear", p->glClear) &&
        load_proc(get_proc_, fallback_gles_dl_, "glClearColor", p->glClearColor) &&
        load_proc(get_proc_, fallback_gles_dl_, "glColorMask", p->glColorMask) &&
        load_proc(get_proc_, fallback_gles_dl_, "glCompileShader", p->glCompileShader) &&
        load_proc(get_proc_, fallback_gles_dl_, "glCreateProgram", p->glCreateProgram) &&
        load_proc(get_proc_, fallback_gles_dl_, "glCreateShader", p->glCreateShader) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteBuffers", p->glDeleteBuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteFramebuffers", p->glDeleteFramebuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteProgram", p->glDeleteProgram) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteRenderbuffers", p->glDeleteRenderbuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteShader", p->glDeleteShader) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDeleteTextures", p->glDeleteTextures) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDisable", p->glDisable) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDisableVertexAttribArray", p->glDisableVertexAttribArray) &&
        load_proc(get_proc_, fallback_gles_dl_, "glDrawArrays", p->glDrawArrays) &&
        load_proc(get_proc_, fallback_gles_dl_, "glEnable", p->glEnable) &&
        load_proc(get_proc_, fallback_gles_dl_, "glEnableVertexAttribArray", p->glEnableVertexAttribArray) &&
        load_proc(get_proc_, fallback_gles_dl_, "glFramebufferRenderbuffer", p->glFramebufferRenderbuffer) &&
        load_proc(get_proc_, fallback_gles_dl_, "glFramebufferTexture2D", p->glFramebufferTexture2D) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGenBuffers", p->glGenBuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGenFramebuffers", p->glGenFramebuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGenRenderbuffers", p->glGenRenderbuffers) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGenTextures", p->glGenTextures) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetError", p->glGetError) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetIntegerv", p->glGetIntegerv) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetProgramInfoLog", p->glGetProgramInfoLog) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetProgramiv", p->glGetProgramiv) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetShaderInfoLog", p->glGetShaderInfoLog) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetShaderiv", p->glGetShaderiv) &&
        load_proc(get_proc_, fallback_gles_dl_, "glGetUniformLocation", p->glGetUniformLocation) &&
        load_proc(get_proc_, fallback_gles_dl_, "glLinkProgram", p->glLinkProgram) &&
        load_proc(get_proc_, fallback_gles_dl_, "glPixelStorei", p->glPixelStorei) &&
        load_proc(get_proc_, fallback_gles_dl_, "glRenderbufferStorage", p->glRenderbufferStorage) &&
        load_proc(get_proc_, fallback_gles_dl_, "glShaderSource", p->glShaderSource) &&
        load_proc(get_proc_, fallback_gles_dl_, "glTexImage2D", p->glTexImage2D) &&
        load_proc(get_proc_, fallback_gles_dl_, "glTexParameteri", p->glTexParameteri) &&
        load_proc(get_proc_, fallback_gles_dl_, "glTexSubImage2D", p->glTexSubImage2D) &&
        load_proc(get_proc_, fallback_gles_dl_, "glUniform1f", p->glUniform1f) &&
        load_proc(get_proc_, fallback_gles_dl_, "glUniform1i", p->glUniform1i) &&
        load_proc(get_proc_, fallback_gles_dl_, "glUniform2f", p->glUniform2f) &&
        load_proc(get_proc_, fallback_gles_dl_, "glUniform4f", p->glUniform4f) &&
        load_proc(get_proc_, fallback_gles_dl_, "glUseProgram", p->glUseProgram) &&
        load_proc(get_proc_, fallback_gles_dl_, "glVertexAttribPointer", p->glVertexAttribPointer) &&
        load_proc(get_proc_, fallback_gles_dl_, "glViewport", p->glViewport);

    if (!ok) {
        delete p;
        cleanup();
        return false;
    }

    gl_ = p;
    GLint max_sz = 2048;
    gl_->glGetIntegerv(kGlMaxTextureSize, &max_sz);
    max_tex_size_ = std::max(512, max_sz);

    if (!compile_shaders()) {
        cleanup();
        return false;
    }

    gl_->glGenBuffers(1, &vbo_quad_);
    initialized_ = true;
    return true;
}

void Gles2Presenter::release_hw_fbo() {
    if (gl_ && initialized_) {
        if (hw_depth_rbo_) gl_->glDeleteRenderbuffers(1, &hw_depth_rbo_);
        if (hw_fbo_) gl_->glDeleteFramebuffers(1, &hw_fbo_);
        if (hw_tex_) gl_->glDeleteTextures(1, &hw_tex_);
    }
    hw_depth_rbo_ = 0;
    hw_fbo_ = 0;
    hw_tex_ = 0;
    hw_tex_w_ = 0;
    hw_tex_h_ = 0;
    hw_has_depth_ = false;
    hw_is_rgb565_ = false;
}

void Gles2Presenter::release_native_texture() {
    if (gl_ && initialized_ && tex_native_) {
        gl_->glDeleteTextures(1, &tex_native_);
    }
    tex_native_ = 0;
    native_tex_w_ = 0;
    native_tex_h_ = 0;
}

void Gles2Presenter::cleanup() {
    if (gl_ && initialized_) {
        release_hw_fbo();
        release_native_texture();
        if (tex_lcd_) gl_->glDeleteTextures(1, &tex_lcd_);
        if (tex_overlay_) gl_->glDeleteTextures(1, &tex_overlay_);
        if (vbo_quad_) gl_->glDeleteBuffers(1, &vbo_quad_);
        if (prog_tex_) gl_->glDeleteProgram(prog_tex_);
        if (prog_solid_) gl_->glDeleteProgram(prog_solid_);
    }
    hw_depth_rbo_ = 0;
    hw_fbo_ = 0;
    hw_tex_ = 0;
    hw_tex_w_ = 0;
    hw_tex_h_ = 0;
    hw_has_depth_ = false;
    prefer_rgb565_ = false;
    hw_is_rgb565_ = false;
    tex_lcd_ = 0;
    tex_native_ = 0;
    native_tex_w_ = 0;
    native_tex_h_ = 0;
    tex_overlay_ = 0;
    vbo_quad_ = 0;
    prog_tex_ = 0;
    prog_solid_ = 0;
    initialized_ = false;
    delete gl_;
    gl_ = nullptr;
    get_proc_ = nullptr;
    if (fallback_gles_dl_) {
        dlclose(fallback_gles_dl_);
        fallback_gles_dl_ = nullptr;
    }
}

void* Gles2Presenter::get_proc_address(const char* sym) const {
    if (!sym) return nullptr;
    void* addr = get_proc_ ? get_proc_(sym) : nullptr;
    if (!addr && fallback_gles_dl_) {
        addr = dlsym(fallback_gles_dl_, sym);
    }
    return addr;
}

bool Gles2Presenter::compile_shaders() {
    auto compile_one = [&](GLenum type, const char* src) -> GLuint {
        const GLuint sh = gl_->glCreateShader(type);
        if (!sh) return 0;
        const GLint len = static_cast<GLint>(std::strlen(src));
        gl_->glShaderSource(sh, 1, &src, &len);
        gl_->glCompileShader(sh);
        GLint status = 0;
        gl_->glGetShaderiv(sh, kGlCompileStatus, &status);
        if (!status) {
            char log_buf[512] = {};
            GLsizei out_len = 0;
            gl_->glGetShaderInfoLog(sh, sizeof(log_buf) - 1, &out_len, log_buf);
            std::fprintf(stderr, "czx: GLES2 shader compile error: %s\n", log_buf);
            gl_->glDeleteShader(sh);
            return 0;
        }
        return sh;
    };

    auto link_prog = [&](const char* vs_src, const char* fs_src, bool with_uv) -> GLuint {
        const GLuint vs = compile_one(kGlVertexShader, vs_src);
        if (!vs) return 0;
        const GLuint fs = compile_one(kGlFragmentShader, fs_src);
        if (!fs) {
            gl_->glDeleteShader(vs);
            return 0;
        }
        const GLuint prog = gl_->glCreateProgram();
        if (!prog) {
            gl_->glDeleteShader(vs);
            gl_->glDeleteShader(fs);
            return 0;
        }
        gl_->glAttachShader(prog, vs);
        gl_->glAttachShader(prog, fs);
        gl_->glBindAttribLocation(prog, 0, "a_pos");
        if (with_uv) {
            gl_->glBindAttribLocation(prog, 1, "a_uv");
        }
        gl_->glLinkProgram(prog);
        gl_->glDeleteShader(vs);
        gl_->glDeleteShader(fs);
        GLint status = 0;
        gl_->glGetProgramiv(prog, kGlLinkStatus, &status);
        if (!status) {
            char log_buf[512] = {};
            GLsizei out_len = 0;
            gl_->glGetProgramInfoLog(prog, sizeof(log_buf) - 1, &out_len, log_buf);
            std::fprintf(stderr, "czx: GLES2 program link error: %s\n", log_buf);
            gl_->glDeleteProgram(prog);
            return 0;
        }
        return prog;
    };

    prog_tex_ = link_prog(kTexVs, kTexFs, true);
    prog_solid_ = link_prog(kSolidVs, kSolidFs, false);
    if (!prog_tex_ || !prog_solid_) {
        return false;
    }

    u_tex_ = gl_->glGetUniformLocation(prog_tex_, "u_tex");
    u_swizzle_rb_ = gl_->glGetUniformLocation(prog_tex_, "u_swizzle_rb");
    u_force_opaque_ = gl_->glGetUniformLocation(prog_tex_, "u_force_opaque");
    u_uv_max_ = gl_->glGetUniformLocation(prog_tex_, "u_uv_max");
    u_color_ = gl_->glGetUniformLocation(prog_solid_, "u_color");
    return true;
}

void Gles2Presenter::ensure_lcd_texture() {
    if (tex_lcd_) return;
    gl_->glGenTextures(1, &tex_lcd_);
    gl_->glActiveTexture(kGlTexture0);
    gl_->glBindTexture(kGlTexture2d, tex_lcd_);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);
    gl_->glPixelStorei(kGlUnpackAlignment, 1);
    gl_->glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgb), video::kLcdWidth, video::kLcdHeight,
                      0, kGlRgb, kGlUnsignedShort565, nullptr);
}

void Gles2Presenter::ensure_overlay_texture() {
    if (tex_overlay_) return;
    gl_->glGenTextures(1, &tex_overlay_);
    gl_->glActiveTexture(kGlTexture0);
    gl_->glBindTexture(kGlTexture2d, tex_overlay_);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
    gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);
    gl_->glPixelStorei(kGlUnpackAlignment, 1);
    gl_->glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba), video::kLcdWidth, video::kLcdHeight,
                      0, kGlRgba, kGlUnsignedByte, nullptr);
}

void Gles2Presenter::ensure_native_texture(int width, int height) {
    release_hw_fbo();
    const int cw = std::clamp(width, 1, max_tex_size_);
    const int ch = std::clamp(height, 1, max_tex_size_);
    if (!tex_native_) {
        gl_->glGenTextures(1, &tex_native_);
    }
    gl_->glActiveTexture(kGlTexture0);
    gl_->glBindTexture(kGlTexture2d, tex_native_);
    if (native_tex_w_ != cw || native_tex_h_ != ch) {
        gl_->glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);
        gl_->glPixelStorei(kGlUnpackAlignment, 1);
        gl_->glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba), cw, ch, 0,
                          kGlRgba, kGlUnsignedByte, nullptr);
        native_tex_w_ = cw;
        native_tex_h_ = ch;
    }
}

bool Gles2Presenter::hw_ensure_fbo(int width, int height, bool depth, bool /*stencil*/) {
    if (!gl_ || !initialized_) return false;
    const int cw = std::clamp(width, 1, max_tex_size_);
    const int ch = std::clamp(height, 1, max_tex_size_);
    if (hw_fbo_ && hw_tex_ && hw_tex_w_ == cw && hw_tex_h_ == ch && hw_has_depth_ == depth) {
        return true;
    }

    release_native_texture();
    if (hw_fbo_) {
        gl_->glBindFramebuffer(kGlFramebuffer, hw_fbo_);
        gl_->glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2d, 0, 0);
        gl_->glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthAttachment, kGlRenderbuffer, 0);
        gl_->glBindFramebuffer(kGlFramebuffer, 0);
    }
    if (hw_depth_rbo_) {
        gl_->glDeleteRenderbuffers(1, &hw_depth_rbo_);
        hw_depth_rbo_ = 0;
    }
    if (hw_tex_) {
        gl_->glDeleteTextures(1, &hw_tex_);
        hw_tex_ = 0;
    }
    hw_tex_w_ = 0;
    hw_tex_h_ = 0;
    hw_has_depth_ = false;
    hw_is_rgb565_ = false;

    if (!hw_fbo_) {
        gl_->glGenFramebuffers(1, &hw_fbo_);
    }
    gl_->glGenTextures(1, &hw_tex_);

    auto try_alloc = [&](bool use_rgb565, GLenum& out_err, GLenum& out_status) -> bool {
        out_err = kGlNoError;
        out_status = kGlFramebufferComplete;
        while (gl_->glGetError() != kGlNoError) {}

        gl_->glActiveTexture(kGlTexture0);
        gl_->glBindTexture(kGlTexture2d, hw_tex_);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
        gl_->glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);
        gl_->glPixelStorei(kGlUnpackAlignment, 1);
        if (use_rgb565) {
            gl_->glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgb), cw, ch, 0,
                              kGlRgb, kGlUnsignedShort565, nullptr);
        } else {
            gl_->glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba), cw, ch, 0,
                              kGlRgba, kGlUnsignedByte, nullptr);
        }
        out_err = gl_->glGetError();
        gl_->glBindTexture(kGlTexture2d, 0);
        if (out_err != kGlNoError) {
            return false;
        }

        gl_->glBindFramebuffer(kGlFramebuffer, hw_fbo_);
        gl_->glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2d, hw_tex_, 0);

        if (depth) {
            if (!hw_depth_rbo_) {
                gl_->glGenRenderbuffers(1, &hw_depth_rbo_);
            }
            gl_->glBindRenderbuffer(kGlRenderbuffer, hw_depth_rbo_);
            gl_->glRenderbufferStorage(kGlRenderbuffer, kGlDepthComponent16, cw, ch);
            gl_->glBindRenderbuffer(kGlRenderbuffer, 0);
            out_err = gl_->glGetError();
            if (out_err != kGlNoError) {
                gl_->glBindFramebuffer(kGlFramebuffer, 0);
                return false;
            }
            gl_->glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthAttachment, kGlRenderbuffer, hw_depth_rbo_);
        }

        out_status = gl_->glCheckFramebufferStatus(kGlFramebuffer);
        if (out_status == kGlFramebufferComplete) {
            gl_->glDisable(kGlScissorTest);
            gl_->glColorMask(static_cast<GLboolean>(kGlTrue), static_cast<GLboolean>(kGlTrue),
                             static_cast<GLboolean>(kGlTrue), static_cast<GLboolean>(kGlTrue));
            gl_->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl_->glClear(kGlColorBufferBit);
            out_err = gl_->glGetError();
        }
        gl_->glBindFramebuffer(kGlFramebuffer, 0);
        return out_status == kGlFramebufferComplete && out_err == kGlNoError;
    };

    GLenum err = kGlNoError;
    GLenum status = kGlFramebufferComplete;
    bool used_rgb565 = prefer_rgb565_;
    bool allocated = try_alloc(used_rgb565, err, status);
    if (!allocated) {
        used_rgb565 = !prefer_rgb565_;
        allocated = try_alloc(used_rgb565, err, status);
    }
    if (!allocated) {
        release_hw_fbo();
        std::fprintf(stderr,
                     "czx: HW GLES2 FBO %dx%d allocation failed (err=0x%x status=0x%x)\n",
                     cw, ch, static_cast<unsigned>(err), static_cast<unsigned>(status));
        return false;
    }

    hw_tex_w_ = cw;
    hw_tex_h_ = ch;
    hw_has_depth_ = depth;
    hw_is_rgb565_ = used_rgb565;
    std::fprintf(stderr, "czx: HW GLES2 FBO ready (%dx%d %s)\n",
                 cw, ch, used_rgb565 ? "RGB565" : "RGBA8");
    return true;
}

void Gles2Presenter::begin_present(int out_w, int out_h, uint32_t clear_xrgb) {
    gl_->glBindFramebuffer(kGlFramebuffer, 0);
    gl_->glViewport(0, 0, out_w, out_h);
    gl_->glDisable(kGlDepthTest);
    gl_->glDisable(kGlCullFace);
    gl_->glDisable(kGlScissorTest);
    gl_->glDisable(kGlStencilTest);
    gl_->glDisable(kGlDither);
    gl_->glDisable(kGlBlend);
    gl_->glColorMask(static_cast<GLboolean>(kGlTrue), static_cast<GLboolean>(kGlTrue),
                     static_cast<GLboolean>(kGlTrue), static_cast<GLboolean>(kGlTrue));
    gl_->glPixelStorei(kGlUnpackAlignment, 1);

    const float r = static_cast<float>((clear_xrgb >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((clear_xrgb >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(clear_xrgb & 0xFFu) / 255.0f;
    gl_->glClearColor(r, g, b, 1.0f);
    gl_->glClear(kGlColorBufferBit);
}

void Gles2Presenter::end_present() {
    gl_->glDisable(kGlBlend);
    gl_->glDisableVertexAttribArray(0);
    gl_->glDisableVertexAttribArray(1);
    gl_->glBindBuffer(kGlArrayBuffer, 0);
    gl_->glUseProgram(0);
    gl_->glActiveTexture(kGlTexture0);
    gl_->glBindTexture(kGlTexture2d, 0);
}

void Gles2Presenter::draw_textured_quad(uint32_t tex,
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
                                          bool force_opaque) {
    const float x0 = 2.0f * static_cast<float>(dst_x) / static_cast<float>(out_w) - 1.0f;
    const float x1 = 2.0f * static_cast<float>(dst_x + dst_w) / static_cast<float>(out_w) - 1.0f;
    const float y_top = 1.0f - 2.0f * static_cast<float>(dst_y) / static_cast<float>(out_h);
    const float y_bot = 1.0f - 2.0f * static_cast<float>(dst_y + dst_h) / static_cast<float>(out_h);
    const float v_top = bottom_left_origin ? v_max : 0.0f;
    const float v_bot = bottom_left_origin ? 0.0f : v_max;

    const float verts[16] = {
        x0, y_top, 0.0f,  v_top,
        x1, y_top, u_max, v_top,
        x0, y_bot, 0.0f,  v_bot,
        x1, y_bot, u_max, v_bot,
    };

    gl_->glUseProgram(prog_tex_);
    gl_->glActiveTexture(kGlTexture0);
    gl_->glBindTexture(kGlTexture2d, tex);
    gl_->glUniform1i(u_tex_, 0);
    gl_->glUniform1f(u_swizzle_rb_, swizzle_rb ? 1.0f : 0.0f);
    gl_->glUniform1f(u_force_opaque_, force_opaque ? 1.0f : 0.0f);
    gl_->glUniform2f(u_uv_max_, u_clamp_max, v_clamp_max);

    gl_->glBindBuffer(kGlArrayBuffer, vbo_quad_);
    gl_->glBufferData(kGlArrayBuffer, static_cast<GLsizeiptr>(sizeof(verts)), verts, kGlDynamicDraw);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, kGlFloat, static_cast<GLboolean>(kGlFalse), 16, nullptr);
    gl_->glEnableVertexAttribArray(1);
    gl_->glVertexAttribPointer(1, 2, kGlFloat, static_cast<GLboolean>(kGlFalse), 16,
                               reinterpret_cast<const void*>(static_cast<uintptr_t>(8)));
    gl_->glDrawArrays(kGlTriangleStrip, 0, 4);
}

void Gles2Presenter::draw_solid_rect(int x,
                                     int y,
                                     int w,
                                     int h,
                                     int out_w,
                                     int out_h,
                                     float r,
                                     float g,
                                     float b,
                                     float a) {
    if (w <= 0 || h <= 0) return;
    const float x0 = 2.0f * static_cast<float>(x) / static_cast<float>(out_w) - 1.0f;
    const float x1 = 2.0f * static_cast<float>(x + w) / static_cast<float>(out_w) - 1.0f;
    const float y_top = 1.0f - 2.0f * static_cast<float>(y) / static_cast<float>(out_h);
    const float y_bot = 1.0f - 2.0f * static_cast<float>(y + h) / static_cast<float>(out_h);
    const float verts[8] = {
        x0, y_top,
        x1, y_top,
        x0, y_bot,
        x1, y_bot,
    };

    gl_->glUseProgram(prog_solid_);
    gl_->glUniform4f(u_color_, r, g, b, a);
    gl_->glDisableVertexAttribArray(1);
    gl_->glBindBuffer(kGlArrayBuffer, vbo_quad_);
    gl_->glBufferData(kGlArrayBuffer, static_cast<GLsizeiptr>(sizeof(verts)), verts, kGlDynamicDraw);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 2, kGlFloat, static_cast<GLboolean>(kGlFalse), 8, nullptr);
    gl_->glDrawArrays(kGlTriangleStrip, 0, 4);
}

void Gles2Presenter::draw_overlay(int out_w,
                                  int out_h,
                                  const uint32_t* overlay_argb,
                                  bool dim_outside) {
    if (!overlay_argb) return;

    int ov_w = 0;
    int ov_h = 0;
    if (out_w >= video::kLcdWidth && out_h >= video::kLcdHeight) {
        const int ui_scale = std::max(1, std::min(out_w / video::kLcdWidth, out_h / video::kLcdHeight));
        ov_w = video::kLcdWidth * ui_scale;
        ov_h = video::kLcdHeight * ui_scale;
    } else {
        const double scale = std::min(
            static_cast<double>(out_w) / static_cast<double>(video::kLcdWidth),
            static_cast<double>(out_h) / static_cast<double>(video::kLcdHeight));
        ov_w = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)),
                          1, out_w);
        ov_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)),
                          1, out_h);
    }
    const int ov_x = (out_w - ov_w) / 2;
    const int ov_y = (out_h - ov_h) / 2;

    gl_->glEnable(kGlBlend);
    gl_->glBlendFunc(kGlSrcAlpha, kGlOneMinusSrcAlpha);

    if (dim_outside) {
        constexpr float kDimA = 140.0f / 255.0f;
        if (ov_y > 0) {
            draw_solid_rect(0, 0, out_w, ov_y, out_w, out_h, 0.0f, 0.0f, 0.0f, kDimA);
            draw_solid_rect(0, ov_y + ov_h, out_w, out_h - (ov_y + ov_h), out_w, out_h, 0.0f, 0.0f, 0.0f, kDimA);
        }
        if (ov_x > 0) {
            draw_solid_rect(0, ov_y, ov_x, ov_h, out_w, out_h, 0.0f, 0.0f, 0.0f, kDimA);
            draw_solid_rect(ov_x + ov_w, ov_y, out_w - (ov_x + ov_w), ov_h, out_w, out_h, 0.0f, 0.0f, 0.0f, kDimA);
        }
    }

    ensure_overlay_texture();
    gl_->glBindTexture(kGlTexture2d, tex_overlay_);
    gl_->glPixelStorei(kGlUnpackAlignment, 1);
    gl_->glTexSubImage2D(kGlTexture2d, 0, 0, 0, video::kLcdWidth, video::kLcdHeight,
                         kGlRgba, kGlUnsignedByte, overlay_argb);
    draw_textured_quad(tex_overlay_, ov_x, ov_y, ov_w, ov_h, out_w, out_h,
                       1.0f, 1.0f, 1.0f, 1.0f, false, true, false);
}

void Gles2Presenter::present_lcd(int out_w, int out_h, const uint16_t* rgb565) {
    if (!gl_ || !initialized_ || !rgb565 || out_w <= 0 || out_h <= 0) return;
    begin_present(out_w, out_h, 0u);

    ensure_lcd_texture();
    gl_->glBindTexture(kGlTexture2d, tex_lcd_);
    gl_->glPixelStorei(kGlUnpackAlignment, 1);
    gl_->glTexSubImage2D(kGlTexture2d, 0, 0, 0, video::kLcdWidth, video::kLcdHeight,
                         kGlRgb, kGlUnsignedShort565, rgb565);

    const double scale = std::min(
        static_cast<double>(out_w) / static_cast<double>(video::kLcdWidth),
        static_cast<double>(out_h) / static_cast<double>(video::kLcdHeight));
    const int dst_w = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)), 1, out_w);
    const int dst_h = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)), 1, out_h);
    const int dst_x = (out_w - dst_w) / 2;
    const int dst_y = (out_h - dst_h) / 2;

    draw_textured_quad(tex_lcd_, dst_x, dst_y, dst_w, dst_h, out_w, out_h,
                       1.0f, 1.0f, 1.0f, 1.0f, false, false, true);
    end_present();
}

void Gles2Presenter::present_native(int out_w, int out_h, const NativeFrameView& view) {
    if (!gl_ || !initialized_ || out_w <= 0 || out_h <= 0) return;
    begin_present(out_w, out_h, view.margin_xrgb);

    if (view.xrgb && view.width > 0 && view.height > 0) {
        ensure_native_texture(view.width, view.height);
        const int cw = std::min(view.width, native_tex_w_);
        const int ch = std::min(view.height, native_tex_h_);
        gl_->glPixelStorei(kGlUnpackAlignment, 1);
        gl_->glTexSubImage2D(kGlTexture2d, 0, 0, 0, cw, ch, kGlRgba, kGlUnsignedByte, view.xrgb);

        const auto dst = video::compute_native_present_rect(
            view.width, view.height, view.par_num, view.par_den, view.aspect_policy, out_w, out_h);

        draw_textured_quad(tex_native_, dst.x, dst.y, dst.width, dst.height, out_w, out_h,
                           1.0f, 1.0f, 1.0f, 1.0f, false, true, true);
    }

    draw_overlay(out_w, out_h, view.overlay_argb, view.dim_outside_overlay);
    end_present();
}

void Gles2Presenter::present_hw(int out_w, int out_h, const NativeHwFrameView& view) {
    if (!gl_ || !initialized_ || out_w <= 0 || out_h <= 0) return;
    begin_present(out_w, out_h, view.margin_xrgb);

    if (hw_tex_ && hw_tex_w_ > 0 && hw_tex_h_ > 0 && view.width > 0 && view.height > 0) {
        const int w = std::min(view.width, hw_tex_w_);
        const int h = std::min(view.height, hw_tex_h_);
        const auto dst = video::compute_native_present_rect(
            w, h, view.par_num, view.par_den, view.aspect_policy, out_w, out_h);

        const float u_max = static_cast<float>(w) / static_cast<float>(hw_tex_w_);
        const float v_max = static_cast<float>(h) / static_cast<float>(hw_tex_h_);
        const float u_clamp = (static_cast<float>(w) - 0.5f) / static_cast<float>(hw_tex_w_);
        const float v_clamp = (static_cast<float>(h) - 0.5f) / static_cast<float>(hw_tex_h_);

        draw_textured_quad(hw_tex_, dst.x, dst.y, dst.width, dst.height, out_w, out_h,
                           u_max, v_max, u_clamp, v_clamp, view.bottom_left_origin, false, true);
    }

    draw_overlay(out_w, out_h, view.overlay_argb, view.dim_outside_overlay);
    end_present();
}

}  // namespace czx::platform
