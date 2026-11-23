#include "platform/sdl2_gles2.hpp"

#include "video/scaler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace czx::platform {

namespace {

constexpr uint32_t kSdlInitVideo = 0x00000020u;
constexpr int kSdlWindowPosCentered = 0x2FFF0000;
constexpr uint32_t kSdlWindowFullscreen = 0x00000001u;
constexpr uint32_t kSdlWindowOpengl = 0x00000002u;
constexpr uint32_t kSdlWindowResizable = 0x00000020u;
constexpr uint32_t kSdlWindowFullscreenDesktop = kSdlWindowFullscreen | 0x00001000u;

constexpr int kSdlGlDoublebuffer = 5;
constexpr int kSdlGlContextMajorVersion = 17;
constexpr int kSdlGlContextMinorVersion = 18;
constexpr int kSdlGlContextProfileMask = 21;
constexpr int kSdlGlContextProfileEs = 0x0004;

constexpr uint32_t kSdlQuit = 0x100u;
constexpr uint32_t kSdlWindowEvent = 0x200u;
constexpr uint32_t kSdlKeyDown = 0x300u;
constexpr uint32_t kSdlKeyUp = 0x301u;
constexpr uint32_t kSdlMouseMotion = 0x400u;
constexpr uint32_t kSdlMouseButtonDown = 0x401u;
constexpr uint32_t kSdlMouseButtonUp = 0x402u;

constexpr uint8_t kSdlWindowEventExposed = 3;
constexpr uint8_t kSdlWindowEventSizeChanged = 6;
constexpr uint8_t kSdlWindowEventFocusLost = 13;

constexpr uint8_t kSdlButtonLeft = 1;
constexpr uint8_t kSdlButtonMiddle = 2;
constexpr uint8_t kSdlButtonRight = 3;

struct SdlDisplayMode {
    uint32_t format;
    int w;
    int h;
    int refresh_rate;
    void* driverdata;
};

struct SdlKeysym {
    int32_t scancode;
    int32_t sym;
    uint16_t mod;
    uint32_t unused;
};

struct SdlWindowEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t window_id;
    uint8_t event;
    uint8_t padding1;
    uint8_t padding2;
    uint8_t padding3;
    int32_t data1;
    int32_t data2;
};

struct SdlKeyboardEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t window_id;
    uint8_t state;
    uint8_t repeat;
    uint8_t padding2;
    uint8_t padding3;
    SdlKeysym keysym;
};

struct SdlMouseMotionEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t window_id;
    uint32_t which;
    uint32_t state;
    int32_t x;
    int32_t y;
    int32_t xrel;
    int32_t yrel;
};

struct SdlMouseButtonEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t window_id;
    uint32_t which;
    uint8_t button;
    uint8_t state;
    uint8_t clicks;
    uint8_t padding1;
    int32_t x;
    int32_t y;
};

union alignas(8) SdlEvent {
    uint32_t type;
    SdlWindowEvent window;
    SdlKeyboardEvent key;
    SdlMouseMotionEvent motion;
    SdlMouseButtonEvent button;
    uint8_t padding[56];
};

static_assert(sizeof(SdlEvent) == 56, "SDL_Event ABI size must be 56 bytes");
static_assert(offsetof(SdlWindowEvent, event) == 12);
static_assert(offsetof(SdlKeyboardEvent, repeat) == 13);
static_assert(offsetof(SdlKeyboardEvent, keysym) == 16);
static_assert(offsetof(SdlKeysym, sym) == 4);
static_assert(offsetof(SdlMouseMotionEvent, xrel) == 28);
static_assert(offsetof(SdlMouseMotionEvent, yrel) == 32);
static_assert(offsetof(SdlMouseButtonEvent, button) == 16);

template <typename Fn>
bool resolve_sdl_sym(void* dl, const char* name, Fn& out) {
    static_assert(sizeof(Fn) == sizeof(void*));
    void* sym = dlsym(dl, name);
    if (!sym) {
        out = nullptr;
        return false;
    }
    std::memcpy(&out, &sym, sizeof(out));
    return true;
}

czx_key_t sdl_scancode_to_czx(int32_t scancode, int32_t sym) noexcept {
    if (scancode >= 4 && scancode <= 29) {
        return static_cast<czx_key_t>(CZX_KEY_A + (scancode - 4));
    }
    if (scancode >= 30 && scancode <= 38) {
        return static_cast<czx_key_t>(CZX_KEY_1 + (scancode - 30));
    }
    if (scancode == 39) {
        return CZX_KEY_0;
    }
    if (scancode >= 58 && scancode <= 69) {
        return static_cast<czx_key_t>(CZX_KEY_F1 + (scancode - 58));
    }

    switch (scancode) {
        case 40:  // RETURN
        case 88:  // KP_ENTER
            return CZX_KEY_ENTER;
        case 41: return CZX_KEY_ESC;
        case 42: return CZX_KEY_BACKSPACE;
        case 43: return CZX_KEY_TAB;
        case 44: return CZX_KEY_SPACE;
        case 45: return CZX_KEY_MINUS;
        case 46: return CZX_KEY_EQUAL;
        case 47: return CZX_KEY_LEFTBRACE;
        case 48: return CZX_KEY_RIGHTBRACE;
        case 49:
        case 50: return CZX_KEY_BACKSLASH;
        case 51: return CZX_KEY_SEMICOLON;
        case 52: return CZX_KEY_APOSTROPHE;
        case 53: return CZX_KEY_GRAVE;
        case 54: return CZX_KEY_COMMA;
        case 55: return CZX_KEY_DOT;
        case 56: return CZX_KEY_SLASH;
        case 70: return CZX_KEY_PRINT;
        case 73: return CZX_KEY_INSERT;
        case 74: return CZX_KEY_HOME;
        case 75: return CZX_KEY_PAGEUP;
        case 76: return CZX_KEY_DELETE;
        case 77: return CZX_KEY_END;
        case 78: return CZX_KEY_PAGEDOWN;
        case 79: return CZX_KEY_RIGHT;
        case 80: return CZX_KEY_LEFT;
        case 81: return CZX_KEY_DOWN;
        case 82: return CZX_KEY_UP;
        case 117: return CZX_KEY_HELP;
        case 127:
        case 262: return CZX_KEY_MUTE;
        case 128: return CZX_KEY_VOLUMEUP;
        case 129: return CZX_KEY_VOLUMEDOWN;
        case 224: return CZX_KEY_LCTRL;
        case 225: return CZX_KEY_LSHIFT;
        case 226: return CZX_KEY_LALT;
        case 227: return CZX_KEY_LMETA;
        case 228: return CZX_KEY_RCTRL;
        case 229: return CZX_KEY_RSHIFT;
        case 230: return CZX_KEY_RALT;
        case 231: return CZX_KEY_RMETA;
        default: break;
    }

    switch (sym) {
        case (1 << 30) | 127:
        case (1 << 30) | 262: return CZX_KEY_MUTE;
        case (1 << 30) | 128: return CZX_KEY_VOLUMEUP;
        case (1 << 30) | 129: return CZX_KEY_VOLUMEDOWN;
        default: return CZX_KEY_NONE;
    }
}

}  // namespace

struct Sdl2Gles2Context::SdlApi {
    int (*SDL_Init)(uint32_t) = nullptr;
    void (*SDL_QuitSubSystem)(uint32_t) = nullptr;
    int (*SDL_GL_SetAttribute)(int, int) = nullptr;
    void* (*SDL_CreateWindow)(const char*, int, int, int, int, uint32_t) = nullptr;
    void (*SDL_DestroyWindow)(void*) = nullptr;
    void (*SDL_SetWindowMinimumSize)(void*, int, int) = nullptr;
    void (*SDL_SetWindowSize)(void*, int, int) = nullptr;
    void (*SDL_GetWindowSize)(void*, int*, int*) = nullptr;
    void (*SDL_SetWindowTitle)(void*, const char*) = nullptr;
    int (*SDL_GetWindowDisplayIndex)(void*) = nullptr;
    int (*SDL_GetCurrentDisplayMode)(int, SdlDisplayMode*) = nullptr;
    void* (*SDL_GL_CreateContext)(void*) = nullptr;
    void (*SDL_GL_DeleteContext)(void*) = nullptr;
    int (*SDL_GL_MakeCurrent)(void*, void*) = nullptr;
    void* (*SDL_GL_GetProcAddress)(const char*) = nullptr;
    int (*SDL_GL_SetSwapInterval)(int) = nullptr;
    void (*SDL_GL_SwapWindow)(void*) = nullptr;
    void (*SDL_GL_GetDrawableSize)(void*, int*, int*) = nullptr;
    int (*SDL_PollEvent)(SdlEvent*) = nullptr;
    int (*SDL_ShowCursor)(int) = nullptr;
    const char* (*SDL_GetError)() = nullptr;
};

bool has_connected_hdmi_connector() {
    std::error_code ec;
    const fs::path drm_dir("/sys/class/drm");
    if (!fs::is_directory(drm_dir, ec)) {
        return false;
    }
    for (const auto& entry : fs::directory_iterator(drm_dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.find("-HDMI-") == std::string::npos) {
            continue;
        }
        std::ifstream ifs(entry.path() / "status");
        if (!ifs.is_open()) continue;
        std::string status;
        std::getline(ifs, status);
        while (!status.empty() && (status.back() == '\n' || status.back() == '\r' || status.back() == ' ')) {
            status.pop_back();
        }
        if (status == "connected") {
            return true;
        }
    }
    return false;
}

bool has_desktop_session() {
    if (const char* wl = std::getenv("WAYLAND_DISPLAY"); wl && wl[0] != '\0') {
        return true;
    }
    if (const char* disp = std::getenv("DISPLAY"); disp && disp[0] != '\0') {
        return true;
    }
    return false;
}

Sdl2Gles2Context::~Sdl2Gles2Context() {
    cleanup();
}

void Sdl2Gles2Context::cleanup() {
    if (gl_ctx && window && sdl && sdl->SDL_GL_MakeCurrent) {
        (void)sdl->SDL_GL_MakeCurrent(window, gl_ctx);
    }
    presenter.cleanup();
    if (gl_ctx && sdl && sdl->SDL_GL_DeleteContext) {
        sdl->SDL_GL_DeleteContext(gl_ctx);
    }
    gl_ctx = nullptr;
    if (window && sdl && sdl->SDL_DestroyWindow) {
        sdl->SDL_DestroyWindow(window);
    }
    window = nullptr;
    if (sdl_video_inited && sdl && sdl->SDL_QuitSubSystem) {
        sdl->SDL_QuitSubSystem(kSdlInitVideo);
    }
    sdl_video_inited = false;
    vsync_enabled = false;
    vsync_toggleable = false;
    fullscreen = false;
    held_keys.clear();
    pending_mouse.clear();
    delete sdl;
    sdl = nullptr;
    if (dl) {
        dlclose(dl);
        dl = nullptr;
    }
}

bool Sdl2Gles2Context::init(const PlatformOptions& opts) {
    cleanup();
    dl = dlopen("libSDL2-2.0.so.0", RTLD_LAZY);
    if (!dl) {
        return false;
    }

    auto* api = new SdlApi();
    const bool syms_ok =
        resolve_sdl_sym(dl, "SDL_Init", api->SDL_Init) &&
        resolve_sdl_sym(dl, "SDL_QuitSubSystem", api->SDL_QuitSubSystem) &&
        resolve_sdl_sym(dl, "SDL_GL_SetAttribute", api->SDL_GL_SetAttribute) &&
        resolve_sdl_sym(dl, "SDL_CreateWindow", api->SDL_CreateWindow) &&
        resolve_sdl_sym(dl, "SDL_DestroyWindow", api->SDL_DestroyWindow) &&
        resolve_sdl_sym(dl, "SDL_SetWindowMinimumSize", api->SDL_SetWindowMinimumSize) &&
        resolve_sdl_sym(dl, "SDL_SetWindowSize", api->SDL_SetWindowSize) &&
        resolve_sdl_sym(dl, "SDL_GetWindowSize", api->SDL_GetWindowSize) &&
        resolve_sdl_sym(dl, "SDL_SetWindowTitle", api->SDL_SetWindowTitle) &&
        resolve_sdl_sym(dl, "SDL_GetWindowDisplayIndex", api->SDL_GetWindowDisplayIndex) &&
        resolve_sdl_sym(dl, "SDL_GetCurrentDisplayMode", api->SDL_GetCurrentDisplayMode) &&
        resolve_sdl_sym(dl, "SDL_GL_CreateContext", api->SDL_GL_CreateContext) &&
        resolve_sdl_sym(dl, "SDL_GL_DeleteContext", api->SDL_GL_DeleteContext) &&
        resolve_sdl_sym(dl, "SDL_GL_MakeCurrent", api->SDL_GL_MakeCurrent) &&
        resolve_sdl_sym(dl, "SDL_GL_GetProcAddress", api->SDL_GL_GetProcAddress) &&
        resolve_sdl_sym(dl, "SDL_GL_SetSwapInterval", api->SDL_GL_SetSwapInterval) &&
        resolve_sdl_sym(dl, "SDL_GL_SwapWindow", api->SDL_GL_SwapWindow) &&
        resolve_sdl_sym(dl, "SDL_PollEvent", api->SDL_PollEvent);

    (void)resolve_sdl_sym(dl, "SDL_GL_GetDrawableSize", api->SDL_GL_GetDrawableSize);
    (void)resolve_sdl_sym(dl, "SDL_ShowCursor", api->SDL_ShowCursor);
    (void)resolve_sdl_sym(dl, "SDL_GetError", api->SDL_GetError);

    if (!syms_ok) {
        delete api;
        cleanup();
        return false;
    }
    sdl = api;

    if (sdl->SDL_Init(kSdlInitVideo) != 0) {
        std::fprintf(stderr, "czx: SDL_Init failed: %s\n", sdl->SDL_GetError ? sdl->SDL_GetError() : "unknown");
        cleanup();
        return false;
    }
    sdl_video_inited = true;

    (void)sdl->SDL_GL_SetAttribute(kSdlGlContextProfileMask, kSdlGlContextProfileEs);
    (void)sdl->SDL_GL_SetAttribute(kSdlGlContextMajorVersion, 2);
    (void)sdl->SDL_GL_SetAttribute(kSdlGlContextMinorVersion, 0);
    (void)sdl->SDL_GL_SetAttribute(kSdlGlDoublebuffer, 1);

    fullscreen = opts.fullscreen;
    const int init_w = opts.desktop_native ? kDefaultNativeW : (video::kLcdWidth * kLcdScale);
    const int init_h = opts.desktop_native ? kDefaultNativeH : (video::kLcdHeight * kLcdScale);
    const char* init_title = fullscreen
                                 ? "czx - HDMI Fullscreen"
                                 : (opts.desktop_native ? "czx - Desktop Mode"
                                                        : "czx - Cardputer Zero Simulator (320x170)");
    const uint32_t win_flags =
        kSdlWindowOpengl | (fullscreen ? kSdlWindowFullscreenDesktop : kSdlWindowResizable);

    window = sdl->SDL_CreateWindow(init_title, kSdlWindowPosCentered, kSdlWindowPosCentered,
                                   init_w, init_h, win_flags);
    if (!window) {
        std::fprintf(stderr, "czx: SDL_CreateWindow failed: %s\n",
                     sdl->SDL_GetError ? sdl->SDL_GetError() : "unknown");
        cleanup();
        return false;
    }
    last_title = init_title;
    if (!fullscreen) {
        sdl->SDL_SetWindowMinimumSize(window, kMinWinW, kMinWinH);
    } else if (sdl->SDL_ShowCursor) {
        (void)sdl->SDL_ShowCursor(0);
    }

    gl_ctx = sdl->SDL_GL_CreateContext(window);
    if (!gl_ctx || sdl->SDL_GL_MakeCurrent(window, gl_ctx) != 0) {
        std::fprintf(stderr, "czx: SDL_GL_CreateContext(GLES2) failed: %s\n",
                     sdl->SDL_GetError ? sdl->SDL_GetError() : "unknown");
        cleanup();
        return false;
    }

    if (sdl->SDL_GL_SetSwapInterval(opts.vsync ? 1 : 0) == 0) {
        vsync_toggleable = true;
        vsync_enabled = opts.vsync;
    } else {
        vsync_toggleable = false;
        vsync_enabled = false;
    }

    if (!presenter.init(sdl->SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "czx: GLES2 presenter init failed\n");
        cleanup();
        return false;
    }

    return true;
}

bool Sdl2Gles2Context::make_current() const {
    if (!sdl || !sdl->SDL_GL_MakeCurrent || !window || !gl_ctx) {
        return false;
    }
    return sdl->SDL_GL_MakeCurrent(window, gl_ctx) == 0;
}

bool Sdl2Gles2Context::set_vsync_enabled(bool enabled) {
    if (!vsync_toggleable || !make_current()) {
        return false;
    }
    if (sdl->SDL_GL_SetSwapInterval(enabled ? 1 : 0) == 0) {
        vsync_enabled = enabled;
        return true;
    }
    return false;
}

void Sdl2Gles2Context::drawable_size(int& out_w, int& out_h) const {
    out_w = kDefaultNativeW;
    out_h = kDefaultNativeH;
    if (!sdl || !window) return;
    if (sdl->SDL_GL_GetDrawableSize) {
        sdl->SDL_GL_GetDrawableSize(window, &out_w, &out_h);
    } else if (sdl->SDL_GetWindowSize) {
        sdl->SDL_GetWindowSize(window, &out_w, &out_h);
    }
    out_w = std::max(1, out_w);
    out_h = std::max(1, out_h);
}

void Sdl2Gles2Context::present(const uint16_t* rgb565) {
    if (!make_current() || !rgb565) return;
    int out_w = 0;
    int out_h = 0;
    drawable_size(out_w, out_h);
    presenter.present_lcd(out_w, out_h, rgb565);
    sdl->SDL_GL_SwapWindow(window);
}

void Sdl2Gles2Context::present_native(const NativeFrameView& view) {
    if (!make_current()) return;
    int out_w = 0;
    int out_h = 0;
    drawable_size(out_w, out_h);
    presenter.present_native(out_w, out_h, view);
    sdl->SDL_GL_SwapWindow(window);
}

void Sdl2Gles2Context::present_hw(const NativeHwFrameView& view) {
    if (!make_current()) return;
    int out_w = 0;
    int out_h = 0;
    drawable_size(out_w, out_h);
    presenter.present_hw(out_w, out_h, view);
    sdl->SDL_GL_SwapWindow(window);
}

void Sdl2Gles2Context::resize_window(int w, int h) {
    if (!sdl || !window || fullscreen) return;
    const int nw = std::max(kMinWinW, w);
    const int nh = std::max(kMinWinH, h);
    int cur_w = 0;
    int cur_h = 0;
    sdl->SDL_GetWindowSize(window, &cur_w, &cur_h);
    if (nw == cur_w && nh == cur_h) return;
    sdl->SDL_SetWindowSize(window, nw, nh);
    redraw_needed = true;
}

bool Sdl2Gles2Context::get_desktop_bounds(int& max_w, int& max_h) const {
    if (!sdl || !window) {
        max_w = 0;
        max_h = 0;
        return false;
    }
    int disp = sdl->SDL_GetWindowDisplayIndex(window);
    if (disp < 0) disp = 0;
    SdlDisplayMode dm{};
    if (sdl->SDL_GetCurrentDisplayMode(disp, &dm) != 0 || dm.w <= 0 || dm.h <= 0) {
        max_w = 0;
        max_h = 0;
        return false;
    }
    max_w = std::max(kMinWinW, dm.w);
    max_h = std::max(kMinWinH, dm.h);
    return true;
}

void Sdl2Gles2Context::set_window_title(const char* title) {
    if (!sdl || !window || !title || last_title == title) return;
    last_title = title;
    sdl->SDL_SetWindowTitle(window, title);
}

void Sdl2Gles2Context::poll_events(std::vector<czx::input::RawKeyEvent>& out_events) {
    if (!sdl || !sdl->SDL_PollEvent) return;
    SdlEvent ev{};
    while (sdl->SDL_PollEvent(&ev)) {
        if (ev.type == kSdlQuit) {
            close_requested = true;
        } else if (ev.type == kSdlWindowEvent) {
            if (ev.window.event == kSdlWindowEventSizeChanged ||
                ev.window.event == kSdlWindowEventExposed) {
                redraw_needed = true;
            } else if (ev.window.event == kSdlWindowEventFocusLost) {
                for (czx_key_t k : held_keys) {
                    out_events.push_back(czx::input::RawKeyEvent{k, false, false, 0});
                }
                held_keys.clear();
            }
        } else if (ev.type == kSdlMouseMotion) {
            if (ev.motion.xrel != 0 || ev.motion.yrel != 0) {
                pending_mouse.push_back(RawMouseEvent{ev.motion.xrel, ev.motion.yrel, mouse_buttons});
            }
        } else if (ev.type == kSdlMouseButtonDown || ev.type == kSdlMouseButtonUp) {
            uint32_t bit = 0;
            if (ev.button.button == kSdlButtonLeft) {
                bit = CZX_MOUSE_LEFT;
            } else if (ev.button.button == kSdlButtonRight) {
                bit = CZX_MOUSE_RIGHT;
            } else if (ev.button.button == kSdlButtonMiddle) {
                bit = CZX_MOUSE_MIDDLE;
            }
            if (bit != 0) {
                if (ev.type == kSdlMouseButtonDown) {
                    mouse_buttons |= bit;
                } else {
                    mouse_buttons &= ~bit;
                }
                pending_mouse.push_back(RawMouseEvent{0, 0, mouse_buttons});
            }
        } else if (ev.type == kSdlKeyDown || ev.type == kSdlKeyUp) {
            const czx_key_t key = sdl_scancode_to_czx(ev.key.keysym.scancode, ev.key.keysym.sym);
            if (key != CZX_KEY_NONE) {
                const bool pressed = (ev.type == kSdlKeyDown);
                if (pressed) {
                    if (std::find(held_keys.begin(), held_keys.end(), key) == held_keys.end()) {
                        held_keys.push_back(key);
                    }
                } else {
                    const auto it = std::find(held_keys.begin(), held_keys.end(), key);
                    if (it != held_keys.end()) {
                        held_keys.erase(it);
                    }
                }
                out_events.push_back(czx::input::RawKeyEvent{
                    key,
                    pressed,
                    ev.key.repeat != 0,
                    0,
                });
            }
        }
    }
}

void Sdl2Gles2Context::poll_mouse(std::vector<RawMouseEvent>& out_mouse) {
    if (pending_mouse.empty()) return;
    out_mouse.insert(out_mouse.end(), pending_mouse.begin(), pending_mouse.end());
    pending_mouse.clear();
}

}  // namespace czx::platform
