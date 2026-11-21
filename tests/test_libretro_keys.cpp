#include "core/libretro_keys.hpp"
#include "czx/libretro.h"

#include <cstdio>
#include <cstdlib>

using namespace czx::core;

#define CHECK(expr)                                                                 \
    do {                                                                            \
        if (!(expr)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

namespace {

void test_standard_keys() {
    czx_key_event_t a{CZX_KEY_A, true, false, 0, 'a'};
    const auto ma = map_key_to_libretro(a);
    CHECK(ma.retrok == RETROK_a);
    CHECK(ma.character == 'a');
    CHECK(!ma.needs_synthetic_shift);

    czx_key_event_t enter{CZX_KEY_ENTER, true, false, 0, '\r'};
    const auto me = map_key_to_libretro(enter);
    CHECK(me.retrok == RETROK_RETURN);
    CHECK(me.character == '\r');
}

void test_sym_layer_synthetic_shift() {
    // Cardputer Zero SYM+4 emits '$' with CZX_MOD_SYM (no physical Shift)
    czx_key_event_t dollar{CZX_KEY_4, true, false, CZX_MOD_SYM, '$'};
    const auto md = map_key_to_libretro(dollar);
    CHECK(md.retrok == RETROK_4);
    CHECK(md.character == '$');
    CHECK(md.needs_synthetic_shift);
    CHECK((md.retro_mods & RETROKMOD_SHIFT) != 0);

    // Cardputer Zero SYM+8 emits '*' -> RETROK_8 + synthetic Shift
    czx_key_event_t star{CZX_KEY_8, true, false, CZX_MOD_SYM, '*'};
    const auto ms = map_key_to_libretro(star);
    CHECK(ms.retrok == RETROK_8);
    CHECK(ms.character == '*');
    CHECK(ms.needs_synthetic_shift);

    // Cardputer Zero SYM+F emits '"' -> RETROK_QUOTE + synthetic Shift
    czx_key_event_t quote{CZX_KEY_F, true, false, CZX_MOD_SYM, '"'};
    const auto mq = map_key_to_libretro(quote);
    CHECK(mq.retrok == RETROK_QUOTE);
    CHECK(mq.character == '"');
    CHECK(mq.needs_synthetic_shift);

    // Unshifted SYM symbol '=' does NOT synthesize Shift
    czx_key_event_t eq{CZX_KEY_EQUAL, true, false, CZX_MOD_SYM, '='};
    const auto meq = map_key_to_libretro(eq);
    CHECK(meq.retrok == RETROK_EQUALS);
    CHECK(meq.character == '=');
    CHECK(!meq.needs_synthetic_shift);

    // Physical Shift+4 does NOT request redundant synthetic Shift
    czx_key_event_t phys_dollar{CZX_KEY_4, true, false, CZX_MOD_SHIFT, '$'};
    const auto mpd = map_key_to_libretro(phys_dollar);
    CHECK(mpd.retrok == RETROK_4);
    CHECK(mpd.character == '$');
    CHECK(!mpd.needs_synthetic_shift);

    // SYM and FN modifier keys themselves are not forwarded to guest cores
    czx_key_event_t sym{CZX_KEY_SYM, true, false, CZX_MOD_SYM, 0};
    CHECK(map_key_to_libretro(sym).retrok == RETROK_UNKNOWN);
}

}  // namespace

int main() {
    test_standard_keys();
    test_sym_layer_synthetic_shift();
    std::printf("test_libretro_keys: all assertions passed\n");
    return 0;
}
