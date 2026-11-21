/*
 * SDL-free replacement for third_party/g800/depend.h used when building
 * czx_g800_upstream (-include cmake/g800_czx/depend.h).
 */
#if !defined(DEPEND_H)
#define DEPEND_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif

/* Upstream basic.c defines `static void pause(struct Basic *)`, which clashes with unistd.h. */
#define pause g800_basic_pause

/* Upstream basic.c loadBas() calls fclose() on NULL entries in bas->fp[]. */
static inline int czx_safe_fclose(FILE* fp) {
    return fp ? (fclose)(fp) : 0;
}
#define fclose czx_safe_fclose

#define KEY_NONE         0
#define KEY_BACKSPACE    1
#define KEY_TAB          2
#define KEY_CLEAR        3
#define KEY_RETURN       4
#define KEY_PAUSE        5
#define KEY_ESCAPE       6
#define KEY_SPACE        7
#define KEY_QUOTE        8
#define KEY_COMMA        9
#define KEY_MINUS        10
#define KEY_PERIOD       11
#define KEY_SLASH        12
#define KEY_0            13
#define KEY_1            14
#define KEY_2            15
#define KEY_3            16
#define KEY_4            17
#define KEY_5            18
#define KEY_6            19
#define KEY_7            20
#define KEY_8            21
#define KEY_9            22
#define KEY_SEMICOLON    23
#define KEY_EQUALS       24
#define KEY_LEFTBRACKET  25
#define KEY_BACKSLASH    26
#define KEY_RIGHTBRACKET 27
#define KEY_BACKQUOTE    28
#define KEY_A            29
#define KEY_B            30
#define KEY_C            31
#define KEY_D            32
#define KEY_E            33
#define KEY_F            34
#define KEY_G            35
#define KEY_H            36
#define KEY_I            37
#define KEY_J            38
#define KEY_K            39
#define KEY_L            40
#define KEY_M            41
#define KEY_N            42
#define KEY_O            43
#define KEY_P            44
#define KEY_Q            45
#define KEY_R            46
#define KEY_S            47
#define KEY_T            48
#define KEY_U            49
#define KEY_V            50
#define KEY_W            51
#define KEY_X            52
#define KEY_Y            53
#define KEY_Z            54
#define KEY_DELETE       55
#define KEY_KP0          56
#define KEY_KP1          57
#define KEY_KP2          58
#define KEY_KP3          59
#define KEY_KP4          60
#define KEY_KP5          61
#define KEY_KP6          62
#define KEY_KP7          63
#define KEY_KP8          64
#define KEY_KP9          65
#define KEY_KP_PERIOD    66
#define KEY_KP_DIVIDE    67
#define KEY_KP_MULTIPLY  68
#define KEY_KP_MINUS     69
#define KEY_KP_PLUS      70
#define KEY_KP_ENTER     71
#define KEY_KP_EQUALS    72
#define KEY_UP           73
#define KEY_DOWN         74
#define KEY_RIGHT        75
#define KEY_LEFT         76
#define KEY_INSERT       77
#define KEY_HOME         78
#define KEY_END          79
#define KEY_PAGEUP       80
#define KEY_PAGEDOWN     81
#define KEY_F1           82
#define KEY_F2           83
#define KEY_F3           84
#define KEY_F4           85
#define KEY_F5           86
#define KEY_F6           87
#define KEY_F7           88
#define KEY_F8           89
#define KEY_F9           90
#define KEY_F10          91
#define KEY_F11          92
#define KEY_F12          93
#define KEY_F13          94
#define KEY_F14          95
#define KEY_F15          96
#define KEY_NUMLOCK      97
#define KEY_CAPSLOCK     98
#define KEY_SCROLLOCK    99
#define KEY_RSHIFT       100
#define KEY_LSHIFT       101
#define KEY_RCTRL        102
#define KEY_LCTRL        103
#define KEY_RALT         104
#define KEY_LALT         105
#define KEY_MODE         106
#define KEY_COMPOSE      107
#define KEY_HELP         108
#define KEY_PRINT        109
#define KEY_SYSREQ       110
#define KEY_BREAK        111
#define KEY_MENU         112
#define KEY_POWER        113
#define KEY_UNDERSCORE   114
#define KEY_KANA         115
#define KEY_YEN          116
#define KEY_XFER         117
#define KEY_NFER         118
#define KEY_LAST         127

#define MODKEY_ALT       0x1000
#define MODKEY_CTRL      0x2000
#define MODKEY_SHIFT     0x4000

#endif
