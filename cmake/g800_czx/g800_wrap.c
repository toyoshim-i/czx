/*
 * czx bridge & SDL-free platform layer for SHARP PC-G800/G815/E200 emulator g800.
 * Wraps unmodified upstream third_party/g800/g800.c.
 */

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "czx/core.h"
#include "g800_bridge.h"

#define main g800_unused_main
#include "g800.c"
#undef main

static jmp_buf g_abort_jmp;
static int g_jmp_active = 0;
static uint8 g_default_base[sizeof(base)];
static int g_default_base_saved = 0;
static int g_updatekey_calls = 0;
static int g_phys_shift_held = 0;
static int g_lcd_dirty = 0;
static int g_rom_frames = 0;
static int g_blank_warned = 0;
static int g_halt_logged = 0;

static void g800_logf(int level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    czx_g800_host_log(level, buf);
}

static FILE* fopen_ci(const char* path, const char* mode) {
    FILE* fp;
    const char* slash;
    char dir_buf[PATH_MAX];
    const char* base_name;
    DIR* dir;
    struct dirent* ent;

    if (path == NULL || path[0] == '\0' || mode == NULL) {
        return NULL;
    }
    fp = fopen(path, mode);
    if (fp != NULL || mode[0] != 'r') {
        return fp;
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        snprintf(dir_buf, sizeof(dir_buf), ".");
        base_name = path;
    } else if (slash == path) {
        snprintf(dir_buf, sizeof(dir_buf), "/");
        base_name = slash + 1;
    } else {
        size_t dlen = (size_t)(slash - path);
        if (dlen >= sizeof(dir_buf)) {
            dlen = sizeof(dir_buf) - 1;
        }
        memcpy(dir_buf, path, dlen);
        dir_buf[dlen] = '\0';
        base_name = slash + 1;
    }
    if (base_name[0] == '\0') {
        return NULL;
    }

    dir = opendir(dir_buf);
    if (dir == NULL) {
        return NULL;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (strcasecmp(ent->d_name, base_name) == 0) {
            char resolved[PATH_MAX];
            if (strcmp(dir_buf, "/") == 0) {
                snprintf(resolved, sizeof(resolved), "/%s", ent->d_name);
            } else {
                snprintf(resolved, sizeof(resolved), "%s/%s", dir_buf, ent->d_name);
            }
            closedir(dir);
            return fopen(resolved, mode);
        }
    }
    closedir(dir);
    return NULL;
}

int (stricmp)(const char* a, const char* b) {
    return strcasecmp(a, b);
}

int strnicmp(const char* a, const char* b, size_t n) {
    return strncasecmp(a, b, n);
}

int memicmp(const void* a, const void* b, size_t n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = tolower(pa[i]);
        int cb = tolower(pb[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static void build_lcd_snapshot(czx_g800_lcd_snapshot_t* out) {
    const Machineinfo* m = machineInfo[machine];
    int status;
    int top_row;
    int shift;

    memset(out, 0, sizeof(*out));
    memcpy(out->vram, vram, sizeof(out->vram));
    out->machine = machine;
    out->cell_width = cellWidth;
    out->cell_height = cellHeight;
    out->lcd_cols = lcdCols;
    out->lcd_rows = lcdRows;
    out->lcd_width = lcdWidth;
    out->lcd_height = lcdHeight;
    out->vram_rows = vramRows;
    out->vram_width = vramWidth;
    out->lcd_top = lcdTop;
    out->lcd_disabled = lcdDisabled;
    out->lcd_contrast = lcdContrast;
    out->lcd_reverse = lcdEffectReverse;
    out->lcd_black = lcdEffectBlack;
    out->lcd_white = lcdEffectWhite;
    out->lcd_dark = lcdEffectDark;
    out->use_rom = useROM;
    out->sio_mode = sioMode;
    out->cpu_halted = isoff() ? 1 : 0;

    top_row = lcdTop / 8;
    shift = lcdTop % 8;
    for (status = STATUS_FIRST; status <= STATUS_LAST; status++) {
        int r0 = (m->status_row[status] + top_row) % 8;
        int r1 = (r0 + 1) % 8;
        int col_off = lcdCols * cellWidth;
        uint8 pat = (uint8)((vram[r0 * vramWidth + col_off] >> shift) |
                            (shift ? (vram[r1 * vramWidth + col_off] << (8 - shift)) : 0));
        if (pat & m->status_mask[status]) {
            out->status_bits |= (1u << status);
        }
    }
}

static void yield_frame(void) {
    czx_g800_lcd_snapshot_t snap;
    build_lcd_snapshot(&snap);
    if (useROM && !g_blank_warned) {
        g_rom_frames++;
        if (!snap.lcd_disabled) {
            int vram_bytes = snap.vram_rows * snap.vram_width;
            int k;
            if (vram_bytes > (int)sizeof(snap.vram)) {
                vram_bytes = (int)sizeof(snap.vram);
            }
            for (k = 0; k < vram_bytes; k++) {
                if (snap.vram[k] != 0) {
                    g_blank_warned = 1;
                    break;
                }
            }
        }
        if (!g_blank_warned && g_rom_frames >= 120) {
            g_blank_warned = 1;
            g800_logf(2,
                      "g800: ROM LCD still blank after 120 frames (PC=0x%04x SP=0x%04x bank=%d lcdDisabled=%d contrast=%d halt=%d iff=%d)",
                      z80.r16.pc,
                      z80.r16.sp,
                      romBank,
                      (int)lcdDisabled,
                      (int)lcdContrast,
                      (int)z80.r.halt,
                      (int)z80.r.iff);
        }
    }
    czx_g800_host_on_frame(&snap, soundReadBuffer, soundBufferSize);
    g_updatekey_calls = 0;
    g_lcd_dirty = 0;
    if (!czx_g800_host_wait_next_frame()) {
        if (g_jmp_active) {
            longjmp(g_abort_jmp, 1);
        }
    }
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_hex_byte(FILE* fp) {
    int c0 = fgetc(fp);
    int c1 = fgetc(fp);
    int d0, d1;
    if (c0 == EOF || c1 == EOF) return -1;
    d0 = hex_digit(c0);
    d1 = hex_digit(c1);
    if (d0 < 0 || d1 < 0) return -1;
    return (d0 << 4) | d1;
}

static int parse_ihex(FILE* fp, uint8* mem, int* ret_top, int mem_size, int check, int rel_base) {
    int top = 0x10000;
    int max_end = 0;
    int any = 0;

    for (;;) {
        int ch = fgetc(fp);
        int len, off_h, off_l, off, rec, sum, chk, i;
        if (ch == EOF || ch == 0x1a) break;
        if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
        if (ch != ':') return -1;

        if ((len = read_hex_byte(fp)) < 0) return -1;
        if ((off_h = read_hex_byte(fp)) < 0) return -1;
        if ((off_l = read_hex_byte(fp)) < 0) return -1;
        if ((rec = read_hex_byte(fp)) < 0) return -1;

        off = (off_h << 8) | off_l;
        sum = len + off_h + off_l + rec;

        if (rec == 1) {
            (void)read_hex_byte(fp);
            break;
        }
        if (rec != 0) {
            return -1;
        }
        if (len == 0) {
            (void)read_hex_byte(fp);
            break;
        }

        for (i = 0; i < len; i++) {
            int val = read_hex_byte(fp);
            int dst = off + i - rel_base;
            if (val < 0) return -1;
            sum += val;
            if (mem != NULL && dst >= 0 && dst < mem_size) {
                mem[dst] = (uint8)val;
            }
        }
        if ((chk = read_hex_byte(fp)) < 0) return -1;
        sum = (~sum + 1) & 0xff;
        if (check && chk != sum) return -1;

        if (!any || off < top) top = off;
        if (off + len > max_end) max_end = off + len;
        any = 1;
    }

    if (ret_top != NULL) *ret_top = any ? top : 0;
    return any ? (max_end - top) : 0;
}

int readBin(const char* path, void* mem, int mem_size) {
    FILE* fp;
    long sz;
    size_t got;

    if (path == NULL || path[0] == '\0') return -1;
    fp = fopen_ci(path, "rb");
    if (fp == NULL) return -1;

    if (mem_size <= 0) {
        fclose(fp);
        return 0;
    }
    if (mem != NULL) {
        got = fread(mem, 1, (size_t)mem_size, fp);
        fclose(fp);
        return (int)got;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = ftell(fp);
    fclose(fp);
    if (sz < 0) return -1;
    return (sz < mem_size) ? (int)sz : mem_size;
}

int writeBin(const char* path, const void* mem, int size) {
    FILE* fp;
    size_t put = 0;

    if (path == NULL || path[0] == '\0') return -1;
    fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    if (mem != NULL && size > 0) {
        put = fwrite(mem, 1, (size_t)size, fp);
    }
    fclose(fp);
    return (int)put;
}

int readHex(const char* path, void* mem, int* off, int mem_size, int check) {
    FILE* fp;
    int sz;

    if (path == NULL || path[0] == '\0') return -1;
    fp = fopen_ci(path, "r");
    if (fp == NULL) return -1;
    sz = parse_ihex(fp, (uint8*)mem, off, mem_size, check, 0);
    fclose(fp);
    return sz;
}

int readHexAbs(const char* path, void* mem, int* off, int mem_size, int check) {
    FILE* fp;
    int top = 0;
    int sz;

    if (path == NULL || path[0] == '\0') return -1;
    fp = fopen_ci(path, "r");
    if (fp == NULL) return -1;
    sz = parse_ihex(fp, NULL, &top, 0x10000, check, 0);
    if (sz >= 0 && mem != NULL) {
        rewind(fp);
        sz = parse_ihex(fp, (uint8*)mem, NULL, mem_size, check, top);
    }
    fclose(fp);
    if (off != NULL) *off = top;
    return sz;
}

static int save_ihex(const char* path, const uint8* mem, int off, int size, int abs_addr) {
    FILE* fp;
    int pos = 0;
    const uint8* r;

    if (path == NULL || path[0] == '\0' || mem == NULL || size < 0) return -1;
    fp = fopen(path, "w");
    if (fp == NULL) return -1;

    r = mem + off;
    while (pos < size) {
        int len = (size - pos > 16) ? 16 : (size - pos);
        int addr = abs_addr ? (off + pos) : pos;
        int sum = len + ((addr >> 8) & 0xff) + (addr & 0xff);
        int i;
        fprintf(fp, ":%02X%04X00", len, addr & 0xffff);
        for (i = 0; i < len; i++) {
            fprintf(fp, "%02X", r[pos + i]);
            sum += r[pos + i];
        }
        fprintf(fp, "%02X\n", (~sum + 1) & 0xff);
        pos += len;
    }
    fprintf(fp, ":00000001FF\n");
    fclose(fp);
    return size;
}

int writeHex(const char* path, const void* mem, int off, int size) {
    return save_ihex(path, (const uint8*)mem, off, size, 1);
}

int writeHexAbs(const char* path, const void* mem, int off, int size) {
    return save_ihex(path, (const uint8*)mem, off, size, 0);
}

int copyFile(const char* src, const char* dst) {
    uint8 buf[0x8000];
    int size = readBin(src, buf, (int)sizeof(buf));
    if (size < 0) return FALSE;
    return writeBin(dst, buf, size) >= 0;
}

int removeFile(const char* path) {
    if (path == NULL || path[0] == '\0') return -1;
    if (access(path, F_OK) != 0) return 0;
    return unlink(path);
}

int cmpFile(const char* path1, const char* path2) {
    uint8 buf1[0x8000];
    uint8 buf2[0x8000];
    int s1 = readBin(path1, buf1, (int)sizeof(buf1));
    int s2 = readBin(path2, buf2, (int)sizeof(buf2));
    if (s1 < 0 || s2 < 0 || s1 != s2) return FALSE;
    return memcmp(buf1, buf2, (size_t)s1) == 0;
}

int updateLCD(void) {
    g_lcd_dirty = 1;
    return TRUE;
}

void updateLCDContrast(void) {
    g_lcd_dirty = 1;
}

void updateLayout(void) {
    const Machineinfo* m = machineInfo[machine];
    cellWidth = m->cell_width;
    cellHeight = m->cell_height;
    lcdCols = m->lcd_cols;
    lcdRows = m->lcd_rows;
    vramCols = m->vram_cols;
    vramRows = m->vram_rows;
    lcdWidth = cellWidth * lcdCols;
    lcdHeight = 8 * lcdRows;
    vramWidth = m->vram_width;
    vramHeight = m->vram_height;
    statesKeyStrobeClear = m->states_keystrobe_clear;
}

void updateSerial(void) {
}

int initDepend(void) {
    updateLayout();
    return 0;
}

int delay(int interval) {
    (void)interval;
    yield_frame();
    return FALSE;
}

static uint8 pressKey(uint16 raw_k) {
    uint8 k = (uint8)(raw_k & 0xff);
    switch (k) {
        case GKEY_NONE:
            return 0;
        case GKEY_BREAK:
            keyBreak |= 0x80;
            return INTERRUPT_KON;
        case GKEY_SHIFT:
            keyShift |= 0x01;
            return INTERRUPT_IA;
        case GKEY_RESET:
            keyReset = TRUE;
            return 0;
        case GKEY_MENU:
        case GKEY_DEBUG:
            return 0;
        default:
            k = (uint8)(k - 1);
            if ((size_t)(k / 8) < sizeof(keyMatrix)) {
                keyMatrix[k / 8] |= (uint8)(1u << (k % 8));
            }
            return INTERRUPT_IA;
    }
}

static void releaseKey(uint16 raw_k) {
    uint8 k = (uint8)(raw_k & 0xff);
    switch (k) {
        case GKEY_NONE:
            break;
        case GKEY_BREAK:
            keyBreak &= (uint8)~0x80u;
            break;
        case GKEY_SHIFT:
            keyShift &= (uint8)~0x01u;
            break;
        case GKEY_RESET:
            keyReset = FALSE;
            break;
        case GKEY_MENU:
            if (emulatorMode != EMULATOR_MODE_MENU) {
                emulatorMode = EMULATOR_MODE_MENU;
                menu();
                emulatorMode = EMULATOR_MODE_RUN;
            }
            break;
        default:
            k = (uint8)(k - 1);
            if ((size_t)(k / 8) < sizeof(keyMatrix)) {
                keyMatrix[k / 8] &= (uint8)~(1u << (k % 8));
            }
            break;
    }
}

static int has_ext_ci(const char* path, const char* ext) {
    size_t lp = strlen(path);
    size_t le = strlen(ext);
    size_t i;
    if (lp < le) return 0;
    for (i = 0; i < le; i++) {
        if (tolower((unsigned char)path[lp - le + i]) != tolower((unsigned char)ext[i])) {
            return 0;
        }
    }
    return 1;
}

static void handle_media_request(const char* path) {
    if (path == NULL || path[0] == '\0') return;
    if (useROM) {
        sioLoad(path);
        return;
    }
    if (has_ext_ci(path, ".ihx") || has_ext_ci(path, ".hex")) {
        if (readHex(path, memory, NULL, 0x8000, FALSE) >= 0) {
            storeRAM(pathRAM);
        }
        return;
    }
    strncpy(pathSioIn, path, sizeof(pathSioIn) - 1);
    pathSioIn[sizeof(pathSioIn) - 1] = '\0';
    if (inportBas(path)) {
        if (memory[0x7902] == 0x40) {
            setAutoKeyText("CLOAD\n", FALSE);
        }
    }
}

uint8 updateKey(void) {
    static uint8 autoKey = GKEY_NONE;
    static int autoKeyCount = 0;
    char media_req[1024];
    uint16 gkey = GKEY_NONE;
    int pressed = 0;
    uint8 itype = 0;

    if (++g_updatekey_calls >= 32) {
        yield_frame();
    }

    if (czx_g800_host_pop_media(media_req, (int)sizeof(media_req))) {
        handle_media_request(media_req);
    }

    while (czx_g800_host_pop_key(&gkey, &pressed)) {
        uint8 code = (uint8)(gkey & 0xff);
        if (pressed) {
            if (code == GKEY_SHIFT) {
                g_phys_shift_held = 1;
            }
            itype |= pressKey(gkey);
            if ((gkey & GMODKEY_NOSHIFT) && (keyShift & 0x01)) {
                releaseKey(GKEY_SHIFT);
            } else if ((gkey & GMODKEY_SHIFT) && !(keyShift & 0x01)) {
                itype |= pressKey(GKEY_SHIFT);
            }
        } else {
            if (code == GKEY_SHIFT) {
                g_phys_shift_held = 0;
            }
            releaseKey(gkey);
            if ((gkey & GMODKEY_NOSHIFT) && g_phys_shift_held && !(keyShift & 0x01)) {
                itype |= pressKey(GKEY_SHIFT);
            } else if ((gkey & GMODKEY_SHIFT) && !g_phys_shift_held && (keyShift & 0x01)) {
                releaseKey(GKEY_SHIFT);
            }
        }
    }

    if (autoKeyCount == 0) {
        if ((autoKey = getAutoKey()) != GKEY_NONE) {
            itype |= pressKey(autoKey);
            autoKeyCount++;
        }
    } else if (autoKeyCount == freqUpdateIO / 20) {
        releaseKey(autoKey);
        autoKeyCount++;
    } else if (autoKeyCount >= 2 * freqUpdateIO / 20) {
        autoKeyCount = 0;
    } else {
        autoKeyCount++;
    }

    return itype;
}

uint16_t czx_g800_map_key(int czx_key, uint32_t mods, uint32_t codepoint) {
    /* First map exact printable ASCII punctuation/digits from codepoint so SYM/Shift layouts match. */
    if (codepoint >= 0x20u && codepoint <= 0x7eu) {
        switch (codepoint) {
            case ' ':  return GMODKEY_NOSHIFT | GKEY_SPACE;
            case '!':  return GMODKEY_SHIFT   | GKEY_Q;
            case '"':  return GMODKEY_SHIFT   | GKEY_W;
            case '#':  return GMODKEY_SHIFT   | GKEY_E;
            case '$':  return GMODKEY_SHIFT   | GKEY_R;
            case '%':  return GMODKEY_SHIFT   | GKEY_T;
            case '&':  return GMODKEY_SHIFT   | GKEY_Y;
            case '\'': return GMODKEY_SHIFT   | GKEY_U;
            case '[':  return GMODKEY_SHIFT   | GKEY_A;
            case ']':  return GMODKEY_SHIFT   | GKEY_S;
            case '{':  return GMODKEY_SHIFT   | GKEY_D;
            case '}':  return GMODKEY_SHIFT   | GKEY_F;
            case '\\': return GMODKEY_SHIFT   | GKEY_G;
            case '|':  return GMODKEY_SHIFT   | GKEY_H;
            case '~':  return GMODKEY_SHIFT   | GKEY_J;
            case '_':  return GMODKEY_SHIFT   | GKEY_K;
            case '?':  return GMODKEY_SHIFT   | GKEY_COMMA;
            case ':':  return GMODKEY_SHIFT   | GKEY_SEMICOLON;
            case '<':  return GMODKEY_SHIFT   | GKEY_I;
            case '>':  return GMODKEY_SHIFT   | GKEY_O;
            case '@':  return GMODKEY_SHIFT   | GKEY_P;
            case '(':  return GMODKEY_NOSHIFT | GKEY_LKAKKO;
            case ')':  return GMODKEY_NOSHIFT | GKEY_RKAKKO;
            case '^':  return GMODKEY_NOSHIFT | GKEY_HAT;
            case '+':  return GMODKEY_NOSHIFT | GKEY_PLUS;
            case '-':  return GMODKEY_NOSHIFT | GKEY_MINUS;
            case '*':  return GMODKEY_NOSHIFT | GKEY_ASTER;
            case '/':  return GMODKEY_NOSHIFT | GKEY_SLASH;
            case '=':  return GMODKEY_NOSHIFT | GKEY_EQUAL;
            case ',':  return GMODKEY_NOSHIFT | GKEY_COMMA;
            case '.':  return GMODKEY_NOSHIFT | GKEY_PERIOD;
            case ';':  return GMODKEY_NOSHIFT | GKEY_SEMICOLON;
            case '0':  return GMODKEY_NOSHIFT | GKEY_0;
            case '1':  return GMODKEY_NOSHIFT | GKEY_1;
            case '2':  return GMODKEY_NOSHIFT | GKEY_2;
            case '3':  return GMODKEY_NOSHIFT | GKEY_3;
            case '4':  return GMODKEY_NOSHIFT | GKEY_4;
            case '5':  return GMODKEY_NOSHIFT | GKEY_5;
            case '6':  return GMODKEY_NOSHIFT | GKEY_6;
            case '7':  return GMODKEY_NOSHIFT | GKEY_7;
            case '8':  return GMODKEY_NOSHIFT | GKEY_8;
            case '9':  return GMODKEY_NOSHIFT | GKEY_9;
            default: break;
        }
    }

    switch ((czx_key_t)czx_key) {
        case CZX_KEY_A: return GKEY_A;
        case CZX_KEY_B: return GKEY_B;
        case CZX_KEY_C: return GKEY_C;
        case CZX_KEY_D: return GKEY_D;
        case CZX_KEY_E: return GKEY_E;
        case CZX_KEY_F: return GKEY_F;
        case CZX_KEY_G: return GKEY_G;
        case CZX_KEY_H: return GKEY_H;
        case CZX_KEY_I: return GKEY_I;
        case CZX_KEY_J: return GKEY_J;
        case CZX_KEY_K: return GKEY_K;
        case CZX_KEY_L: return GKEY_L;
        case CZX_KEY_M: return GKEY_M;
        case CZX_KEY_N: return GKEY_N;
        case CZX_KEY_O: return GKEY_O;
        case CZX_KEY_P: return GKEY_P;
        case CZX_KEY_Q: return GKEY_Q;
        case CZX_KEY_R: return GKEY_R;
        case CZX_KEY_S: return GKEY_S;
        case CZX_KEY_T: return GKEY_T;
        case CZX_KEY_U: return GKEY_U;
        case CZX_KEY_V: return GKEY_V;
        case CZX_KEY_W: return GKEY_W;
        case CZX_KEY_X: return GKEY_X;
        case CZX_KEY_Y: return GKEY_Y;
        case CZX_KEY_Z: return GKEY_Z;
        case CZX_KEY_0: return GKEY_0;
        case CZX_KEY_1: return GKEY_1;
        case CZX_KEY_2: return GKEY_2;
        case CZX_KEY_3: return GKEY_3;
        case CZX_KEY_4: return GKEY_4;
        case CZX_KEY_5: return GKEY_5;
        case CZX_KEY_6: return GKEY_6;
        case CZX_KEY_7: return GKEY_7;
        case CZX_KEY_8: return GKEY_8;
        case CZX_KEY_9: return GKEY_9;
        case CZX_KEY_ENTER:      return GKEY_RETURN;
        case CZX_KEY_ESC:        return (mods & CZX_MOD_SHIFT) ? GKEY_OFF : GKEY_BREAK;
        case CZX_KEY_BACKSPACE:  return GKEY_BACKSPACE;
        case CZX_KEY_DELETE:     return GKEY_BACKSPACE;
        case CZX_KEY_TAB:        return GKEY_TAB;
        case CZX_KEY_SPACE:      return GKEY_SPACE;
        case CZX_KEY_MINUS:      return GKEY_MINUS;
        case CZX_KEY_EQUAL:      return GKEY_EQUAL;
        case CZX_KEY_LEFTBRACE:  return GKEY_LKAKKO;
        case CZX_KEY_RIGHTBRACE: return GKEY_RKAKKO;
        case CZX_KEY_BACKSLASH:  return GMODKEY_SHIFT | GKEY_G;
        case CZX_KEY_SEMICOLON:  return GKEY_SEMICOLON;
        case CZX_KEY_APOSTROPHE: return GKEY_ASTER;
        case CZX_KEY_COMMA:      return GKEY_COMMA;
        case CZX_KEY_DOT:        return GKEY_PERIOD;
        case CZX_KEY_SLASH:      return GKEY_SLASH;
        case CZX_KEY_UP:         return GKEY_UP;
        case CZX_KEY_DOWN:       return GKEY_DOWN;
        case CZX_KEY_LEFT:       return GKEY_LEFT;
        case CZX_KEY_RIGHT:      return GKEY_RIGHT;
        case CZX_KEY_HOME:       return GKEY_CLS;
        case CZX_KEY_INSERT:     return GKEY_INSERT;
        case CZX_KEY_LSHIFT:
        case CZX_KEY_RSHIFT:     return GKEY_SHIFT;
        case CZX_KEY_F1:         return GKEY_BASIC;
        case CZX_KEY_F2:         return GKEY_TEXT;
        case CZX_KEY_F3:         return GKEY_2NDF;
        case CZX_KEY_F4:         return GKEY_CLS;
        case CZX_KEY_F5:         return GKEY_CAPS;
        case CZX_KEY_F6:         return GKEY_KANA;
        case CZX_KEY_F7:         return GKEY_CONST;
        case CZX_KEY_F8:         return GKEY_ANS;
        case CZX_KEY_F9:         return GKEY_OFF;
        case CZX_KEY_F10:        return GKEY_MENU;
        default:                 return GKEY_NONE;
    }
}

uint16_t czx_g800_map_joy_key(int joy_bit) {
    switch (joy_bit) {
        case CZX_JOY_UP:    return GKEY_UP;
        case CZX_JOY_DOWN:  return GKEY_DOWN;
        case CZX_JOY_LEFT:  return GKEY_LEFT;
        case CZX_JOY_RIGHT: return GKEY_RIGHT;
        case CZX_JOY_FIRE1: return GKEY_SPACE;
        case CZX_JOY_FIRE2: return GKEY_RETURN;
        default:            return GKEY_NONE;
    }
}

static int dir_has_g800_rom(const char* dir, const char** reason_out) {
    uint8 probe[0x40];
    if (dir == NULL || dir[0] == '\0') {
        if (reason_out) *reason_out = "empty rom_dir";
        return FALSE;
    }
    if (loadROM1page(probe, dir, -1, 0x40) < 0x40) {
        if (reason_out) *reason_out = "no base.bin/base.txt";
        return FALSE;
    }
    if (loadROM1page(NULL, dir, 0, 0x4000) != 0x4000) {
        if (reason_out) *reason_out = "base found, missing or invalid 16KiB rom00.bin/rom00.txt";
        return FALSE;
    }
    if (reason_out) *reason_out = "ok";
    return TRUE;
}

int czx_g800_session_run(int model,
                         const char* rom_dir,
                         const char* ram_path_sim,
                         const char* ram_path_rom,
                         const char* basic_path,
                         const char* initial_media,
                         int cold_boot) {
    const Machineinfo* m;
    const char* model_name = "PC-G850";
    const char* rom_reason = "no base.bin/base.txt";
    const char* selected_ram = NULL;
    const char* ram_status = "fresh";
    int ram_loaded = FALSE;

    if (!g_default_base_saved) {
        memcpy(g_default_base, base, sizeof(base));
        g_default_base_saved = 1;
    } else {
        memcpy(base, g_default_base, sizeof(base));
    }

    if (rom != NULL) {
        free(rom);
        rom = NULL;
    }
    if (soundReadBuffer != NULL) {
        free(soundReadBuffer);
        soundReadBuffer = NULL;
        soundWriteBuffer = NULL;
    }
    if (bas.prog != NULL) {
        free(bas.prog);
    }
    if (bas.vars != NULL) {
        free(bas.vars);
    }
    memset(&bas, 0, sizeof(bas));
    memset(memory, 0, sizeof(memory));
    memset(&z80, 0, sizeof(z80));

    switch (model) {
        case CZX_G800_MODEL_G815:
            machine = MACHINE_G815;
            machineSub = MACHINE_SUB_PCG815;
            model_name = "PC-G815";
            break;
        case CZX_G800_MODEL_E200:
            machine = MACHINE_E200;
            machineSub = MACHINE_SUB_PCE200;
            model_name = "PC-E200";
            break;
        case CZX_G800_MODEL_G850:
        default:
            machine = MACHINE_G850;
            machineSub = MACHINE_SUB_PCG850V;
            model_name = "PC-G850";
            break;
    }

    m = machineInfo[machine];
    freqCPU = m->cpu_clocks;
    freqUpdateIO = 60;
    noWait = FALSE;
    useBasic = TRUE;
    romBanks = 0;
    romBank = 0;
    exBank = 0;
    ramBank = 0;
    ioReset = 1;
    onCd = 0;
    m1Wait = 0;
    ioWait = 0;
    csClk = 0;
    interruptType = 0;
    interruptMask = 0;
    timer = 0;
    timerCount = 0;
    lcdScales = 2;
    lcdRead = 0;
    lcdX = 0;
    lcdY = 0;
    lcdX2 = 0;
    lcdY2 = 0;
    lcdMod = 0;
    lcdTrim = 0;
    lcdTop = 0;
    lcdBegin = 0;
    lcdDisabled = 0;
    lcdContrast = 0x0f;
    lcdEffectMirror = 0;
    lcdEffectBlack = 0;
    lcdEffectReverse = 0;
    lcdEffectDark = 0;
    lcdEffectWhite = 0;
    pin11If = PIN11IF_3IO;
    pin11In = 0;
    xinEnabled = 0;
    io3Out = 0;
    pio8Io = 0;
    pio8Out = 0;
    uartIo = 0;
    uartMode = 0;
    uartCommand = 0;
    uartStatus = 0;
    battChk = 0;
    emulatorMode = EMULATOR_MODE_RUN;
    sioMode = SIO_MODE_STOP;
    sioBufferSize = 0;
    sioCount = 0;
    sioBusy = 0;
    pathSioIn[0] = '\0';
    pathSioOut[0] = '\0';
    memset(vram, 0, sizeof(vram));
    memset(keyMatrix, 0, sizeof(keyMatrix));
    keyBreak = 0;
    keyShift = 0;
    keyReset = FALSE;
    g_phys_shift_held = 0;
    g_updatekey_calls = 0;
    g_rom_frames = 0;
    g_blank_warned = 0;
    g_halt_logged = 0;
    setAutoKeyText("", FALSE);

    updateLayout();

    if (basic_path != NULL) {
        strncpy(pathBasic, basic_path, sizeof(pathBasic) - 1);
        pathBasic[sizeof(pathBasic) - 1] = '\0';
    } else {
        pathBasic[0] = '\0';
    }

    buzzer = BUZZER_USE;
    soundBufferSize = FREQ_SOUND / 60;
    soundReadBuffer = (uint8*)calloc(1, (size_t)soundBufferSize * 2u);
    soundWriteBuffer = soundReadBuffer + soundBufferSize;
    soundPlayed = TRUE;

    if (dir_has_g800_rom(rom_dir, &rom_reason)) {
        useROM = TRUE;
        emulateIOCS = FALSE;
        strncpy(dirROM, rom_dir, sizeof(dirROM) - 1);
        dirROM[sizeof(dirROM) - 1] = '\0';
    } else {
        useROM = FALSE;
        emulateIOCS = TRUE;
        dirROM[0] = '\0';
    }

    if (!loadROM(dirROM)) {
        useROM = FALSE;
        emulateIOCS = TRUE;
        rom_reason = "loadROM failed";
        dirROM[0] = '\0';
        (void)loadROM(dirROM);
    }

    selected_ram = useROM ? ram_path_rom : ram_path_sim;
    if (selected_ram != NULL) {
        strncpy(pathRAM, selected_ram, sizeof(pathRAM) - 1);
        pathRAM[sizeof(pathRAM) - 1] = '\0';
    } else {
        pathRAM[0] = '\0';
    }

    if (!cold_boot) {
        ram_loaded = loadRAM(pathRAM);
        ram_status = ram_loaded ? "loaded" : "fresh";
    } else {
        ram_status = "cold-boot";
        if (!useROM) {
            memory[0x7901] = 0x02;
            memory[0x7903] = 0x60;
        }
    }

    if (useROM) {
        g800_logf(1,
                  "g800: model=%s mode=ROM dir=%s banks=%d ram=%s (%s)",
                  model_name,
                  dirROM,
                  romBanks,
                  pathRAM,
                  ram_status);
    } else {
        g800_logf(1,
                  "g800: model=%s mode=SIM dir=%s (%s) ram=%s (%s)",
                  model_name,
                  rom_dir ? rom_dir : "",
                  rom_reason,
                  pathRAM,
                  ram_status);
    }

    z80.m = memory;
    z80.i.trace = FALSE;
    z80.i.emulate_subroutine = emulateIOCS;
    z80srand((uint32)time(NULL));

    boot();

    if (initial_media != NULL && initial_media[0] != '\0') {
        handle_media_request(initial_media);
    }

    g_jmp_active = 1;
    if (setjmp(g_abort_jmp) == 0) {
        if (useROM) {
            exec(FALSE);
        } else {
            execSim();
        }
        /* Keep pumping LCD frames if pocket computer entered software power-off state. */
        while (g_jmp_active) {
            if (useROM && !g_halt_logged) {
                g_halt_logged = 1;
                g800_logf(1,
                          "g800: CPU entered power-off/HALT (PC=0x%04x SP=0x%04x bank=%d lcdDisabled=%d iff=%d ioReset=%d); press ESC (ON/BRK) to wake",
                          z80.r16.pc,
                          z80.r16.sp,
                          romBank,
                          (int)lcdDisabled,
                          (int)z80.r.iff,
                          (int)ioReset);
            }
            updateKey();
            if (keyReset || (keyBreak & 0x80)) {
                keyReset = FALSE;
                if (useROM) {
                    g800_logf(1, "g800: waking CPU from power-off/HALT");
                    g_halt_logged = 0;
                }
                boot();
                if (useROM) {
                    exec(FALSE);
                } else {
                    execSim();
                }
                continue;
            }
            yield_frame();
        }
    }
    g_jmp_active = 0;

    storeRAM(pathRAM);

    if (rom != NULL) {
        free(rom);
        rom = NULL;
    }
    if (soundReadBuffer != NULL) {
        free(soundReadBuffer);
        soundReadBuffer = NULL;
        soundWriteBuffer = NULL;
    }
    if (bas.prog != NULL) {
        free(bas.prog);
        bas.prog = NULL;
    }
    if (bas.vars != NULL) {
        free(bas.vars);
        bas.vars = NULL;
    }
    return 0;
}
