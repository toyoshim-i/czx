#include <libretro.h>

#define handle_input czx_unused_upstream_handle_input
#define retro_init czx_upstream_retro_init
#define retro_run czx_upstream_retro_run
#include "LIBRETRO/libretro.c"
#undef retro_run
#undef retro_init
#undef handle_input

#include "menu.h"

static bool czx_pad_state[2][6];
static bool czx_mouse_btn[3];

static void czx_handle_ctrl_key(void)
{
    bool ctrl_on = input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LCTRL) ||
                   input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RCTRL);

    if (!pad_buffer[KEY88_CTRL])
    {
        if (!key_buffer[KEY88_CTRL] && ctrl_on)
        {
            quasi88_key(KEY88_CTRL, 1);
            key_buffer[KEY88_CTRL] = true;
        }
        else if (key_buffer[KEY88_CTRL] && !ctrl_on)
        {
            quasi88_key(KEY88_CTRL, 0);
            key_buffer[KEY88_CTRL] = false;
        }
    }
}

void retro_init(void)
{
    czx_upstream_retro_init();

    memset(czx_pad_state, 0, sizeof(czx_pad_state));
    memset(czx_mouse_btn, 0, sizeof(czx_mouse_btn));

    if (load_system_file(KNJ1_ROM, kanji_rom[0][0], 0x20000))
    {
        has_kanji_rom = TRUE;
        menu_lang = MENU_JAPAN;
    }
    else
    {
        has_kanji_rom = FALSE;
        menu_lang = MENU_ENGLISH;
    }

    mouse_mode = MOUSE_JOYSTICK;
    use_swcursor = TRUE;
}

static void czx_handle_input(void)
{
    static const unsigned joy_retro[6] = {
        RETRO_DEVICE_ID_JOYPAD_UP,
        RETRO_DEVICE_ID_JOYPAD_DOWN,
        RETRO_DEVICE_ID_JOYPAD_LEFT,
        RETRO_DEVICE_ID_JOYPAD_RIGHT,
        RETRO_DEVICE_ID_JOYPAD_A,
        RETRO_DEVICE_ID_JOYPAD_B,
    };
    static const int joy_key88[2][6] = {
        {
            KEY88_PAD1_UP,
            KEY88_PAD1_DOWN,
            KEY88_PAD1_LEFT,
            KEY88_PAD1_RIGHT,
            KEY88_PAD1_A,
            KEY88_PAD1_B,
        },
        {
            KEY88_PAD2_UP,
            KEY88_PAD2_DOWN,
            KEY88_PAD2_LEFT,
            KEY88_PAD2_RIGHT,
            KEY88_PAD2_A,
            KEY88_PAD2_B,
        },
    };
    static const int mouse_key88[3] = {
        KEY88_MOUSE_L,
        KEY88_MOUSE_R,
        KEY88_MOUSE_M,
    };
    uint8_t i;
    unsigned port;
    unsigned k;
    unsigned b;
    int16_t mdx;
    int16_t mdy;
    bool mbtns[3];
    bool mouse_clicked;

    input_poll_cb();

    /* Ignore other input while swapping disks */
    if (handle_disk_swap(true, RETRO_DEVICE_ID_JOYPAD_L) ||
        handle_disk_swap(false, RETRO_DEVICE_ID_JOYPAD_R))
    {
        return;
    }

    /* Basics, numbers */
    for (i = 0; i < 64; i++)
        handle_key(i, i);
    for (i = 0; i < 6; i++)
        handle_key(KEY88_BRACKETLEFT + i, RETROK_LEFTBRACKET + i);
    for (i = 0; i < 4; i++)
        handle_key(KEY88_BRACELEFT + i, RETROK_LEFTBRACE + i);
    handle_key(KEY88_RETURN,   RETROK_RETURN);
    handle_key(KEY88_HOME,     RETROK_HOME);
    handle_key(KEY88_UP,       RETROK_UP);
    handle_key(KEY88_RIGHT,    RETROK_RIGHT);
    handle_key(KEY88_INS_DEL,  RETROK_BACKSPACE);
    handle_key(KEY88_GRAPH,    RETROK_LALT);
    handle_key(KEY88_KANA,     RETROK_LSUPER);
    handle_key(KEY88_SHIFT,    RETROK_RSHIFT);
    czx_handle_ctrl_key();
    handle_key(KEY88_STOP,     RETROK_BREAK);
    handle_key(KEY88_ESC,      RETROK_ESCAPE);
    handle_key(KEY88_TAB,      RETROK_TAB);
    handle_key(KEY88_DOWN,     RETROK_DOWN);
    handle_key(KEY88_LEFT,     RETROK_LEFT);
    handle_key(KEY88_HELP,     RETROK_END);
    handle_key(KEY88_COPY,     RETROK_PRINT);
    handle_key(KEY88_CAPS,     RETROK_CAPSLOCK);
    handle_key(KEY88_ROLLUP,   RETROK_PAGEUP);
    handle_key(KEY88_ROLLDOWN, RETROK_PAGEDOWN);
    handle_key(KEY88_BS,       RETROK_BACKSPACE);
    handle_key(KEY88_INS,      RETROK_INSERT);
    handle_key(KEY88_DEL,      RETROK_DELETE);
    handle_key(KEY88_ZENKAKU,  RETROK_RALT);
    handle_key(KEY88_RETURNL,  RETROK_RETURN);
    handle_key(KEY88_RETURNR,  RETROK_RETURN);
    handle_key(KEY88_SHIFTL,   RETROK_LSHIFT);
    handle_key(KEY88_SHIFTR,   RETROK_RSHIFT);

    /* Keypad */
    for (i = 0; i < 10; i++)
        handle_key(KEY88_KP_0 + i, RETROK_KP0 + i);
    handle_key(KEY88_KP_MULTIPLY, RETROK_KP_MULTIPLY);
    handle_key(KEY88_KP_ADD,      RETROK_KP_PLUS);
    handle_key(KEY88_KP_EQUAL,    RETROK_KP_EQUALS);
    handle_key(KEY88_KP_COMMA,    RETROK_KP_ENTER);
    handle_key(KEY88_KP_PERIOD,   RETROK_KP_PERIOD);
    handle_key(KEY88_KP_SUB,      RETROK_KP_MINUS);
    handle_key(KEY88_KP_DIVIDE,   RETROK_KP_DIVIDE);

    /* Alphabet */
    for (i = 0; i < 26; i++)
        handle_key(KEY88_A + i, RETROK_a + i);

    /* Function keys (including F11 status and F12 QUASI88 settings menu) */
    for (i = 0; i < 5; i++)
        handle_key(KEY88_F1 + i, RETROK_F1 + i);
    for (i = 0; i < 5; i++)
        handle_key(KEY88_F6 + i, RETROK_F6 + i);
    handle_key(KEY88_F11, RETROK_F11);
    handle_key(KEY88_F12, RETROK_F12);

    /* Real hardware joypads (ports 0 and 1 via quasi88_pad) */
    for (port = 0; port < 2; port++)
    {
        for (k = 0; k < 6; k++)
        {
            bool on = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, joy_retro[k]) != 0;
            if (on != czx_pad_state[port][k])
            {
                czx_pad_state[port][k] = on;
                if (on && quasi88_is_exec() && mouse_mode != MOUSE_JOYSTICK)
                {
                    mouse_mode = MOUSE_JOYSTICK;
                    keyboard_switch();
                }
                quasi88_pad(joy_key88[port][k], on ? TRUE : FALSE);
            }
        }
    }

    /* Hardware mouse (PC-88 port when in exec, Q8TK pointer when in menu) */
    mdx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    mdy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    mbtns[0] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
    mbtns[1] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
    mbtns[2] = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) != 0;
    mouse_clicked = (mbtns[0] && !czx_mouse_btn[0]) ||
                    (mbtns[1] && !czx_mouse_btn[1]) ||
                    (mbtns[2] && !czx_mouse_btn[2]);
    if ((mdx != 0 || mdy != 0 || mouse_clicked) && quasi88_is_exec() && mouse_mode != MOUSE_MOUSE)
    {
        mouse_mode = MOUSE_MOUSE;
        keyboard_switch();
    }
    if (mdx != 0 || mdy != 0)
    {
        quasi88_mouse_move(mdx, mdy, FALSE);
    }
    for (b = 0; b < 3; b++)
    {
        if (mbtns[b] != czx_mouse_btn[b])
        {
            czx_mouse_btn[b] = mbtns[b];
            quasi88_mouse(mouse_key88[b], mbtns[b] ? TRUE : FALSE);
        }
    }
}

void retro_run(void)
{
    int stat;

    czx_handle_input();
    handle_autotype();
    set_tape_fastforward(tape_loading());

    /* Run the emulation loop until one VSYNC period has completed */
    audio_buf_frames = 0;
    do
    {
        stat = quasi88_loop();
    } while (stat == QUASI88_LOOP_BUSY);

    /* Choosing Quit inside Q8TK sets QUIT mode; recover cleanly back to EXEC */
    if (stat == QUASI88_LOOP_EXIT)
    {
        quasi88_event_flags &= ~EVENT_QUIT;
        quasi88_exec();
    }

    tape_tick();
    disk_tick();

    if (rumble_cb)
        handle_rumble();
    video_cb(screen_buf, WIDTH, HEIGHT, WIDTH * 2);

    /* Prevent a loud audio pop */
    if (frames <= FRAMES_BEFORE_AUDIO)
        memset(audio_buf, 0, audio_buf_frames * 2 * sizeof(INT16));
    if (audio_buf_frames)
        audio_batch_cb(audio_buf, audio_buf_frames);
    if (frames <= FRAMES_BEFORE_AUDIO)
        frames++;
}
