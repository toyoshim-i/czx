/*
 * czx bridge connecting NP2kai's MPU-PC98II / Super MPU-98 emulation to the
 * libretro frontend MIDI interface (RETRO_ENVIRONMENT_GET_MIDI_INTERFACE),
 * which drives czx's background-threaded munt Roland MT-32 / CM-32L synth.
 *
 * Linked into np2kai_libretro.so via:
 *   -Wl,--wrap=pccore_reset -Wl,--wrap=commng_create
 */

#include <compiler.h>
#include <pccore.h>
#include <commng.h>
#include <np2.h>
#include <libretro.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern retro_environment_t environ_cb;

extern void __real_pccore_reset(void);
extern COMMNG __real_commng_create(UINT device, BOOL onReset);

static struct retro_midi_interface g_midi_if;
static int g_midi_queried = 0;
static int g_midi_available = 0;

static void czx_midi_ensure_queried(void) {
    if (g_midi_queried) {
        return;
    }
    if (!environ_cb) {
        return;
    }
    g_midi_queried = 1;
    memset(&g_midi_if, 0, sizeof(g_midi_if));
    if (environ_cb(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE, &g_midi_if) &&
        g_midi_if.output_enabled != NULL &&
        g_midi_if.write != NULL) {
        g_midi_available = 1;
    }
}

static int czx_midi_output_active(void) {
    czx_midi_ensure_queried();
    return g_midi_available && g_midi_if.output_enabled && g_midi_if.output_enabled();
}

static UINT czx_midi_read(COMMNG self, UINT8 *data) {
    (void)self;
    (void)data;
    return 0;
}

static UINT czx_midi_write(COMMNG self, UINT8 data) {
    (void)self;
    if (czx_midi_output_active()) {
        g_midi_if.write(data, 0);
    }
    return 1;
}

static UINT czx_midi_writeretry(COMMNG self) {
    (void)self;
    return 1;
}

static void czx_midi_block_noop(COMMNG self) {
    (void)self;
}

static UINT czx_midi_lastwritesuccess(COMMNG self) {
    (void)self;
    return 1;
}

static UINT8 czx_midi_getstat(COMMNG self) {
    (void)self;
    return 0;
}

static INTPTR czx_midi_msg(COMMNG self, UINT msg, INTPTR param) {
    (void)self;
    (void)param;
    if (msg == COMMSG_MIDIRESET) {
        if (g_midi_available && g_midi_if.flush) {
            g_midi_if.flush();
        }
    }
    return 0;
}

static void czx_midi_release(COMMNG self) {
    (void)self;
}

static _COMMNG g_czx_midi_commng = {
    COMCONNECT_MIDI,
    czx_midi_read,
    czx_midi_write,
    czx_midi_writeretry,
    czx_midi_block_noop,
    czx_midi_block_noop,
    czx_midi_lastwritesuccess,
    czx_midi_getstat,
    czx_midi_msg,
    czx_midi_release,
    0,
    0,
    0,
};

void __wrap_pccore_reset(void) {
    if (czx_midi_output_active()) {
        np2cfg.mpuenable = 1;
        if (np2cfg.mpuopt == 0) {
            np2cfg.mpuopt = 0x82; /* I/O 0xE0D0, INT 2 (IRQ 6) */
        }
        np2oscfg.mpu.port = COMPORT_MIDI;
    } else {
        np2cfg.mpuenable = 0;
    }
    __real_pccore_reset();
}

COMMNG __wrap_commng_create(UINT device, BOOL onReset) {
    int is_mpu = (device == COMCREATE_MPU98II);
#if defined(SUPPORT_SMPU98)
    if (device == COMCREATE_SMPU98_A || device == COMCREATE_SMPU98_B) {
        is_mpu = 1;
    }
#endif
    if (is_mpu && !onReset && czx_midi_output_active()) {
        return (COMMNG)&g_czx_midi_commng;
    }
    return __real_commng_create(device, onReset);
}
