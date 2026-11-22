#include "platform/desktop_x11.hpp"

#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace czx::platform {

namespace {

uint32_t dim_xrgb(uint32_t px) noexcept {
    const uint32_t r = (((px >> 16) & 0xFFu) * 115u) >> 8;
    const uint32_t g = (((px >> 8) & 0xFFu) * 115u) >> 8;
    const uint32_t b = ((px & 0xFFu) * 115u) >> 8;
    return (r << 16) | (g << 8) | b;
}

uint32_t blend_argb_over_xrgb(uint32_t dst, uint32_t src) noexcept {
    const uint32_t a = (src >> 24) & 0xFFu;
    if (a == 0u) return dst;
    if (a == 255u) return src & 0x00FFFFFFu;
    const uint32_t inv = 255u - a;
    const uint32_t sr = (src >> 16) & 0xFFu;
    const uint32_t sg = (src >> 8) & 0xFFu;
    const uint32_t sb = src & 0xFFu;
    const uint32_t dr = (dst >> 16) & 0xFFu;
    const uint32_t dg = (dst >> 8) & 0xFFu;
    const uint32_t db = dst & 0xFFu;
    const uint32_t rr = (sr * a + dr * inv + 127u) / 255u;
    const uint32_t rg = (sg * a + dg * inv + 127u) / 255u;
    const uint32_t rb = (sb * a + db * inv + 127u) / 255u;
    return (rr << 16) | (rg << 8) | rb;
}

void build_nearest_lut(std::vector<int>& lut, int dst_len, int src_len) {
    lut.resize(static_cast<size_t>(dst_len));
    for (int d = 0; d < dst_len; ++d) {
        int s = static_cast<int>((static_cast<int64_t>(d) * src_len) / dst_len);
        if (s >= src_len) s = src_len - 1;
        lut[static_cast<size_t>(d)] = s;
    }
}

void composite_overlay_xrgb(uint32_t* buf, int win_w, int win_h,
                            const uint32_t* overlay_argb, bool dim_outside) {
    int ov_w = 0;
    int ov_h = 0;
    int ui_scale = 0;
    if (win_w >= video::kLcdWidth && win_h >= video::kLcdHeight) {
        ui_scale = std::max(1, std::min(win_w / video::kLcdWidth, win_h / video::kLcdHeight));
        ov_w = video::kLcdWidth * ui_scale;
        ov_h = video::kLcdHeight * ui_scale;
    } else {
        const double scale = std::min(
            static_cast<double>(win_w) / static_cast<double>(video::kLcdWidth),
            static_cast<double>(win_h) / static_cast<double>(video::kLcdHeight));
        ov_w = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)), 1, win_w);
        ov_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)), 1, win_h);
    }
    const int ov_x = (win_w - ov_w) / 2;
    const int ov_y = (win_h - ov_h) / 2;

    if (dim_outside) {
        for (int y = 0; y < win_h; ++y) {
            uint32_t* row = buf + static_cast<size_t>(y) * static_cast<size_t>(win_w);
            if (y < ov_y || y >= ov_y + ov_h) {
                for (int x = 0; x < win_w; ++x) {
                    row[x] = dim_xrgb(row[x]);
                }
            } else {
                for (int x = 0; x < ov_x; ++x) {
                    row[x] = dim_xrgb(row[x]);
                }
                for (int x = ov_x + ov_w; x < win_w; ++x) {
                    row[x] = dim_xrgb(row[x]);
                }
            }
        }
    }

    if (ui_scale > 0) {
        for (int sy = 0; sy < video::kLcdHeight; ++sy) {
            const uint32_t* ov_row = overlay_argb + static_cast<size_t>(sy) * video::kLcdWidth;
            const int dy0 = ov_y + sy * ui_scale;
            for (int sx = 0; sx < video::kLcdWidth; ++sx) {
                const uint32_t src = ov_row[sx];
                const uint32_t a = (src >> 24) & 0xFFu;
                if (a == 0u) continue;
                const int dx0 = ov_x + sx * ui_scale;
                if (a == 255u) {
                    const uint32_t solid = src & 0x00FFFFFFu;
                    for (int ry = 0; ry < ui_scale; ++ry) {
                        uint32_t* dst = buf + static_cast<size_t>(dy0 + ry) * static_cast<size_t>(win_w) + dx0;
                        std::fill_n(dst, ui_scale, solid);
                    }
                } else {
                    for (int ry = 0; ry < ui_scale; ++ry) {
                        uint32_t* dst = buf + static_cast<size_t>(dy0 + ry) * static_cast<size_t>(win_w) + dx0;
                        for (int rx = 0; rx < ui_scale; ++rx) {
                            dst[rx] = blend_argb_over_xrgb(dst[rx], src);
                        }
                    }
                }
            }
        }
    } else {
        std::vector<int> ov_x_lut;
        std::vector<int> ov_y_lut;
        build_nearest_lut(ov_x_lut, ov_w, video::kLcdWidth);
        build_nearest_lut(ov_y_lut, ov_h, video::kLcdHeight);
        for (int dy = 0; dy < ov_h; ++dy) {
            const uint32_t* ov_row =
                overlay_argb + static_cast<size_t>(ov_y_lut[static_cast<size_t>(dy)]) * video::kLcdWidth;
            uint32_t* dst_row =
                buf + static_cast<size_t>(ov_y + dy) * static_cast<size_t>(win_w) + ov_x;
            for (int dx = 0; dx < ov_w; ++dx) {
                const uint32_t src = ov_row[ov_x_lut[static_cast<size_t>(dx)]];
                if ((src >> 24) == 0u) continue;
                dst_row[dx] = blend_argb_over_xrgb(dst_row[dx], src);
            }
        }
    }
}

czx_key_t keysym_to_czx(KeySym ks) noexcept {
    if (ks >= XK_a && ks <= XK_z) {
        return static_cast<czx_key_t>(CZX_KEY_A + (ks - XK_a));
    }
    if (ks >= XK_A && ks <= XK_Z) {
        return static_cast<czx_key_t>(CZX_KEY_A + (ks - XK_A));
    }
    if (ks >= XK_0 && ks <= XK_9) {
        return static_cast<czx_key_t>(CZX_KEY_0 + (ks - XK_0));
    }
    if (ks >= XK_F1 && ks <= XK_F12) {
        return static_cast<czx_key_t>(CZX_KEY_F1 + (ks - XK_F1));
    }

    switch (ks) {
        case XK_Return:
        case XK_KP_Enter: return CZX_KEY_ENTER;
        case XK_Escape: return CZX_KEY_ESC;
        case XK_BackSpace: return CZX_KEY_BACKSPACE;
        case XK_Tab: return CZX_KEY_TAB;
        case XK_space: return CZX_KEY_SPACE;
        case XK_minus:
        case XK_underscore: return CZX_KEY_MINUS;
        case XK_equal:
        case XK_plus: return CZX_KEY_EQUAL;
        case XK_bracketleft:
        case XK_braceleft: return CZX_KEY_LEFTBRACE;
        case XK_bracketright:
        case XK_braceright: return CZX_KEY_RIGHTBRACE;
        case XK_backslash:
        case XK_bar: return CZX_KEY_BACKSLASH;
        case XK_semicolon:
        case XK_colon: return CZX_KEY_SEMICOLON;
        case XK_apostrophe:
        case XK_quotedbl: return CZX_KEY_APOSTROPHE;
        case XK_grave:
        case XK_asciitilde: return CZX_KEY_GRAVE;
        case XK_comma:
        case XK_less: return CZX_KEY_COMMA;
        case XK_period:
        case XK_greater: return CZX_KEY_DOT;
        case XK_slash:
        case XK_question: return CZX_KEY_SLASH;
        case XK_Up: return CZX_KEY_UP;
        case XK_Down: return CZX_KEY_DOWN;
        case XK_Left: return CZX_KEY_LEFT;
        case XK_Right: return CZX_KEY_RIGHT;
        case XK_Home: return CZX_KEY_HOME;
        case XK_End: return CZX_KEY_END;
        case XK_Page_Up: return CZX_KEY_PAGEUP;
        case XK_Page_Down: return CZX_KEY_PAGEDOWN;
        case XK_Insert: return CZX_KEY_INSERT;
        case XK_Delete: return CZX_KEY_DELETE;
        case XK_Shift_L: return CZX_KEY_LSHIFT;
        case XK_Shift_R: return CZX_KEY_RSHIFT;
        case XK_Control_L: return CZX_KEY_LCTRL;
        case XK_Control_R: return CZX_KEY_RCTRL;
        case XK_Alt_L:
        case XK_Meta_L: return CZX_KEY_LALT;
        case XK_Alt_R:
        case XK_Meta_R: return CZX_KEY_RALT;
        case XK_Super_L: return CZX_KEY_LMETA;
        case XK_Super_R: return CZX_KEY_RMETA;
        case XK_Print: return CZX_KEY_PRINT;
        case XK_Help: return CZX_KEY_HELP;
        default: return CZX_KEY_NONE;
    }
}

}  // namespace

DesktopX11Context::~DesktopX11Context() {
    if (image) {
        image->data = nullptr;
        XDestroyImage(image);
        image = nullptr;
    }
    if (display) {
        if (gc) XFreeGC(display, gc);
        if (window) XDestroyWindow(display, window);
        XCloseDisplay(display);
        display = nullptr;
    }
}

bool DesktopX11Context::ensure_ximage() {
    if (!display || win_w <= 0 || win_h <= 0) return false;
    if (image && img_w == win_w && img_h == win_h) {
        return true;
    }
    if (image) {
        image->data = nullptr;
        XDestroyImage(image);
        image = nullptr;
    }
    const int screen = DefaultScreen(display);
    Visual* visual = DefaultVisual(display, screen);
    const int depth = DefaultDepth(display, screen);
    xrgb_buf.assign(static_cast<size_t>(win_w) * static_cast<size_t>(win_h), 0);
    image = XCreateImage(
        display, visual, static_cast<unsigned int>(depth), ZPixmap, 0,
        reinterpret_cast<char*>(xrgb_buf.data()),
        static_cast<unsigned int>(win_w), static_cast<unsigned int>(win_h),
        32, win_w * 4);
    if (!image) {
        img_w = 0;
        img_h = 0;
        return false;
    }
    img_w = win_w;
    img_h = win_h;
    return true;
}

bool DesktopX11Context::init(const PlatformOptions& opts) {
    display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "czx: failed to open X11 display\n");
        return false;
    }

    Bool detectable = False;
    XkbSetDetectableAutoRepeat(display, True, &detectable);

    const int screen = DefaultScreen(display);
    win_w = opts.desktop_native ? kDefaultNativeW : (video::kLcdWidth * kLcdScale);
    win_h = opts.desktop_native ? kDefaultNativeH : (video::kLcdHeight * kLcdScale);

    window = XCreateSimpleWindow(
        display, RootWindow(display, screen),
        0, 0, static_cast<unsigned int>(win_w), static_cast<unsigned int>(win_h),
        0, BlackPixel(display, screen), BlackPixel(display, screen));

    const char* init_title = opts.desktop_native
                                 ? "czx - Desktop Mode"
                                 : "czx - Cardputer Zero Simulator (320x170)";
    XStoreName(display, window, init_title);
    last_title = init_title;

    XSizeHints* hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize;
        hints->min_width = kMinWinW;
        hints->min_height = kMinWinH;
        XSetWMNormalHints(display, window, hints);
        XFree(hints);
    }

    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    XSelectInput(display, window,
                 KeyPressMask | KeyReleaseMask | FocusChangeMask | ExposureMask |
                     StructureNotifyMask | PointerMotionMask | ButtonPressMask |
                     ButtonReleaseMask | LeaveWindowMask);

    gc = XCreateGC(display, window, 0, nullptr);
    if (!ensure_ximage()) {
        return false;
    }

    XMapWindow(display, window);
    XFlush(display);
    return true;
}

void DesktopX11Context::resize_window(int w, int h) {
    if (!display || !window) return;
    const int nw = std::max(kMinWinW, w);
    const int nh = std::max(kMinWinH, h);
    if (nw == win_w && nh == win_h) return;
    XResizeWindow(display, window, static_cast<unsigned int>(nw), static_cast<unsigned int>(nh));
    XFlush(display);
    win_w = nw;
    win_h = nh;
    has_last_mouse = false;
    redraw_needed = true;
}

bool DesktopX11Context::get_desktop_bounds(int& max_w, int& max_h) const {
    if (!display) {
        max_w = 0;
        max_h = 0;
        return false;
    }
    const int screen = DefaultScreen(display);
    max_w = std::max(kMinWinW, DisplayWidth(display, screen));
    max_h = std::max(kMinWinH, DisplayHeight(display, screen));
    return true;
}

void DesktopX11Context::set_window_title(const char* title) {
    if (!display || !window || !title) return;
    if (last_title == title) return;
    last_title = title;
    XStoreName(display, window, title);
    XFlush(display);
}

void DesktopX11Context::present(const uint16_t* rgb565) {
    if (!display || !rgb565 || !ensure_ximage()) return;

    const double scale = std::min(
        static_cast<double>(win_w) / static_cast<double>(video::kLcdWidth),
        static_cast<double>(win_h) / static_cast<double>(video::kLcdHeight));
    const int dst_w = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdWidth) * scale)), 1, win_w);
    const int dst_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(video::kLcdHeight) * scale)), 1, win_h);
    const int dst_x = (win_w - dst_w) / 2;
    const int dst_y = (win_h - dst_h) / 2;

    if (dst_x != 0 || dst_y != 0 || dst_w != win_w || dst_h != win_h) {
        std::fill(xrgb_buf.begin(), xrgb_buf.end(), 0u);
    }

    if (dst_w == video::kLcdWidth * kLcdScale && dst_h == video::kLcdHeight * kLcdScale) {
        for (int sy = 0; sy < video::kLcdHeight; ++sy) {
            const uint16_t* src_row = rgb565 + sy * video::kLcdWidth;
            uint32_t* dst_base =
                xrgb_buf.data() + static_cast<size_t>(dst_y + sy * kLcdScale) * static_cast<size_t>(win_w) + dst_x;
            for (int sx = 0; sx < video::kLcdWidth; ++sx) {
                uint8_t r = 0, g = 0, b = 0;
                video::unpack_rgb565(src_row[sx], r, g, b);
                const uint32_t px =
                    (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8) |
                    static_cast<uint32_t>(b);
                const int dx = sx * kLcdScale;
                dst_base[dx + 0] = px;
                dst_base[dx + 1] = px;
                dst_base[dx + 2] = px;
            }
            std::copy_n(dst_base, dst_w, dst_base + win_w);
            std::copy_n(dst_base, dst_w, dst_base + win_w * 2);
        }
    } else {
        build_nearest_lut(x_lut, dst_w, video::kLcdWidth);
        build_nearest_lut(y_lut, dst_h, video::kLcdHeight);
        for (int dy = 0; dy < dst_h; ++dy) {
            const uint16_t* src_row = rgb565 + y_lut[static_cast<size_t>(dy)] * video::kLcdWidth;
            uint32_t* dst_row =
                xrgb_buf.data() + static_cast<size_t>(dst_y + dy) * static_cast<size_t>(win_w) + dst_x;
            for (int dx = 0; dx < dst_w; ++dx) {
                uint8_t r = 0, g = 0, b = 0;
                video::unpack_rgb565(src_row[x_lut[static_cast<size_t>(dx)]], r, g, b);
                dst_row[dx] =
                    (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8) |
                    static_cast<uint32_t>(b);
            }
        }
    }

    XPutImage(display, window, gc, image, 0, 0, 0, 0,
              static_cast<unsigned int>(win_w),
              static_cast<unsigned int>(win_h));
    XFlush(display);
}

void DesktopX11Context::present_native(const NativeFrameView& view) {
    if (!display || !ensure_ximage()) return;
    if (!view.xrgb || view.width <= 0 || view.height <= 0) {
        std::fill(xrgb_buf.begin(), xrgb_buf.end(), view.margin_xrgb);
    } else {
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
            static_cast<double>(win_w) / eff_w,
            static_cast<double>(win_h) / eff_h);
        const int dst_w = std::clamp(static_cast<int>(std::lround(eff_w * scale)), 1, win_w);
        const int dst_h = std::clamp(static_cast<int>(std::lround(eff_h * scale)), 1, win_h);
        const int dst_x = (win_w - dst_w) / 2;
        const int dst_y = (win_h - dst_h) / 2;

        if (dst_x != 0 || dst_y != 0 || dst_w != win_w || dst_h != win_h) {
            std::fill(xrgb_buf.begin(), xrgb_buf.end(), view.margin_xrgb);
        }

        if (dst_w == view.width && dst_h == view.height) {
            for (int dy = 0; dy < dst_h; ++dy) {
                const uint32_t* src_row = view.xrgb + static_cast<size_t>(dy) * static_cast<size_t>(view.width);
                uint32_t* dst_row =
                    xrgb_buf.data() + static_cast<size_t>(dst_y + dy) * static_cast<size_t>(win_w) + dst_x;
                std::copy_n(src_row, dst_w, dst_row);
            }
        } else {
            build_nearest_lut(x_lut, dst_w, view.width);
            build_nearest_lut(y_lut, dst_h, view.height);
            for (int dy = 0; dy < dst_h; ++dy) {
                const uint32_t* src_row =
                    view.xrgb + static_cast<size_t>(y_lut[static_cast<size_t>(dy)]) * static_cast<size_t>(view.width);
                uint32_t* dst_row =
                    xrgb_buf.data() + static_cast<size_t>(dst_y + dy) * static_cast<size_t>(win_w) + dst_x;
                for (int dx = 0; dx < dst_w; ++dx) {
                    dst_row[dx] = src_row[x_lut[static_cast<size_t>(dx)]];
                }
            }
        }
    }

    if (view.overlay_argb) {
        composite_overlay_xrgb(xrgb_buf.data(), win_w, win_h, view.overlay_argb, view.dim_outside_overlay);
    }

    XPutImage(display, window, gc, image, 0, 0, 0, 0,
              static_cast<unsigned int>(win_w),
              static_cast<unsigned int>(win_h));
    XFlush(display);
}

void DesktopX11Context::poll_events(std::vector<czx::input::RawKeyEvent>& out_events) {
    if (!display) return;

    while (XPending(display) > 0) {
        XEvent xev{};
        XNextEvent(display, &xev);

        switch (xev.type) {
            case ClientMessage:
                if (static_cast<Atom>(xev.xclient.data.l[0]) == wm_delete) {
                    close_requested = true;
                }
                break;

            case ConfigureNotify: {
                const int nw = std::max(1, xev.xconfigure.width);
                const int nh = std::max(1, xev.xconfigure.height);
                if (nw != win_w || nh != win_h) {
                    win_w = nw;
                    win_h = nh;
                    has_last_mouse = false;
                    redraw_needed = true;
                }
                break;
            }

            case Expose:
                if (xev.xexpose.count == 0) {
                    redraw_needed = true;
                }
                break;

            case LeaveNotify:
                has_last_mouse = false;
                break;

            case FocusOut:
                has_last_mouse = false;
                for (czx_key_t k : held_keys) {
                    out_events.push_back(czx::input::RawKeyEvent{k, false, false, 0});
                }
                held_keys.clear();
                break;

            case MotionNotify: {
                const int mx = xev.xmotion.x;
                const int my = xev.xmotion.y;
                if (has_last_mouse) {
                    const int32_t dx = mx - last_mouse_x;
                    const int32_t dy = my - last_mouse_y;
                    if (dx != 0 || dy != 0) {
                        pending_mouse.push_back(RawMouseEvent{dx, dy, mouse_buttons});
                    }
                }
                last_mouse_x = mx;
                last_mouse_y = my;
                has_last_mouse = true;
                break;
            }

            case ButtonPress:
            case ButtonRelease: {
                uint32_t bit = 0;
                if (xev.xbutton.button == Button1) {
                    bit = CZX_MOUSE_LEFT;
                } else if (xev.xbutton.button == Button3) {
                    bit = CZX_MOUSE_RIGHT;
                } else if (xev.xbutton.button == Button2) {
                    bit = CZX_MOUSE_MIDDLE;
                }
                if (bit != 0) {
                    if (xev.type == ButtonPress) {
                        mouse_buttons |= bit;
                    } else {
                        mouse_buttons &= ~bit;
                    }
                    pending_mouse.push_back(RawMouseEvent{0, 0, mouse_buttons});
                }
                break;
            }

            case KeyPress:
            case KeyRelease: {
                const bool pressed = (xev.type == KeyPress);
                const KeySym base_ks = XLookupKeysym(&xev.xkey, 0);
                const czx_key_t key = keysym_to_czx(base_ks);
                if (key == CZX_KEY_NONE) {
                    break;
                }

                bool repeat = false;
                if (pressed) {
                    if (std::find(held_keys.begin(), held_keys.end(), key) != held_keys.end()) {
                        repeat = true;
                    } else {
                        held_keys.push_back(key);
                    }
                } else {
                    const auto it = std::find(held_keys.begin(), held_keys.end(), key);
                    if (it != held_keys.end()) {
                        held_keys.erase(it);
                    }
                }

                uint32_t codepoint = 0;
                if (pressed && (xev.xkey.state & (ControlMask | Mod1Mask)) == 0) {
                    char buf[8] = {};
                    KeySym ignored = 0;
                    const int len = XLookupString(&xev.xkey, buf, sizeof(buf) - 1, &ignored, nullptr);
                    if (len == 1 && static_cast<unsigned char>(buf[0]) >= 0x20 &&
                        static_cast<unsigned char>(buf[0]) < 0x7F) {
                        codepoint = static_cast<uint8_t>(buf[0]);
                    }
                }

                out_events.push_back(czx::input::RawKeyEvent{key, pressed, repeat, codepoint});
                break;
            }
            default:
                break;
        }
    }
}

void DesktopX11Context::poll_mouse(std::vector<RawMouseEvent>& out_mouse) {
    if (pending_mouse.empty()) return;
    out_mouse.insert(out_mouse.end(), pending_mouse.begin(), pending_mouse.end());
    pending_mouse.clear();
}

}  // namespace czx::platform
