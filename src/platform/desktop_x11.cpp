#include "platform/desktop_x11.hpp"

#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cstdio>

namespace czx::platform {

namespace {

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

bool DesktopX11Context::init() {
    display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "czx: failed to open X11 display\n");
        return false;
    }

    Bool detectable = False;
    XkbSetDetectableAutoRepeat(display, True, &detectable);

    const int screen = DefaultScreen(display);
    Visual* visual = DefaultVisual(display, screen);
    const int depth = DefaultDepth(display, screen);

    const int win_w = video::kLcdWidth * kScale;
    const int win_h = video::kLcdHeight * kScale;

    window = XCreateSimpleWindow(
        display, RootWindow(display, screen),
        0, 0, static_cast<unsigned int>(win_w), static_cast<unsigned int>(win_h),
        0, BlackPixel(display, screen), BlackPixel(display, screen));

    XStoreName(display, window, "czx - Cardputer Zero Simulator (320x170)");

    XSizeHints* hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = win_w;
        hints->max_width = win_w;
        hints->min_height = win_h;
        hints->max_height = win_h;
        XSetWMNormalHints(display, window, hints);
        XFree(hints);
    }

    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask | ExposureMask);

    gc = XCreateGC(display, window, 0, nullptr);
    xrgb_buf.assign(static_cast<size_t>(win_w) * static_cast<size_t>(win_h), 0);

    image = XCreateImage(
        display, visual, static_cast<unsigned int>(depth), ZPixmap, 0,
        reinterpret_cast<char*>(xrgb_buf.data()),
        static_cast<unsigned int>(win_w), static_cast<unsigned int>(win_h),
        32, win_w * 4);
    if (!image) {
        return false;
    }

    XMapWindow(display, window);
    XFlush(display);
    return true;
}

void DesktopX11Context::present(const uint16_t* rgb565) {
    if (!display || !image || !rgb565) return;

    constexpr int kDstW = video::kLcdWidth * kScale;
    for (int sy = 0; sy < video::kLcdHeight; ++sy) {
        const uint16_t* src_row = rgb565 + sy * video::kLcdWidth;
        uint32_t* dst_base = xrgb_buf.data() + (sy * kScale) * kDstW;
        for (int sx = 0; sx < video::kLcdWidth; ++sx) {
            uint8_t r = 0, g = 0, b = 0;
            video::unpack_rgb565(src_row[sx], r, g, b);
            const uint32_t px =
                (static_cast<uint32_t>(r) << 16) |
                (static_cast<uint32_t>(g) << 8) |
                static_cast<uint32_t>(b);
            const int dx = sx * kScale;
            dst_base[dx + 0] = px;
            dst_base[dx + 1] = px;
            dst_base[dx + 2] = px;
        }
        std::copy_n(dst_base, kDstW, dst_base + kDstW);
        std::copy_n(dst_base, kDstW, dst_base + kDstW * 2);
    }

    XPutImage(display, window, gc, image, 0, 0, 0, 0,
              static_cast<unsigned int>(kDstW),
              static_cast<unsigned int>(video::kLcdHeight * kScale));
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

            case FocusOut:
                for (czx_key_t k : held_keys) {
                    out_events.push_back(czx::input::RawKeyEvent{k, false, false, 0});
                }
                held_keys.clear();
                break;

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

}  // namespace czx::platform
