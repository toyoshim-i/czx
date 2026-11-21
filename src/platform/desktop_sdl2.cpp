#include "platform/desktop_sdl2.hpp"

#include "video/scaler.hpp"

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
        default: return CZX_KEY_NONE;
    }
}

}  // namespace

DesktopSdl2Context::~DesktopSdl2Context() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool DesktopSdl2Context::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    window = SDL_CreateWindow(
        "czx - Cardputer Zero Simulator (320x170)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        video::kLcdWidth * kScale, video::kLcdHeight * kScale, 0);
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) return false;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                video::kLcdWidth, video::kLcdHeight);
    return texture != nullptr;
}

void DesktopSdl2Context::present(const uint16_t* rgb565) {
    if (!renderer || !texture || !rgb565) return;
    SDL_UpdateTexture(texture, nullptr, rgb565, video::kLcdWidth * static_cast<int>(sizeof(uint16_t)));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void DesktopSdl2Context::poll_events(std::vector<czx::input::RawKeyEvent>& out_events) {
    SDL_Event ev{};
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            close_requested = true;
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

}  // namespace czx::platform
