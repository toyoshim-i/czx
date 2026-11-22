#include <dirent.h>
#include <libretro.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#define handle_input czx_unused_upstream_handle_input
#define retro_init czx_unused_upstream_retro_init
#define retro_load_game czx_unused_upstream_retro_load_game
#define retro_run czx_unused_upstream_retro_run
#define retro_set_environment czx_unused_upstream_retro_set_environment
#include "LIBRETRO/libretro.c"
#undef retro_set_environment
#undef retro_run
#undef retro_load_game
#undef retro_init
#undef handle_input

#include "file-op.h"
#include "menu.h"

#define CZX_FONT_SZ (8 * 256)

static bool czx_pad_state[2][6];
static bool czx_mouse_btn[3];
static bool czx_drive_ejected[2];
static char czx_pending_disk[2][OSD_MAX_FILENAME];
static bool czx_has_pending_disk[2];

static int czx_parse_drive_option(void)
{
    struct retro_variable var;
    var.key = "q88_drive";
    var.value = NULL;
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "DRIVE2") == 0 ||
            strcmp(var.value, "2") == 0 ||
            strcmp(var.value, "1") == 0)
        {
            return DRIVE_2;
        }
    }
    return DRIVE_1;
}

static bool RETRO_CALLCONV czx_disk_set_eject_state(bool ejected)
{
    int drv = czx_parse_drive_option();
    czx_drive_ejected[drv] = ejected;
    if (ejected)
    {
        quasi88_disk_eject(drv);
        return true;
    }
    if (czx_has_pending_disk[drv])
    {
        czx_has_pending_disk[drv] = false;
        return quasi88_disk_insert(drv, czx_pending_disk[drv], 0, 0) != FALSE;
    }
    return true;
}

static bool RETRO_CALLCONV czx_disk_get_eject_state(void)
{
    return czx_drive_ejected[czx_parse_drive_option()];
}

static unsigned RETRO_CALLCONV czx_disk_get_image_index(void)
{
    return 0;
}

static bool RETRO_CALLCONV czx_disk_set_image_index(unsigned index)
{
    (void)index;
    return true;
}

static unsigned RETRO_CALLCONV czx_disk_get_num_images(void)
{
    return 1;
}

static bool RETRO_CALLCONV czx_disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
    int drv;
    (void)index;
    drv = czx_parse_drive_option();
    if (!info || !info->path || info->path[0] == '\0')
    {
        czx_has_pending_disk[drv] = false;
        czx_pending_disk[drv][0] = '\0';
        return true;
    }
    if (strlen(info->path) >= sizeof(czx_pending_disk[drv]))
        return false;
    strlcpy(czx_pending_disk[drv], info->path, sizeof(czx_pending_disk[drv]));
    czx_has_pending_disk[drv] = true;
    return true;
}

static bool RETRO_CALLCONV czx_disk_add_image_index(void)
{
    return true;
}

void retro_set_environment(retro_environment_t cb)
{
    struct retro_disk_control_callback disk_cb;

    czx_unused_upstream_retro_set_environment(cb);
    memset(&disk_cb, 0, sizeof(disk_cb));
    disk_cb.set_eject_state = czx_disk_set_eject_state;
    disk_cb.get_eject_state = czx_disk_get_eject_state;
    disk_cb.get_image_index = czx_disk_get_image_index;
    disk_cb.set_image_index = czx_disk_set_image_index;
    disk_cb.get_num_images = czx_disk_get_num_images;
    disk_cb.replace_image_index = czx_disk_replace_image_index;
    disk_cb.add_image_index = czx_disk_add_image_index;
    cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_cb);
}

static bool czx_find_file_in_dir(const char *dir, const char *target, char *out, size_t out_sz)
{
    DIR *dp;
    struct dirent *de;

    if (!dir || dir[0] == '\0' || !target || target[0] == '\0')
        return false;

    snprintf(out, out_sz, "%s%c%s", dir, SLASH, target);
    if (osd_file_stat(out) == FILE_STAT_FILE)
        return true;

    dp = opendir(dir);
    if (!dp)
        return false;

    while ((de = readdir(dp)) != NULL)
    {
        if (strcasecmp(de->d_name, target) == 0)
        {
            snprintf(out, out_sz, "%s%c%s", dir, SLASH, de->d_name);
            if (osd_file_stat(out) == FILE_STAT_FILE)
            {
                closedir(dp);
                return true;
            }
        }
    }
    closedir(dp);
    return false;
}

static int64_t czx_load_rom_path(const char *path, byte *rom_data, uint32_t max_size)
{
    RFILE *file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    int64_t got;

    if (!file)
        return -1;

    got = filestream_read(file, rom_data, max_size);
    filestream_close(file);
    if (got < 0)
    {
        memset(rom_data, 0xff, max_size);
        return -1;
    }

    if ((uint32_t)got < max_size)
        memset(&rom_data[got], 0xff, max_size - (size_t)got);

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "[QUASI88]: Loaded %s (0x%08X)\n", path, (unsigned)got);

    return got;
}

static int czx_collect_rom_dirs(char q88_dir[OSD_MAX_FILENAME], const char *dirs[3])
{
    int count = 0;

    if (!string_is_empty(system_dir))
    {
        size_t slen = strlen(system_dir);
        if (slen + 9 < OSD_MAX_FILENAME)
        {
            snprintf(q88_dir, OSD_MAX_FILENAME, "%.*s%cquasi88",
                     (int)slen, system_dir, SLASH);
            dirs[count++] = q88_dir;
        }
        dirs[count++] = system_dir;
    }
    if (!string_is_empty(download_dir) &&
        (string_is_empty(system_dir) || strcmp(download_dir, system_dir) != 0))
    {
        dirs[count++] = download_dir;
    }
    return count;
}

static int64_t czx_load_system_file(uint8_t bios_index, byte *rom_data, uint32_t max_size)
{
    char q88_dir[OSD_MAX_FILENAME];
    const char *dirs[3];
    int dir_count;
    uint8_t i;
    int d;

    if (!rom_data || max_size == 0)
        return -1;

    dir_count = czx_collect_rom_dirs(q88_dir, dirs);

    for (d = 0; d < dir_count; d++)
    {
        for (i = 0; i < 4; i++)
        {
            char candidate[OSD_MAX_FILENAME];
            const char *fname = bios_filenames[bios_index][i];

            if (string_is_empty(fname))
                continue;
            if (czx_find_file_in_dir(dirs[d], fname, candidate, sizeof(candidate)))
            {
                int64_t got = czx_load_rom_path(candidate, rom_data, max_size);
                if (got >= 0)
                    return got;
            }
        }
    }

    return -1;
}

static bool czx_read_compat_chunk(RFILE *fp, int64_t offset, byte *dst, uint32_t size)
{
    int64_t got;

    if (filestream_seek(fp, offset, RETRO_VFS_SEEK_POSITION_START) < 0)
    {
        memset(dst, 0xff, size);
        return false;
    }
    got = filestream_read(fp, dst, size);
    if (got < 0)
    {
        memset(dst, 0xff, size);
        return false;
    }
    if ((uint32_t)got < size)
        memset(&dst[got], 0xff, size - (size_t)got);
    return (uint32_t)got == size;
}

static bool czx_try_load_compat_rom(void)
{
    static const char *const names[2] = { "PC88.ROM", "pc88.rom" };
    char q88_dir[OSD_MAX_FILENAME];
    const char *dirs[3];
    int dir_count = czx_collect_rom_dirs(q88_dir, dirs);
    int d;
    int n;

    for (d = 0; d < dir_count; d++)
    {
        for (n = 0; n < 2; n++)
        {
            char path[OSD_MAX_FILENAME];
            RFILE *fp;
            int64_t fsz;
            bool ok;

            if (!czx_find_file_in_dir(dirs[d], names[n], path, sizeof(path)))
                continue;

            fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
            if (!fp)
                continue;

            fsz = filestream_get_size(fp);
            if (fsz < 0x16000)
            {
                filestream_close(fp);
                continue;
            }

            ok = czx_read_compat_chunk(fp, 0x00000, main_rom, 0x8000) &&
                 czx_read_compat_chunk(fp, 0x0C000, main_rom_ext[0], 0x2000) &&
                 czx_read_compat_chunk(fp, 0x0E000, main_rom_ext[1], 0x2000) &&
                 czx_read_compat_chunk(fp, 0x10000, main_rom_ext[2], 0x2000) &&
                 czx_read_compat_chunk(fp, 0x12000, main_rom_ext[3], 0x2000) &&
                 czx_read_compat_chunk(fp, 0x08000, &main_rom_n[0x6000], 0x2000) &&
                 czx_read_compat_chunk(fp, 0x14000, sub_romram, 0x2000);
            if (ok)
                (void)czx_read_compat_chunk(fp, 0x16000, main_rom_n, 0x6000);

            filestream_close(fp);
            if (ok)
            {
                if (log_cb)
                    log_cb(RETRO_LOG_INFO, "[QUASI88]: Loaded combined %s (0x%llX)\n",
                           path, (unsigned long long)fsz);
                return true;
            }
        }
    }

    return false;
}

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
    char *dir = NULL;
    struct retro_rumble_interface rumble;
    int64_t n88_sz;
    int64_t sub_sz;
    int64_t font1_sz;
    int64_t font2_sz;
    int64_t font3_sz;
    bool compat_loaded = false;

    struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left (Keypad 4)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up (Keypad 8)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down (Keypad 2)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right (Keypad 6)" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "X Key" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Z Key" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Left Shift" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Space Key" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Return Key" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "I Key" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Change drive 1 disk" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Change drive 2 disk" },

        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D Key" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "R Key" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "F Key" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "G Key" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Tab Key" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Q Key" },

        { 0 },
    };

    if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble))
        rumble_cb = rumble.set_rumble_state;

    if (!environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb))
        log_cb = NULL;

    dir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "[QUASI88]: Couldn't find system dir\n");
    }
    else
    {
        snprintf(system_dir, sizeof(system_dir), "%s", dir);
    }

    dir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY, &dir) || !dir)
    {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "[QUASI88]: Couldn't find download dir\n");
    }
    else
    {
        snprintf(download_dir, sizeof(download_dir), "%s", dir);
    }

    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_path) && !string_is_empty(save_path))
        save_to_disk_image = false;
    else
        save_to_disk_image = true;

    (void)osd_file_config_init();
    if (!string_is_empty(system_dir))
    {
        char q88_rom_dir[OSD_MAX_FILENAME];
        size_t slen = strlen(system_dir);
        if (slen + 9 < sizeof(q88_rom_dir))
        {
            snprintf(q88_rom_dir, sizeof(q88_rom_dir), "%.*s%cquasi88",
                     (int)slen, system_dir, SLASH);
            osd_set_dir_rom(osd_file_stat(q88_rom_dir) == FILE_STAT_DIR ? q88_rom_dir : system_dir);
        }
        else
        {
            osd_set_dir_rom(system_dir);
        }
        osd_set_dir_disk(system_dir);
    }
    if (save_path && !string_is_empty(save_path))
    {
        osd_set_dir_state(save_path);
        osd_set_dir_snap(save_path);
    }

    file_coding = 2;
    has_kanji_rom = FALSE;
    font_loaded = 0;

    memory_allocate();

    n88_sz = czx_load_system_file(N88_ROM, main_rom, 0x08000);
    if (n88_sz <= 0 && czx_try_load_compat_rom())
        compat_loaded = true;

    if (!compat_loaded)
    {
        if (n88_sz <= 0)
            memcpy(main_rom, &pbios_n88, 0x08000);

        sub_sz = czx_load_system_file(SUB_ROM, sub_romram, 0x02000);
        if (sub_sz <= 0)
        {
            memcpy(sub_romram, &pbios_disk, 0x00800);
            sub_sz = 0x00800;
        }
        if (sub_sz <= 0x00800)
        {
            memcpy(&sub_romram[0x0800], &sub_romram[0x0000], 0x0800);
            memcpy(&sub_romram[0x1000], &sub_romram[0x0000], 0x1000);
        }

        czx_load_system_file(EXT0_ROM, main_rom_ext[0], 0x02000);
        czx_load_system_file(EXT1_ROM, main_rom_ext[1], 0x02000);
        czx_load_system_file(EXT2_ROM, main_rom_ext[2], 0x02000);
        czx_load_system_file(EXT3_ROM, main_rom_ext[3], 0x02000);
        czx_load_system_file(N_ROM, main_rom_n, 0x08000);
    }

    /* Sub-CPU ROM mirror (0x0000..0x1FFF -> 0x2000..0x3FFF) and Sub RAM (0x4000..0x7FFF) */
    memcpy(&sub_romram[0x2000], &sub_romram[0x0000], 0x2000);
    memset(&sub_romram[0x4000], 0xff, 0x4000);

    rom_version = ROM_VERSION;

    if (czx_load_system_file(KNJ1_ROM, kanji_rom[0][0], 0x20000) == 0x20000)
    {
        has_kanji_rom = TRUE;
        menu_lang = MENU_JAPAN;
    }
    else
    {
        has_kanji_rom = FALSE;
        menu_lang = MENU_ENGLISH;
    }
    czx_load_system_file(KNJ2_ROM, kanji_rom[1][0], 0x20000);

    if (jisho_rom != NULL)
        czx_load_system_file(JISHO_ROM, jisho_rom[0], 0x80000);

    /* Font 1 */
    font1_sz = czx_load_system_file(FONT_ROM, font_mem, CZX_FONT_SZ);
    if (font1_sz == CZX_FONT_SZ)
    {
        font_loaded |= 1;
    }
    else if (has_kanji_rom)
    {
        font_loaded |= 1;
        memcpy(&font_mem[0], &kanji_rom[0][(1 << 11)][0], CZX_FONT_SZ);
    }
    else
    {
        memcpy(&font_mem[0], &built_in_font_ANK[0], CZX_FONT_SZ);
    }
    memcpy(&font_mem[CZX_FONT_SZ], &built_in_font_graph[0], CZX_FONT_SZ);

    /* Font 2 */
    font2_sz = czx_load_system_file(FONT2_ROM, font_mem2, CZX_FONT_SZ * 2);
    if (font2_sz == CZX_FONT_SZ * 2)
    {
        font_loaded |= 2;
    }
    else
    {
        if (font2_sz == CZX_FONT_SZ)
            font_loaded |= 2;
        else
            memcpy(&font_mem2[0], &built_in_font_ANH[0], CZX_FONT_SZ);
        memcpy(&font_mem2[CZX_FONT_SZ], &built_in_font_graph[0], CZX_FONT_SZ);
    }

    /* Font 3 */
    font3_sz = czx_load_system_file(FONT3_ROM, font_mem3, CZX_FONT_SZ * 2);
    if (font3_sz == CZX_FONT_SZ * 2)
    {
        font_loaded |= 4;
    }
    else if (font3_sz == CZX_FONT_SZ)
    {
        font_loaded |= 4;
        memcpy(&font_mem3[CZX_FONT_SZ], &built_in_font_graph[0], CZX_FONT_SZ);
    }
    else
    {
        memset(&font_mem3[0], 0, CZX_FONT_SZ * 2);
    }

    memset(&font_mem[0], 0, 8);
    memset(&font_mem2[0], 0, 8);
    memset(&font_mem3[0], 0, 8);
    memory_reset_font();

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
    key_buffer = (bool *)calloc(KEY88_END, sizeof(bool));
    pad_buffer = (bool *)calloc(KEY88_END, sizeof(bool));
    init_variables();
    retro_disks_init();

    memset(czx_pad_state, 0, sizeof(czx_pad_state));
    memset(czx_mouse_btn, 0, sizeof(czx_mouse_btn));
    memset(czx_drive_ejected, 0, sizeof(czx_drive_ejected));
    memset(czx_pending_disk, 0, sizeof(czx_pending_disk));
    memset(czx_has_pending_disk, 0, sizeof(czx_has_pending_disk));
    mouse_mode = MOUSE_JOYSTICK;
    use_swcursor = TRUE;
}

bool retro_load_game(const struct retro_game_info *info)
{
    init_variables();
    quasi88_start();
    quasi88_disk_eject_all();
    memset(czx_drive_ejected, 0, sizeof(czx_drive_ejected));
    memset(czx_pending_disk, 0, sizeof(czx_pending_disk));
    memset(czx_has_pending_disk, 0, sizeof(czx_has_pending_disk));

    tape_path[0] = '\0';
    autotype_count = 0;
    brace_basic = -1;
    brace_clock = 0;
    if (info && !string_is_empty(info->path))
    {
        char dir[OSD_MAX_FILENAME];
        char file[OSD_MAX_FILENAME];
        const char *ext = path_get_extension(info->path);

        if (osd_path_split(info->path, dir, file, sizeof(dir)) && dir[0] != '\0')
            osd_set_dir_disk(dir);

        if (ext && string_is_equal_noncase(ext, "m3u"))
        {
            if (!load_m3u(info->path) && log_cb)
                log_cb(RETRO_LOG_ERROR, "[QUASI88]: Failed to load playlist: %s\n", info->path);
        }
        else if (is_tape(info->path))
        {
            uint64_t quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE;

            environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
            strlcpy(tape_path, info->path, sizeof(tape_path));
            autotype_plan(info->path, true);
            quasi88_load_tape_insert(tape_path);
        }
        else
        {
            autotype_plan(info->path, false);
            retro_disks_append(info->path);
            if (!quasi88_disk_insert(DRIVE_1, info->path, 0, 0) && log_cb)
                log_cb(RETRO_LOG_ERROR, "[QUASI88]: Failed to insert disk: %s\n", info->path);
        }
    }
    apply_braces();
    if (autotype_count > 0)
        autotype_start();
    quasi88_reset(NULL);
    quasi88_exec();

    return true;
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
