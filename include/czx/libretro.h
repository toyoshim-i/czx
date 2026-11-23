#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RETRO_CALLCONV
#define RETRO_CALLCONV
#endif

#define RETRO_API_VERSION 1

#define RETRO_DEVICE_TYPE_SHIFT 8
#define RETRO_DEVICE_MASK ((1 << RETRO_DEVICE_TYPE_SHIFT) - 1)
#define RETRO_DEVICE_NONE 0
#define RETRO_DEVICE_JOYPAD 1
#define RETRO_DEVICE_MOUSE 2
#define RETRO_DEVICE_KEYBOARD 3
#define RETRO_DEVICE_LIGHTGUN 4
#define RETRO_DEVICE_ANALOG 5
#define RETRO_DEVICE_POINTER 6

#define RETRO_DEVICE_ID_JOYPAD_B 0
#define RETRO_DEVICE_ID_JOYPAD_Y 1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START 3
#define RETRO_DEVICE_ID_JOYPAD_UP 4
#define RETRO_DEVICE_ID_JOYPAD_DOWN 5
#define RETRO_DEVICE_ID_JOYPAD_LEFT 6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT 7
#define RETRO_DEVICE_ID_JOYPAD_A 8
#define RETRO_DEVICE_ID_JOYPAD_X 9
#define RETRO_DEVICE_ID_JOYPAD_L 10
#define RETRO_DEVICE_ID_JOYPAD_R 11
#define RETRO_DEVICE_ID_JOYPAD_L2 12
#define RETRO_DEVICE_ID_JOYPAD_R2 13
#define RETRO_DEVICE_ID_JOYPAD_L3 14
#define RETRO_DEVICE_ID_JOYPAD_R3 15
#define RETRO_DEVICE_ID_JOYPAD_MASK 256

#define RETRO_DEVICE_ID_MOUSE_X 0
#define RETRO_DEVICE_ID_MOUSE_Y 1
#define RETRO_DEVICE_ID_MOUSE_LEFT 2
#define RETRO_DEVICE_ID_MOUSE_RIGHT 3
#define RETRO_DEVICE_ID_MOUSE_WHEELUP 4
#define RETRO_DEVICE_ID_MOUSE_WHEELDOWN 5
#define RETRO_DEVICE_ID_MOUSE_MIDDLE 6

#define RETRO_REGION_NTSC 0
#define RETRO_REGION_PAL 1

#define RETRO_MEMORY_SAVE_RAM 0
#define RETRO_MEMORY_RTC 1
#define RETRO_MEMORY_SYSTEM_RAM 2
#define RETRO_MEMORY_VIDEO_RAM 3

#define RETRO_NUM_CORE_OPTION_VALUES_MAX 128

#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000
#define RETRO_ENVIRONMENT_SET_ROTATION 1
#define RETRO_ENVIRONMENT_GET_OVERSCAN 2
#define RETRO_ENVIRONMENT_GET_CAN_DUPE 3
#define RETRO_ENVIRONMENT_SET_MESSAGE 6
#define RETRO_ENVIRONMENT_SHUTDOWN 7
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL 8
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS 11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK 12
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE 13
#define RETRO_ENVIRONMENT_SET_HW_RENDER 14
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
#define RETRO_ENVIRONMENT_SET_VARIABLES 16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE 17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME 18
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH 19
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK 21
#define RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE 23
#define RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES 24
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27
#define RETRO_ENVIRONMENT_GET_PERF_INTERFACE 28
#define RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY 30
#define RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY 30
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO 32
#define RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO 34
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO 35
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS (36 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_GEOMETRY 37
#define RETRO_ENVIRONMENT_GET_USERNAME 38
#define RETRO_ENVIRONMENT_GET_LANGUAGE 39
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS 44
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE (45 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_LED_INTERFACE (46 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE (47 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_FASTFORWARDING (49 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE (50 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_INPUT_BITMASKS (51 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION 52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS 53
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL 54
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY 55
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION 57
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE 58
#define RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION 59
#define RETRO_ENVIRONMENT_SET_MESSAGE_EXT 60
#define RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS 61
#define RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY 63
#define RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE 64
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 67
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL 68
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK 69
#define RETRO_ENVIRONMENT_SET_VARIABLE 70
#define RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT (72 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY 80

#define RETRO_HW_FRAME_BUFFER_VALID ((void*)-1)

enum retro_language {
    RETRO_LANGUAGE_ENGLISH = 0,
    RETRO_LANGUAGE_JAPANESE = 1,
    RETRO_LANGUAGE_DUMMY = INT_MAX
};

enum retro_key {
    RETROK_UNKNOWN = 0,
    RETROK_FIRST = 0,
    RETROK_BACKSPACE = 8,
    RETROK_TAB = 9,
    RETROK_CLEAR = 12,
    RETROK_RETURN = 13,
    RETROK_PAUSE = 19,
    RETROK_ESCAPE = 27,
    RETROK_SPACE = 32,
    RETROK_EXCLAIM = 33,
    RETROK_QUOTEDBL = 34,
    RETROK_HASH = 35,
    RETROK_DOLLAR = 36,
    RETROK_AMPERSAND = 38,
    RETROK_QUOTE = 39,
    RETROK_LEFTPAREN = 40,
    RETROK_RIGHTPAREN = 41,
    RETROK_ASTERISK = 42,
    RETROK_PLUS = 43,
    RETROK_COMMA = 44,
    RETROK_MINUS = 45,
    RETROK_PERIOD = 46,
    RETROK_SLASH = 47,
    RETROK_0 = 48,
    RETROK_1 = 49,
    RETROK_2 = 50,
    RETROK_3 = 51,
    RETROK_4 = 52,
    RETROK_5 = 53,
    RETROK_6 = 54,
    RETROK_7 = 55,
    RETROK_8 = 56,
    RETROK_9 = 57,
    RETROK_COLON = 58,
    RETROK_SEMICOLON = 59,
    RETROK_LESS = 60,
    RETROK_EQUALS = 61,
    RETROK_GREATER = 62,
    RETROK_QUESTION = 63,
    RETROK_AT = 64,
    RETROK_LEFTBRACKET = 91,
    RETROK_BACKSLASH = 92,
    RETROK_RIGHTBRACKET = 93,
    RETROK_CARET = 94,
    RETROK_UNDERSCORE = 95,
    RETROK_BACKQUOTE = 96,
    RETROK_a = 97,
    RETROK_b = 98,
    RETROK_c = 99,
    RETROK_d = 100,
    RETROK_e = 101,
    RETROK_f = 102,
    RETROK_g = 103,
    RETROK_h = 104,
    RETROK_i = 105,
    RETROK_j = 106,
    RETROK_k = 107,
    RETROK_l = 108,
    RETROK_m = 109,
    RETROK_n = 110,
    RETROK_o = 111,
    RETROK_p = 112,
    RETROK_q = 113,
    RETROK_r = 114,
    RETROK_s = 115,
    RETROK_t = 116,
    RETROK_u = 117,
    RETROK_v = 118,
    RETROK_w = 119,
    RETROK_x = 120,
    RETROK_y = 121,
    RETROK_z = 122,
    RETROK_LEFTBRACE = 123,
    RETROK_BAR = 124,
    RETROK_RIGHTBRACE = 125,
    RETROK_TILDE = 126,
    RETROK_DELETE = 127,
    RETROK_KP0 = 256,
    RETROK_KP1 = 257,
    RETROK_KP2 = 258,
    RETROK_KP3 = 259,
    RETROK_KP4 = 260,
    RETROK_KP5 = 261,
    RETROK_KP6 = 262,
    RETROK_KP7 = 263,
    RETROK_KP8 = 264,
    RETROK_KP9 = 265,
    RETROK_KP_PERIOD = 266,
    RETROK_KP_DIVIDE = 267,
    RETROK_KP_MULTIPLY = 268,
    RETROK_KP_MINUS = 269,
    RETROK_KP_PLUS = 270,
    RETROK_KP_ENTER = 271,
    RETROK_KP_EQUALS = 272,
    RETROK_UP = 273,
    RETROK_DOWN = 274,
    RETROK_RIGHT = 275,
    RETROK_LEFT = 276,
    RETROK_INSERT = 277,
    RETROK_HOME = 278,
    RETROK_END = 279,
    RETROK_PAGEUP = 280,
    RETROK_PAGEDOWN = 281,
    RETROK_F1 = 282,
    RETROK_F2 = 283,
    RETROK_F3 = 284,
    RETROK_F4 = 285,
    RETROK_F5 = 286,
    RETROK_F6 = 287,
    RETROK_F7 = 288,
    RETROK_F8 = 289,
    RETROK_F9 = 290,
    RETROK_F10 = 291,
    RETROK_F11 = 292,
    RETROK_F12 = 293,
    RETROK_NUMLOCK = 300,
    RETROK_CAPSLOCK = 301,
    RETROK_SCROLLOCK = 302,
    RETROK_RSHIFT = 303,
    RETROK_LSHIFT = 304,
    RETROK_RCTRL = 305,
    RETROK_LCTRL = 306,
    RETROK_RALT = 307,
    RETROK_LALT = 308,
    RETROK_RMETA = 309,
    RETROK_LMETA = 310,
    RETROK_LSUPER = 311,
    RETROK_RSUPER = 312,
    RETROK_MODE = 313,
    RETROK_COMPOSE = 314,
    RETROK_HELP = 315,
    RETROK_PRINT = 316,
    RETROK_SYSREQ = 317,
    RETROK_BREAK = 318,
    RETROK_LAST,
    RETROK_DUMMY = INT_MAX
};

enum retro_mod {
    RETROKMOD_NONE = 0x0000,
    RETROKMOD_SHIFT = 0x01,
    RETROKMOD_CTRL = 0x02,
    RETROKMOD_ALT = 0x04,
    RETROKMOD_META = 0x08,
    RETROKMOD_NUMLOCK = 0x10,
    RETROKMOD_CAPSLOCK = 0x20,
    RETROKMOD_SCROLLOCK = 0x40,
    RETROKMOD_DUMMY = INT_MAX
};

enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565 = 2,
    RETRO_PIXEL_FORMAT_UNKNOWN = INT_MAX
};

enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = INT_MAX
};

struct retro_variable {
    const char* key;
    const char* value;
};

struct retro_core_option_value {
    const char* value;
    const char* label;
};

struct retro_core_option_definition {
    const char* key;
    const char* desc;
    const char* info;
    struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char* default_value;
};

struct retro_core_option_v2_category {
    const char* key;
    const char* desc;
    const char* info;
};

struct retro_core_option_v2_definition {
    const char* key;
    const char* desc;
    const char* desc_categorized;
    const char* info;
    const char* info_categorized;
    const char* category_key;
    struct retro_core_option_value values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
    const char* default_value;
};

struct retro_core_options_v2 {
    struct retro_core_option_v2_category* categories;
    struct retro_core_option_v2_definition* definitions;
};

struct retro_core_options_intl {
    struct retro_core_option_definition* us;
    struct retro_core_option_definition* local;
};

struct retro_core_options_v2_intl {
    struct retro_core_options_v2* us;
    struct retro_core_options_v2* local;
};

struct retro_game_info {
    const char* path;
    const void* data;
    size_t size;
    const char* meta;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_system_info {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

typedef void (RETRO_CALLCONV* retro_keyboard_event_t)(
    bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers);

struct retro_keyboard_callback {
    retro_keyboard_event_t callback;
};

typedef bool (RETRO_CALLCONV* retro_set_eject_state_t)(bool ejected);
typedef bool (RETRO_CALLCONV* retro_get_eject_state_t)(void);
typedef unsigned (RETRO_CALLCONV* retro_get_image_index_t)(void);
typedef bool (RETRO_CALLCONV* retro_set_image_index_t)(unsigned index);
typedef unsigned (RETRO_CALLCONV* retro_get_num_images_t)(void);
typedef bool (RETRO_CALLCONV* retro_replace_image_index_t)(unsigned index, const struct retro_game_info* info);
typedef bool (RETRO_CALLCONV* retro_add_image_index_t)(void);
typedef bool (RETRO_CALLCONV* retro_set_initial_image_t)(unsigned index, const char* path);
typedef bool (RETRO_CALLCONV* retro_get_image_path_t)(unsigned index, char* s, size_t len);
typedef bool (RETRO_CALLCONV* retro_get_image_label_t)(unsigned index, char* s, size_t len);

struct retro_disk_control_callback {
    retro_set_eject_state_t set_eject_state;
    retro_get_eject_state_t get_eject_state;
    retro_get_image_index_t get_image_index;
    retro_set_image_index_t set_image_index;
    retro_get_num_images_t get_num_images;
    retro_replace_image_index_t replace_image_index;
    retro_add_image_index_t add_image_index;
};

struct retro_disk_control_ext_callback {
    retro_set_eject_state_t set_eject_state;
    retro_get_eject_state_t get_eject_state;
    retro_get_image_index_t get_image_index;
    retro_set_image_index_t set_image_index;
    retro_get_num_images_t get_num_images;
    retro_replace_image_index_t replace_image_index;
    retro_add_image_index_t add_image_index;
    retro_set_initial_image_t set_initial_image;
    retro_get_image_path_t get_image_path;
    retro_get_image_label_t get_image_label;
};

typedef void (RETRO_CALLCONV* retro_log_printf_t)(enum retro_log_level level, const char* fmt, ...);

struct retro_log_callback {
    retro_log_printf_t log;
};

typedef void (RETRO_CALLCONV* retro_set_led_state_t)(int led, int state);

struct retro_led_interface {
    retro_set_led_state_t set_led_state;
};

struct retro_fastforwarding_override {
    float ratio;
    bool fastforward;
    bool notification;
    bool inhibit_toggle;
};

struct retro_message {
    const char* msg;
    unsigned frames;
};

struct retro_message_ext {
    const char* msg;
    unsigned duration;
    unsigned priority;
    enum retro_log_level level;
    int target;
    int type;
    int8_t progress;
};

typedef void (RETRO_CALLCONV* retro_proc_address_t)(void);
typedef void (RETRO_CALLCONV* retro_hw_context_reset_t)(void);
typedef uintptr_t (RETRO_CALLCONV* retro_hw_get_current_framebuffer_t)(void);
typedef retro_proc_address_t (RETRO_CALLCONV* retro_hw_get_proc_address_t)(const char* sym);

enum retro_hw_context_type {
    RETRO_HW_CONTEXT_NONE = 0,
    RETRO_HW_CONTEXT_OPENGL = 1,
    RETRO_HW_CONTEXT_OPENGLES2 = 2,
    RETRO_HW_CONTEXT_OPENGL_CORE = 3,
    RETRO_HW_CONTEXT_OPENGLES3 = 4,
    RETRO_HW_CONTEXT_OPENGLES_VERSION = 5,
    RETRO_HW_CONTEXT_VULKAN = 6,
    RETRO_HW_CONTEXT_D3D11 = 7,
    RETRO_HW_CONTEXT_D3D10 = 8,
    RETRO_HW_CONTEXT_D3D12 = 9,
    RETRO_HW_CONTEXT_D3D9 = 10,
    RETRO_HW_CONTEXT_DUMMY = 0x7fffffff
};

struct retro_hw_render_callback {
    enum retro_hw_context_type context_type;
    retro_hw_context_reset_t context_reset;
    retro_hw_get_current_framebuffer_t get_current_framebuffer;
    retro_hw_get_proc_address_t get_proc_address;
    bool depth;
    bool stencil;
    bool bottom_left_origin;
    unsigned version_major;
    unsigned version_minor;
    bool cache_context;
    retro_hw_context_reset_t context_destroy;
    bool debug_context;
};

typedef bool (RETRO_CALLCONV* retro_environment_t)(unsigned cmd, void* data);
typedef void (RETRO_CALLCONV* retro_video_refresh_t)(const void* data, unsigned width, unsigned height, size_t pitch);
typedef void (RETRO_CALLCONV* retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (RETRO_CALLCONV* retro_audio_sample_batch_t)(const int16_t* data, size_t frames);
typedef void (RETRO_CALLCONV* retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV* retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#ifdef __cplusplus
}
#endif
