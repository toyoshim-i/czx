#include "platform/desktop_sdl2.hpp"

#include "video/scaler.hpp"

#include <algorithm>
#include <cmath>

namespace czx::platform {

namespace {

czx_key_t sdl_keycode_to_czx(SDL_Keycode sym) noexcept {
    if (sym >= SDLK_a && sym <= SDLK_z) {
        return static_cast<czx_key_t>(CZX_KEY_A + (sym - SDLK_a));
    }
    if (sym >= SDLK_0 && sym <= SDLK_9) {
        return static_cast<czx_key_t>(CZX_KEY_0 + (sym - SDLK_0));
    }
    if (sym >= SDLK_F1 && sym <= SDLK_F12) {
        return static_cast<czx_key_t>(CZX_KEY_F1 + (sym - SDLK_F1));
    }
    switch (sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return CZX_KEY_ENTER;
        case SDLK_ESCAPE: return CZX_KEY_ESC;
        case SDLK_BACKSPACE: return CZX_KEY_BACKSPACE;
        case SDLK_TAB: return CZX_KEY_TAB;
        case SDLK_SPACE: return CZX_KEY_SPACE;
        case SDLK_MINUS: return CZX_KEY_MINUS;
        case SDLK_EQUALS: return CZX_KEY_EQUAL;
        case SDLK_LEFTBRACKET: return CZX_KEY_LEFTBRACE;
        case SDLK_RIGHTBRACKET: return CZX_KEY_RIGHTBRACE;
        case SDLK_BACKSLASH: return CZX_KEY_BACKSLASH;
        case SDLK_SEMICOLON: return CZX_KEY_SEMICOLON;
        case SDLK_QUOTE: return CZX_KEY_APOSTROPHE;
        case SDLK_BACKQUOTE: return CZX_KEY_GRAVE;
        case SDLK_COMMA: return CZX_KEY_COMMA;
        case SDLK_PERIOD: return CZX_KEY_DOT;
        case SDLK_SLASH: return CZX_KEY_SLASH;
        case SDLK_UP: return CZX_KEY_UP;
        case SDLK_DOWN: return CZX_KEY_DOWN;
        case SDLK_LEFT: return CZX_KEY_LEFT;
        case SDLK_RIGHT: return CZX_KEY_RIGHT;
        case SDLK_HOME: return CZX_KEY_HOME;
        case SDLK_END: return CZX_KEY_END;
        case SDLK_PAGEUP: return CZX_KEY_PAGEUP;
        case SDLK_PAGEDOWN: return CZX_KEY_PAGEDOWN;
        case SDLK_INSERT: return CZX_KEY_INSERT;
        case SDLK_DELETE: return CZX_KEY_DELETE;
        case SDLK_LSHIFT: return CZX_KEY_LSHIFT;
        case SDLK_RSHIFT: return CZX_KEY_RSHIFT;
        case SDLK_LCTRL: return CZX_KEY_LCTRL;
        case SDLK_RCTRL: return CZX_KEY_RCTRL;
        case SDLK_LALT: return CZX_KEY_LALT;
        case SDLK_RALT: return CZX_KEY_RALT;
        case SDLK_LGUI: return CZX_KEY_LMETA;
        case SDLK_RGUI: return CZX_KEY_RMETA;
        case SDLK_MUTE: return CZX_KEY_MUTE;
        case SDLK_VOLUMEDOWN: return CZX_KEY_VOLUMEDOWN;
        case SDLK_VOLUMEUP: return CZX_KEY_VOLUMEUP;
        default: return CZX_KEY_NONE;
    }
}

}  // namespace

DesktopSdl2Context::~DesktopSdl2Context() {
    if (overlay_texture) SDL_DestroyTexture(overlay_texture);
    if (native_texture) SDL_DestroyTexture(native_texture);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool DesktopSdl2Context::init(const PlatformOptions& opts) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    const int init_w = opts.desktop_native ? kDefaultNativeW : (video::kLcdWidth * kLcdScale);
    const int init_h = opts.desktop_native ? kDefaultNativeH : (video::kLcdHeight * kLcdScale);
    const char* init_title = opts.desktop_native
                                 ? "czx - Desktop Mode"
                                 : "czx - Cardputer Zero Simulator (320x170)";
    window = SDL_CreateWindow(
        init_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        init_w, init_h, SDL_WINDOW_RESIZABLE);
    if (!window) return false;
    last_title = init_title;
    SDL_SetWindowMinimumSize(window, kMinWinW, kMinWinH);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) return false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                video::kLcdWidth, video::kLcdHeight);
    overlay_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        video::kLcdWidth, video::kLcdHeight);
    if (overlay_texture) {
        SDL_SetTextureBlendMode(overlay_texture, SDL_BLENDMODE_BLEND);
    }
    return texture != nullptr && overlay_texture != nullptr;
}

void DesktopSdl2Context::resize_window(int w, int h) {
    if (!window) return;
    const int nw = std::max(kMinWinW, w);
    const int nh = std::max(kMinWinH, h);
    int cur_w = 0;
    int cur_h = 0;
    SDL_GetWindowSize(window, &cur_w, &cur_h);
    if (nw == cur_w && nh == cur_h) return;
    SDL_SetWindowSize(window, nw, nh);
    redraw_needed = true;
}

bool DesktopSdl2Context::get_desktop_bounds(int& max_w, int& max_h) const {
    if (!window) {
        max_w = 0;
        max_h = 0;
        return false;
    }
    int disp = SDL_GetWindowDisplayIndex(window);
    if (disp < 0) disp = 0;
    SDL_DisplayMode dm{};
    if (SDL_GetCurrentDisplayMode(disp, &dm) != 0 || dm.w <= 0 || dm.h <= 0) {
        max_w = 0;
        max_h = 0;
        return false;
    }
    max_w = std::max(kMinWinW, dm.w);
    max_h = std::max(kMinWinH, dm.h);
    return true;
}

void DesktopSdl2Context::set_window_title(const char* title) {
    if (!window || !title) return;
    if (last_title == title) return;
    last_title = title;
    SDL_SetWindowTitle(window, title);
}

void DesktopSdl2Context::present(const uint16_t* rgb565) {
    if (!renderer || !texture || !rgb565) return;
    SDL_UpdateTexture(texture, nullptr, rgb565, video::kLcdWidth * static_cast<int>(sizeof(uint16_t)));

    int out_w = video::kLcdWidth * kLcdScale;
    int out_h = video::kLcdHeight * kLcdScale;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    out_w = std::max(1, out_w);
    out_h = std::max(1, out_h);

    const double scale = std::min(
        static_cast<double>(out_w) / static_cast<double>(video::kLcdWidth),
        static_cast<double>(out_h) / static_cast<double>(video::kLcdHeight));
    const int dst_w = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)), 1, out_w);
    const int dst_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)), 1, out_h);
    const SDL_Rect dst{(out_w - dst_w) / 2, (out_h - dst_h) / 2, dst_w, dst_h};

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_RenderPresent(renderer);
}

void DesktopSdl2Context::present_native(const NativeFrameView& view) {
    if (!renderer) return;

    int out_w = kDefaultNativeW;
    int out_h = kDefaultNativeH;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    out_w = std::max(1, out_w);
    out_h = std::max(1, out_h);

    const uint8_t mr = static_cast<uint8_t>((view.margin_xrgb >> 16) & 0xFFu);
    const uint8_t mg = static_cast<uint8_t>((view.margin_xrgb >> 8) & 0xFFu);
    const uint8_t mb = static_cast<uint8_t>(view.margin_xrgb & 0xFFu);
    SDL_SetRenderDrawColor(renderer, mr, mg, mb, 255);
    SDL_RenderClear(renderer);

    if (view.xrgb && view.width > 0 && view.height > 0) {
        if (!native_texture || native_tex_w != view.width || native_tex_h != view.height) {
            if (native_texture) {
                SDL_DestroyTexture(native_texture);
                native_texture = nullptr;
            }
            native_texture = SDL_CreateTexture(
                renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
                view.width, view.height);
            if (native_texture) {
                SDL_SetTextureBlendMode(native_texture, SDL_BLENDMODE_NONE);
                native_tex_w = view.width;
                native_tex_h = view.height;
            } else {
                native_tex_w = 0;
                native_tex_h = 0;
            }
        }
        if (native_texture) {
            SDL_UpdateTexture(native_texture, nullptr, view.xrgb,
                              view.width * static_cast<int>(sizeof(uint32_t)));
            double eff_w = static_cast<double>(view.width);
            const double eff_h = static_cast<double>(view.height);
            if (view.crt_4x3) {
                eff_w = eff_h * (4.0 / 3.0);
            } else {
                const int pn = std::max(1, view.par_num);
                const int pd = std::max(1, view.par_den);
                eff_w = eff_w * static_cast<double>(pn) / static_cast<double>(pd);
            }
            const double scale = std::min(
                static_cast<double>(out_w) / eff_w,
                static_cast<double>(out_h) / eff_h);
            const int dst_w = std::clamp(static_cast<int>(std::lround(eff_w * scale)), 1, out_w);
            const int dst_h = std::clamp(static_cast<int>(std::lround(eff_h * scale)), 1, out_h);
            const SDL_Rect dst{(out_w - dst_w) / 2, (out_h - dst_h) / 2, dst_w, dst_h};
            SDL_RenderCopy(renderer, native_texture, nullptr, &dst);
        }
    }

    if (view.overlay_argb && overlay_texture) {
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
            ov_w = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)), 1, out_w);
            ov_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)), 1, out_h);
        }
        const int ov_x = (out_w - ov_w) / 2;
        const int ov_y = (out_h - ov_h) / 2;
        if (view.dim_outside_overlay) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            if (ov_y > 0) {
                const SDL_Rect top{0, 0, out_w, ov_y};
                const SDL_Rect bot{0, ov_y + ov_h, out_w, out_h - (ov_y + ov_h)};
                SDL_RenderFillRect(renderer, &top);
                SDL_RenderFillRect(renderer, &bot);
            }
            if (ov_x > 0) {
                const SDL_Rect left{0, ov_y, ov_x, ov_h};
                const SDL_Rect right{ov_x + ov_w, ov_y, out_w - (ov_x + ov_w), ov_h};
                SDL_RenderFillRect(renderer, &left);
                SDL_RenderFillRect(renderer, &right);
            }
        }
        SDL_UpdateTexture(overlay_texture, nullptr, view.overlay_argb,
                          video::kLcdWidth * static_cast<int>(sizeof(uint32_t)));
        const SDL_Rect ov_dst{ov_x, ov_y, ov_w, ov_h};
        SDL_RenderCopy(renderer, overlay_texture, nullptr, &ov_dst);
    }

    SDL_RenderPresent(renderer);
}

void DesktopSdl2Context::poll_events(std::vector<czx::input::RawKeyEvent>& out_events) {
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            close_requested = true;
        } else if (ev.type == SDL_WINDOWEVENT &&
                   (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_EXPOSED)) {
            redraw_needed = true;
        } else if (ev.type == SDL_MOUSEMOTION) {
            if (ev.motion.xrel != 0 || ev.motion.yrel != 0) {
                pending_mouse.push_back(RawMouseEvent{ev.motion.xrel, ev.motion.yrel, mouse_buttons});
            }
        } else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
            uint32_t bit = 0;
            if (ev.button.button == SDL_BUTTON_LEFT) {
                bit = CZX_MOUSE_LEFT;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                bit = CZX_MOUSE_RIGHT;
            } else if (ev.button.button == SDL_BUTTON_MIDDLE) {
                bit = CZX_MOUSE_MIDDLE;
            }
            if (bit != 0) {
                if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    mouse_buttons |= bit;
                } else {
                    mouse_buttons &= ~bit;
                }
                pending_mouse.push_back(RawMouseEvent{0, 0, mouse_buttons});
            }
        } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            const czx_key_t key = sdl_keycode_to_czx(ev.key.keysym.sym);
            if (key != CZX_KEY_NONE) {
                out_events.push_back(czx::input::RawKeyEvent{
                    key,
                    ev.type == SDL_KEYDOWN,
                    ev.key.repeat != 0,
                    0,
                });
            }
        }
    }
}

void DesktopSdl2Context::poll_mouse(std::vector<RawMouseEvent>& out_mouse) {
    if (pending_mouse.empty()) return;
    out_mouse.insert(out_mouse.end(), pending_mouse.begin(), pending_mouse.end());
    pending_mouse.clear();
}

}  // namespace czx::platform
