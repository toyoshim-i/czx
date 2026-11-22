#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CZX_G800_MODEL_G850 = 0,
    CZX_G800_MODEL_G815 = 1,
    CZX_G800_MODEL_E200 = 2,
};

typedef struct czx_g800_lcd_snapshot {
    uint8_t vram[166 * 9];
    int machine;
    int cell_width;
    int cell_height;
    int lcd_cols;
    int lcd_rows;
    int lcd_width;
    int lcd_height;
    int vram_rows;
    int vram_width;
    uint8_t lcd_top;
    uint8_t lcd_disabled;
    uint8_t lcd_contrast;
    int lcd_reverse;
    int lcd_black;
    int lcd_white;
    int lcd_dark;
    int use_rom;
    int sio_mode;
    int cpu_halted;
    uint32_t status_bits;
} czx_g800_lcd_snapshot_t;

/* Host callbacks implemented in src/core/g800_core.cpp */
void czx_g800_host_on_frame(const czx_g800_lcd_snapshot_t* lcd, const uint8_t* sound_buf, int sound_len);
int czx_g800_host_wait_next_frame(void);
int czx_g800_host_pop_key(uint16_t* gkey_out, int* pressed_out);
int czx_g800_host_pop_media(char* path_out, int path_cap);
void czx_g800_host_log(int level, const char* msg);

/* Control entry points implemented in cmake/g800_czx/g800_wrap.c */
int czx_g800_session_run(int model,
                         const char* rom_dir,
                         const char* ram_path_sim,
                         const char* ram_path_rom,
                         const char* basic_path,
                         const char* initial_media,
                         int cold_boot);
uint16_t czx_g800_map_key(int czx_key, uint32_t mods, uint32_t codepoint);
uint16_t czx_g800_map_joy_key(int joy_bit);

#ifdef __cplusplus
}
#endif
