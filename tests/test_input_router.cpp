#include "input/input_router.hpp"
#include "input/tca8418_keymap.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace czx::input;

#define CHECK(expr)                                                                 \
    do {                                                                            \
        if (!(expr)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {

struct Spy {
    std::vector<HostCommand> cmds;
    std::vector<czx_key_event_t> guest_keys;
    std::vector<czx_key_event_t> menu_keys;
    std::vector<uint32_t> joy_states;
    int pan_x = 0;
    int pan_y = 0;
};

void on_cmd(void* u, HostCommand c) { static_cast<Spy*>(u)->cmds.push_back(c); }
void on_pan(void* u, int dx, int dy) {
    auto* s = static_cast<Spy*>(u);
    s->pan_x += dx;
    s->pan_y += dy;
}
void on_menu(void* u, const czx_key_event_t& ev) { static_cast<Spy*>(u)->menu_keys.push_back(ev); }
void on_guest(void* u, const czx_key_event_t& ev) { static_cast<Spy*>(u)->guest_keys.push_back(ev); }
void on_joy(void* u, uint32_t, uint32_t b) { static_cast<Spy*>(u)->joy_states.push_back(b); }

void test_tca8418_keymap() {
    Tca8418Keymap km;

    // Cardputer Zero TCA8418 SYM scancodes must decode to symbols on device
    const auto dollar = km.map_evdev(40, 1, true);
    CHECK(dollar.pressed && dollar.codepoint == '$');
    const auto star = km.map_evdev(52, 1, true);
    CHECK(star.codepoint == '*');
    const auto quote = km.map_evdev(82, 1, true);
    CHECK(quote.codepoint == '"');
    const auto qmark = km.map_evdev(93, 1, true);
    CHECK(qmark.codepoint == '?');

    // Standard keyboard (non-TCA8418) preserves Linux KEY_APOSTROPHE (40) with no forced SYM codepoint
    const auto apos = km.map_evdev(40, 1, false);
    CHECK(apos.key == CZX_KEY_APOSTROPHE && apos.codepoint == 0);
}

void test_alt_shortcuts_and_panning() {
    Spy spy;
    InputRouter router;
    router.set_callbacks(RouterCallbacks{on_cmd, on_pan, on_menu, on_guest, on_joy, &spy});
    router.set_native_pan_enabled(true);

    // Alt + 4..8 must emit host commands and never leak to guest
    router.handle_raw_key({CZX_KEY_LALT, true, false, 0});
    CHECK(router.alt_held());
    router.handle_raw_key({CZX_KEY_4, true, false, '4'});
    router.handle_raw_key({CZX_KEY_4, false, false, 0});
    router.handle_raw_key({CZX_KEY_8, true, false, '8'});
    router.handle_raw_key({CZX_KEY_8, false, false, 0});
    CHECK(spy.cmds.size() == 2);
    CHECK(spy.cmds[0] == HostCommand::SetModeNativePan);
    CHECK(spy.cmds[1] == HostCommand::ToggleMenu);
    CHECK(spy.guest_keys.empty());

    // Alt + C pans right immediately and continuously via tick()
    router.handle_raw_key({CZX_KEY_C, true, false, 'c'});
    CHECK(spy.pan_x == 8 && spy.pan_y == 0);
    router.tick(0.5);
    CHECK(spy.pan_x > 8);
    const int before_release = spy.pan_x;

    // Releasing Alt stops continuous pan
    router.handle_raw_key({CZX_KEY_LALT, false, false, 0});
    CHECK(!router.alt_held());
    router.tick(0.5);
    CHECK(spy.pan_x == before_release);
    router.handle_raw_key({CZX_KEY_C, false, false, 0});
    CHECK(spy.guest_keys.empty());

    // Plain SYM + keys pass directly to guest
    router.handle_raw_key({CZX_KEY_SYM, true, false, 0});
    router.handle_raw_key({CZX_KEY_4, true, false, '$'});
    CHECK(spy.guest_keys.size() == 2);
    CHECK(spy.guest_keys[1].codepoint == '$');
    CHECK((spy.guest_keys[1].mods & CZX_MOD_SYM) != 0);
}

void test_joystick_and_menu_routing() {
    Spy spy;
    InputRouter router;
    router.set_callbacks(RouterCallbacks{on_cmd, on_pan, on_menu, on_guest, on_joy, &spy});

    router.set_input_mode(InputMode::Joystick);
    router.handle_raw_key({CZX_KEY_F, true, false, 'f'});
    router.handle_raw_key({CZX_KEY_SPACE, true, false, ' '});
    router.handle_raw_key({CZX_KEY_A, true, false, 'a'}); // non-joystick key still passes to guest
    CHECK(spy.joy_states.size() == 2);
    CHECK(spy.joy_states[0] == CZX_JOY_UP);
    CHECK(spy.joy_states[1] == (CZX_JOY_UP | CZX_JOY_FIRE1));
    CHECK(spy.guest_keys.size() == 1 && spy.guest_keys[0].key == CZX_KEY_A);

    // Opening menu clears joystick state and redirects keys to menu
    router.set_menu_open(true);
    CHECK(spy.joy_states.back() == 0);
    router.handle_raw_key({CZX_KEY_X, true, false, 'x'});
    CHECK(spy.menu_keys.size() == 1 && spy.menu_keys[0].key == CZX_KEY_X);
    CHECK(spy.guest_keys.size() == 1);
}

}  // namespace

int main() {
    test_tca8418_keymap();
    test_alt_shortcuts_and_panning();
    test_joystick_and_menu_routing();
    std::printf("test_input_router: all assertions passed\n");
    return 0;
}
