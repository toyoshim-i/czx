#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CZX_CORE_API_VERSION 1u

typedef enum czx_pixel_format {
    CZX_PIXEL_RGB565 = 0,
    CZX_PIXEL_XRGB8888 = 1,
    CZX_PIXEL_ABGR8888 = 2,
} czx_pixel_format_t;

typedef struct czx_frame {
    const void* data;
    uint32_t pitch_bytes;
    uint16_t width;
    uint16_t height;
    /* Optional active crop rectangle inside width x height (0 => full frame). */
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t crop_w;
    uint16_t crop_h;
    /* Pixel aspect ratio (0 => 1:1). Example: Apple II 80-col 560x192 => 1:2. */
    uint16_t par_num;
    uint16_t par_den;
    czx_pixel_format_t format;
} czx_frame_t;

typedef enum czx_key {
    CZX_KEY_NONE = 0,
    CZX_KEY_A, CZX_KEY_B, CZX_KEY_C, CZX_KEY_D, CZX_KEY_E,
    CZX_KEY_F, CZX_KEY_G, CZX_KEY_H, CZX_KEY_I, CZX_KEY_J,
    CZX_KEY_K, CZX_KEY_L, CZX_KEY_M, CZX_KEY_N, CZX_KEY_O,
    CZX_KEY_P, CZX_KEY_Q, CZX_KEY_R, CZX_KEY_S, CZX_KEY_T,
    CZX_KEY_U, CZX_KEY_V, CZX_KEY_W, CZX_KEY_X, CZX_KEY_Y,
    CZX_KEY_Z,
    CZX_KEY_0, CZX_KEY_1, CZX_KEY_2, CZX_KEY_3, CZX_KEY_4,
    CZX_KEY_5, CZX_KEY_6, CZX_KEY_7, CZX_KEY_8, CZX_KEY_9,
    CZX_KEY_ENTER,
    CZX_KEY_ESC,
    CZX_KEY_BACKSPACE,
    CZX_KEY_TAB,
    CZX_KEY_SPACE,
    CZX_KEY_MINUS,
    CZX_KEY_EQUAL,
    CZX_KEY_LEFTBRACE,
    CZX_KEY_RIGHTBRACE,
    CZX_KEY_BACKSLASH,
    CZX_KEY_SEMICOLON,
    CZX_KEY_APOSTROPHE,
    CZX_KEY_GRAVE,
    CZX_KEY_COMMA,
    CZX_KEY_DOT,
    CZX_KEY_SLASH,
    CZX_KEY_UP,
    CZX_KEY_DOWN,
    CZX_KEY_LEFT,
    CZX_KEY_RIGHT,
    CZX_KEY_HOME,
    CZX_KEY_END,
    CZX_KEY_PAGEUP,
    CZX_KEY_PAGEDOWN,
    CZX_KEY_INSERT,
    CZX_KEY_DELETE,
    CZX_KEY_F1, CZX_KEY_F2, CZX_KEY_F3, CZX_KEY_F4,
    CZX_KEY_F5, CZX_KEY_F6, CZX_KEY_F7, CZX_KEY_F8,
    CZX_KEY_F9, CZX_KEY_F10, CZX_KEY_F11, CZX_KEY_F12,
    CZX_KEY_LSHIFT,
    CZX_KEY_RSHIFT,
    CZX_KEY_LCTRL,
    CZX_KEY_RCTRL,
    CZX_KEY_LALT,
    CZX_KEY_RALT,
    CZX_KEY_LMETA,
    CZX_KEY_RMETA,
    CZX_KEY_SYM,
    CZX_KEY_FN,
    CZX_KEY_PRINT,
    CZX_KEY_HELP,
} czx_key_t;

enum {
    CZX_MOD_SHIFT = 1u << 0,
    CZX_MOD_CTRL  = 1u << 1,
    CZX_MOD_ALT   = 1u << 2,
    CZX_MOD_SYM   = 1u << 3,
    CZX_MOD_FN    = 1u << 4,
};

enum {
    CZX_JOY_UP    = 1u << 0,
    CZX_JOY_DOWN  = 1u << 1,
    CZX_JOY_LEFT  = 1u << 2,
    CZX_JOY_RIGHT = 1u << 3,
    CZX_JOY_FIRE1 = 1u << 4,
    CZX_JOY_FIRE2 = 1u << 5,
};

typedef struct czx_key_event {
    czx_key_t key;
    bool pressed;
    bool repeat;
    uint32_t mods;
    uint32_t codepoint;
} czx_key_event_t;

typedef struct czx_host {
    void* user_data;
    void (*video_frame)(void* user_data, const czx_frame_t* frame);
    void (*audio_samples)(void* user_data, const int16_t* stereo_s16, size_t frame_count);
    void (*log)(void* user_data, int level, const char* msg);
} czx_host_t;

typedef struct czx_core_desc {
    uint32_t api_version;
    const char* id;
    const char* name;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t audio_sample_rate;
} czx_core_desc_t;

typedef struct czx_core_instance czx_core_instance_t;

typedef struct czx_core_api {
    void* api_data;
    const czx_core_desc_t* (*get_desc)(const struct czx_core_api* api);
    czx_core_instance_t* (*create)(const struct czx_core_api* api, const czx_host_t* host);
    void (*destroy)(czx_core_instance_t* inst);
    void (*reset)(czx_core_instance_t* inst, bool hard);
    bool (*load_media)(czx_core_instance_t* inst, const char* path);
    bool (*eject_media)(czx_core_instance_t* inst);
    void (*run_frame)(czx_core_instance_t* inst);
    void (*on_key)(czx_core_instance_t* inst, const czx_key_event_t* ev);
    void (*on_joystick)(czx_core_instance_t* inst, uint32_t port, uint32_t buttons);
    bool (*set_option)(czx_core_instance_t* inst, const char* key, const char* value);
    uint32_t (*get_preset_count)(const czx_core_instance_t* inst);
    const char* (*get_preset_name)(const czx_core_instance_t* inst, uint32_t index);
    bool (*select_preset)(czx_core_instance_t* inst, uint32_t index);
    const char* (*get_extensions)(const czx_core_instance_t* inst);
    const char* (*get_media_label)(const czx_core_instance_t* inst);
    void (*set_data_dir)(czx_core_instance_t* inst, const char* data_dir);
    const char* (*get_last_error)(const struct czx_core_api* api);
} czx_core_api_t;

#ifdef __cplusplus
}
#endif
