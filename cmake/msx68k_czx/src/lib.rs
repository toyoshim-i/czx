#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code)]

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_uint, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Instant;

use msx68k_bus::{CpuModel, CpuSpeedMode, Crtc, FrameBudget, VmConfig, X68kVm};
use msx68k_vicon::{
    FrameTimingStats, PixelFormat, RenderedFrame565, RendererConfig, ShaderGeneration,
    ViconCompositor,
};

const RETRO_API_VERSION: c_uint = 1;

const RETRO_DEVICE_MASK: c_uint = 0xFF;
const RETRO_DEVICE_JOYPAD: c_uint = 1;
const RETRO_DEVICE_MOUSE: c_uint = 2;

const RETRO_DEVICE_ID_JOYPAD_B: c_uint = 0;
const RETRO_DEVICE_ID_JOYPAD_UP: c_uint = 4;
const RETRO_DEVICE_ID_JOYPAD_DOWN: c_uint = 5;
const RETRO_DEVICE_ID_JOYPAD_LEFT: c_uint = 6;
const RETRO_DEVICE_ID_JOYPAD_RIGHT: c_uint = 7;
const RETRO_DEVICE_ID_JOYPAD_A: c_uint = 8;
const RETRO_DEVICE_ID_JOYPAD_MASK: c_uint = 256;

const RETRO_DEVICE_ID_MOUSE_X: c_uint = 0;
const RETRO_DEVICE_ID_MOUSE_Y: c_uint = 1;
const RETRO_DEVICE_ID_MOUSE_LEFT: c_uint = 2;
const RETRO_DEVICE_ID_MOUSE_RIGHT: c_uint = 3;

const RETRO_REGION_NTSC: c_uint = 0;

const RETRO_PIXEL_FORMAT_RGB565: c_uint = 2;

const RETRO_LOG_INFO: c_uint = 1;
const RETRO_LOG_WARN: c_uint = 2;
const RETRO_LOG_ERROR: c_uint = 3;

const RETRO_ENVIRONMENT_EXPERIMENTAL: c_uint = 0x10000;
const RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: c_uint = 9;
const RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: c_uint = 10;
const RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: c_uint = 12;
const RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: c_uint = 13;
const RETRO_ENVIRONMENT_SET_HW_RENDER: c_uint = 14;
const RETRO_ENVIRONMENT_GET_VARIABLE: c_uint = 15;
const RETRO_ENVIRONMENT_SET_VARIABLES: c_uint = 16;
const RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: c_uint = 17;
const RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: c_uint = 18;
const RETRO_ENVIRONMENT_GET_LOG_INTERFACE: c_uint = 27;
const RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: c_uint = 31;
const RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: c_uint = 32;
const RETRO_ENVIRONMENT_SET_GEOMETRY: c_uint = 37;
const RETRO_ENVIRONMENT_GET_LED_INTERFACE: c_uint = 46 | RETRO_ENVIRONMENT_EXPERIMENTAL;

const RETROK_BACKSPACE: u32 = 8;
const RETROK_TAB: u32 = 9;
const RETROK_RETURN: u32 = 13;
const RETROK_PAUSE: u32 = 19;
const RETROK_ESCAPE: u32 = 27;
const RETROK_SPACE: u32 = 32;
const RETROK_QUOTE: u32 = 39;
const RETROK_COMMA: u32 = 44;
const RETROK_MINUS: u32 = 45;
const RETROK_PERIOD: u32 = 46;
const RETROK_SLASH: u32 = 47;
const RETROK_0: u32 = 48;
const RETROK_1: u32 = 49;
const RETROK_2: u32 = 50;
const RETROK_3: u32 = 51;
const RETROK_4: u32 = 52;
const RETROK_5: u32 = 53;
const RETROK_6: u32 = 54;
const RETROK_7: u32 = 55;
const RETROK_8: u32 = 56;
const RETROK_9: u32 = 57;
const RETROK_SEMICOLON: u32 = 59;
const RETROK_EQUALS: u32 = 61;
const RETROK_LEFTBRACKET: u32 = 91;
const RETROK_BACKSLASH: u32 = 92;
const RETROK_RIGHTBRACKET: u32 = 93;
const RETROK_BACKQUOTE: u32 = 96;
const RETROK_a: u32 = 97;
const RETROK_b: u32 = 98;
const RETROK_c: u32 = 99;
const RETROK_d: u32 = 100;
const RETROK_e: u32 = 101;
const RETROK_f: u32 = 102;
const RETROK_g: u32 = 103;
const RETROK_h: u32 = 104;
const RETROK_i: u32 = 105;
const RETROK_j: u32 = 106;
const RETROK_k: u32 = 107;
const RETROK_l: u32 = 108;
const RETROK_m: u32 = 109;
const RETROK_n: u32 = 110;
const RETROK_o: u32 = 111;
const RETROK_p: u32 = 112;
const RETROK_q: u32 = 113;
const RETROK_r: u32 = 114;
const RETROK_s: u32 = 115;
const RETROK_t: u32 = 116;
const RETROK_u: u32 = 117;
const RETROK_v: u32 = 118;
const RETROK_w: u32 = 119;
const RETROK_x: u32 = 120;
const RETROK_y: u32 = 121;
const RETROK_z: u32 = 122;
const RETROK_DELETE: u32 = 127;
const RETROK_KP0: u32 = 256;
const RETROK_KP1: u32 = 257;
const RETROK_KP2: u32 = 258;
const RETROK_KP3: u32 = 259;
const RETROK_KP4: u32 = 260;
const RETROK_KP5: u32 = 261;
const RETROK_KP6: u32 = 262;
const RETROK_KP7: u32 = 263;
const RETROK_KP8: u32 = 264;
const RETROK_KP9: u32 = 265;
const RETROK_KP_PERIOD: u32 = 266;
const RETROK_KP_DIVIDE: u32 = 267;
const RETROK_KP_MULTIPLY: u32 = 268;
const RETROK_KP_MINUS: u32 = 269;
const RETROK_KP_PLUS: u32 = 270;
const RETROK_KP_ENTER: u32 = 271;
const RETROK_KP_EQUALS: u32 = 272;
const RETROK_UP: u32 = 273;
const RETROK_DOWN: u32 = 274;
const RETROK_RIGHT: u32 = 275;
const RETROK_LEFT: u32 = 276;
const RETROK_INSERT: u32 = 277;
const RETROK_HOME: u32 = 278;
const RETROK_END: u32 = 279;
const RETROK_PAGEUP: u32 = 280;
const RETROK_PAGEDOWN: u32 = 281;
const RETROK_F1: u32 = 282;
const RETROK_F2: u32 = 283;
const RETROK_F3: u32 = 284;
const RETROK_F4: u32 = 285;
const RETROK_F5: u32 = 286;
const RETROK_F6: u32 = 287;
const RETROK_F7: u32 = 288;
const RETROK_F8: u32 = 289;
const RETROK_F9: u32 = 290;
const RETROK_F10: u32 = 291;
const RETROK_F11: u32 = 292;
const RETROK_F12: u32 = 293;
const RETROK_CAPSLOCK: u32 = 301;
const RETROK_RSHIFT: u32 = 303;
const RETROK_LSHIFT: u32 = 304;
const RETROK_RCTRL: u32 = 305;
const RETROK_LCTRL: u32 = 306;
const RETROK_RALT: u32 = 307;
const RETROK_LALT: u32 = 308;
const RETROK_HELP: u32 = 315;
const RETROK_PRINT: u32 = 316;
const RETROK_BREAK: u32 = 318;

#[repr(C)]
pub struct RetroVariable {
    pub key: *const c_char,
    pub value: *const c_char,
}

#[repr(C)]
pub struct RetroGameInfo {
    pub path: *const c_char,
    pub data: *const c_void,
    pub size: usize,
    pub meta: *const c_char,
}

#[repr(C)]
pub struct RetroGameGeometry {
    pub base_width: c_uint,
    pub base_height: c_uint,
    pub max_width: c_uint,
    pub max_height: c_uint,
    pub aspect_ratio: f32,
}

#[repr(C)]
pub struct RetroSystemTiming {
    pub fps: f64,
    pub sample_rate: f64,
}

#[repr(C)]
pub struct RetroSystemAvInfo {
    pub geometry: RetroGameGeometry,
    pub timing: RetroSystemTiming,
}

#[repr(C)]
pub struct RetroSystemInfo {
    pub library_name: *const c_char,
    pub library_version: *const c_char,
    pub valid_extensions: *const c_char,
    pub need_fullpath: bool,
    pub block_extract: bool,
}

type RetroKeyboardEventFn = unsafe extern "C" fn(bool, c_uint, u32, u16);

#[repr(C)]
pub struct RetroKeyboardCallback {
    pub callback: Option<RetroKeyboardEventFn>,
}

type RetroSetEjectStateFn = unsafe extern "C" fn(bool) -> bool;
type RetroGetEjectStateFn = unsafe extern "C" fn() -> bool;
type RetroGetImageIndexFn = unsafe extern "C" fn() -> c_uint;
type RetroSetImageIndexFn = unsafe extern "C" fn(c_uint) -> bool;
type RetroGetNumImagesFn = unsafe extern "C" fn() -> c_uint;
type RetroReplaceImageIndexFn = unsafe extern "C" fn(c_uint, *const RetroGameInfo) -> bool;
type RetroAddImageIndexFn = unsafe extern "C" fn() -> bool;

#[repr(C)]
pub struct RetroDiskControlCallback {
    pub set_eject_state: Option<RetroSetEjectStateFn>,
    pub get_eject_state: Option<RetroGetEjectStateFn>,
    pub get_image_index: Option<RetroGetImageIndexFn>,
    pub set_image_index: Option<RetroSetImageIndexFn>,
    pub get_num_images: Option<RetroGetNumImagesFn>,
    pub replace_image_index: Option<RetroReplaceImageIndexFn>,
    pub add_image_index: Option<RetroAddImageIndexFn>,
}

type RetroLogPrintfFn = unsafe extern "C" fn(c_uint, *const c_char, ...);

#[repr(C)]
pub struct RetroLogCallback {
    pub log: Option<RetroLogPrintfFn>,
}

type RetroSetLedStateFn = unsafe extern "C" fn(c_int, c_int);

#[repr(C)]
pub struct RetroLedInterface {
    pub set_led_state: Option<RetroSetLedStateFn>,
}

const RETRO_HW_CONTEXT_OPENGLES2: c_uint = 2;
const RETRO_HW_FRAME_BUFFER_VALID: *const c_void = (-1isize) as *const c_void;

type RetroHwContextResetFn = unsafe extern "C" fn();
type RetroHwGetCurrentFramebufferFn = unsafe extern "C" fn() -> usize;
type RetroHwGetProcAddressFn = msx68k_vicon::gles2::GlGetProcAddressFn;

#[repr(C)]
pub struct RetroHwRenderCallback {
    pub context_type: c_uint,
    pub context_reset: Option<RetroHwContextResetFn>,
    pub get_current_framebuffer: Option<RetroHwGetCurrentFramebufferFn>,
    pub get_proc_address: Option<RetroHwGetProcAddressFn>,
    pub depth: bool,
    pub stencil: bool,
    pub bottom_left_origin: bool,
    pub version_major: c_uint,
    pub version_minor: c_uint,
    pub cache_context: bool,
    pub context_destroy: Option<RetroHwContextResetFn>,
    pub debug_context: bool,
}

type RetroEnvironmentFn = unsafe extern "C" fn(c_uint, *mut c_void) -> bool;
type RetroVideoRefreshFn = unsafe extern "C" fn(*const c_void, c_uint, c_uint, usize);
type RetroAudioSampleFn = unsafe extern "C" fn(i16, i16);
type RetroAudioSampleBatchFn = unsafe extern "C" fn(*const i16, usize) -> usize;
type RetroInputPollFn = unsafe extern "C" fn();
type RetroInputStateFn = unsafe extern "C" fn(c_uint, c_uint, c_uint, c_uint) -> i16;

#[derive(Default)]
struct Callbacks {
    env: Option<RetroEnvironmentFn>,
    video: Option<RetroVideoRefreshFn>,
    audio_sample: Option<RetroAudioSampleFn>,
    audio_batch: Option<RetroAudioSampleBatchFn>,
    input_poll: Option<RetroInputPollFn>,
    input_state: Option<RetroInputStateFn>,
    log: Option<RetroLogPrintfFn>,
    led: Option<RetroSetLedStateFn>,
    hw_get_fbo: Option<RetroHwGetCurrentFramebufferFn>,
    hw_get_proc: Option<RetroHwGetProcAddressFn>,
}

struct CoreState {
    vm: X68kVm,
    sram_path: PathBuf,
    hdd0_path: Option<PathBuf>,
    swap_drive: usize,
    pending_floppy: [Option<Vec<u8>>; 2],
    ejected: [bool; 2],
    held_scancodes: [bool; 128],
    joy_sent: u8,
    mouse_left: bool,
    mouse_right: bool,
    cur_w: u32,
    cur_h: u32,
    cur_aspect_bits: u32,
    cur_fps: f64,
    gpu_scale: GpuScaleMode,
    pending_scaled: bool,
    need_full_vram_upload: bool,
    hw_render_enabled: bool,
    renderer_gen: ShaderGeneration,
    fd_accesses: [Arc<AtomicU64>; 4],
    fd_seen: [u64; 4],
    fd_hold: [u8; 2],
    scsi_accesses: Arc<AtomicU64>,
    scsi_seen: u64,
    scsi_hold: u8,
    last_led_mask: i32,
    frame_counter: u64,
    audio_opm: Vec<(i16, i16)>,
    audio_pcm: Vec<i16>,
    perf_log: bool,
    perf_detail: bool,
    perf_frames: u32,
    perf_cpu_ns: u64,
    perf_finish_ns: u64,
    perf_submit_ns: u64,
    perf_up_us: u64,
    perf_draw_us: u64,
    perf_rb_us: u64,
    perf_timing_samples: u32,
}

static CALLBACKS: Mutex<Callbacks> = Mutex::new(Callbacks {
    env: None,
    video: None,
    audio_sample: None,
    audio_batch: None,
    input_poll: None,
    input_state: None,
    log: None,
    led: None,
    hw_get_fbo: None,
    hw_get_proc: None,
});

static CORE_STATE: Mutex<Option<CoreState>> = Mutex::new(None);

thread_local! {
    static COMPOSITOR: RefCell<Option<ViconCompositor>> = const { RefCell::new(None) };
}

unsafe extern "C" fn hw_context_reset() {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let hw_get_proc = CALLBACKS.lock().ok().and_then(|c| c.hw_get_proc);
        let perf_detail = CORE_STATE
            .lock()
            .ok()
            .and_then(|g| g.as_ref().map(|s| s.perf_detail))
            .unwrap_or(false);

        COMPOSITOR.with(|cell| {
            if let Some(mut old) = cell.borrow_mut().take() {
                old.mark_context_lost();
            }
        });

        let ext_comp = unsafe {
            ViconCompositor::new_gles2_external(
                RendererConfig {
                    generation: ShaderGeneration::Gles2,
                    max_width: 1024,
                    max_height: 1024,
                    output_format: PixelFormat::Rgba8,
                },
                hw_get_proc,
            )
        };

        if let Some(mut comp) = ext_comp {
            comp.set_collect_timing(perf_detail);
            match comp.renderer_name() {
                Some(name) if !name.is_empty() => {
                    log_msg(RETRO_LOG_INFO, &format!("VICON renderer: HW GLES2 external ({name})"));
                }
                _ => {
                    log_msg(RETRO_LOG_INFO, "VICON renderer: HW GLES2 external");
                }
            }
            COMPOSITOR.with(|cell| {
                *cell.borrow_mut() = Some(comp);
            });
            if let Ok(mut guard) = CORE_STATE.lock() {
                if let Some(st) = guard.as_mut() {
                    st.hw_render_enabled = true;
                    st.renderer_gen = ShaderGeneration::Gles2;
                    st.need_full_vram_upload = true;
                }
            }
        } else {
            log_msg(
                RETRO_LOG_WARN,
                "VICON HW GLES2 external context unavailable, falling back to CPU compositor",
            );
            let fallback = ViconCompositor::new(RendererConfig {
                generation: ShaderGeneration::Cpu,
                max_width: 1024,
                max_height: 1024,
                output_format: PixelFormat::Rgb565,
            });
            COMPOSITOR.with(|cell| {
                *cell.borrow_mut() = Some(fallback);
            });
            if let Ok(mut guard) = CORE_STATE.lock() {
                if let Some(st) = guard.as_mut() {
                    st.hw_render_enabled = false;
                    st.renderer_gen = ShaderGeneration::Cpu;
                    st.need_full_vram_upload = false;
                }
            }
        }
    }));
}

unsafe extern "C" fn hw_context_destroy() {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        COMPOSITOR.with(|cell| {
            if let Some(mut comp) = cell.borrow_mut().take() {
                comp.destroy_gl_resources();
            }
        });
    }));
}

const DEFAULT_X68K_FPS: f64 = 10_000_000.0 / (317.0 * 568.0);

fn compute_crtc_fps(crtc: &Crtc) -> f64 {
    let r20 = crtc.reg(20);
    let scanline_cycles: f64 = if (r20 & 0x10) != 0 { 317.0 } else { 625.0 };
    let v_total_lines: f64 = ((crtc.reg(4) & 0x0FFF).max(256) as u32 + 1) as f64;
    (10_000_000.0 / (scanline_cycles * v_total_lines)).clamp(20.0, 120.0)
}

const OPM_SAMPLE_RATE: f64 = msx68k_bus::opm::OPM_SAMPLE_RATE as f64;

const LIB_NAME: &[u8] = b"msx68k (X68000)\0";
const LIB_VER: &[u8] = b"0.1.0-exp\0";
const VALID_EXTS: &[u8] = b"xdf|dim|2hd|2dd|hds|hdf|img|m3u\0";

const VAR_SPEED_K: &[u8] = b"msx68k_speed\0";
const VAR_SPEED_V: &[u8] = b"CPU Speed; 10|16|24|25|burst\0";
const VAR_CPU_K: &[u8] = b"msx68k_cpu\0";
const VAR_CPU_V: &[u8] = b"CPU Model; 68000|68030\0";
const VAR_RAM_K: &[u8] = b"msx68k_ram_mb\0";
const VAR_RAM_V: &[u8] = b"Main RAM (MB); 4|2|1|6|8|12\0";
const VAR_MODE_K: &[u8] = b"msx68k_mode\0";
const VAR_MODE_V: &[u8] = b"VM Mode; compat|sx\0";
const VAR_DRIVE_K: &[u8] = b"msx68k_drive\0";
const VAR_DRIVE_V: &[u8] = b"Floppy Swap Drive; FDD0|FDD1\0";
const VAR_RENDERER_K: &[u8] = b"msx68k_renderer\0";
const VAR_RENDERER_V: &[u8] = b"Renderer; auto|gles2|cpu\0";
const VAR_GPU_SCALE_K: &[u8] = b"msx68k_gpu_scale\0";
const VAR_GPU_SCALE_V: &[u8] = b"GPU Downscale; off|fit_par|fit_4x3|axis\0";

fn log_msg(level: c_uint, msg: &str) {
    let cb = CALLBACKS.lock().ok().and_then(|c| c.log);
    if let Some(log_fn) = cb {
        if let Ok(cstr) = std::ffi::CString::new(msg) {
            unsafe {
                log_fn(level, b"msx68k: %s\n\0".as_ptr() as *const c_char, cstr.as_ptr());
            }
            return;
        }
    }
    if level >= RETRO_LOG_WARN {
        eprintln!("[msx68k] {msg}");
    }
}

fn env_call<T>(cmd: c_uint, data: *mut T) -> bool {
    let cb = CALLBACKS.lock().ok().and_then(|c| c.env);
    match cb {
        Some(f) => unsafe { f(cmd, data as *mut c_void) },
        None => false,
    }
}

fn get_variable(key: &[u8]) -> Option<String> {
    let mut var = RetroVariable {
        key: key.as_ptr() as *const c_char,
        value: std::ptr::null(),
    };
    if env_call(RETRO_ENVIRONMENT_GET_VARIABLE, &mut var) && !var.value.is_null() {
        let s = unsafe { CStr::from_ptr(var.value) }.to_string_lossy().into_owned();
        Some(s)
    } else {
        None
    }
}

fn get_directory(cmd: c_uint) -> Option<PathBuf> {
    let mut ptr: *const c_char = std::ptr::null();
    if env_call(cmd, &mut ptr) && !ptr.is_null() {
        let s = unsafe { CStr::from_ptr(ptr) }.to_string_lossy();
        if !s.is_empty() {
            return Some(PathBuf::from(s.as_ref()));
        }
    }
    None
}

fn parse_speed_option(s: Option<&str>, cpu: CpuModel) -> CpuSpeedMode {
    match s.map(|v| v.to_ascii_lowercase()).as_deref() {
        Some("10") | Some("10mhz") => CpuSpeedMode::Mhz10,
        Some("16") | Some("16mhz") => CpuSpeedMode::Mhz16,
        Some("24") | Some("24mhz") => CpuSpeedMode::Mhz24,
        Some("25") | Some("25mhz") => CpuSpeedMode::Mhz25,
        Some("burst") | Some("0") => CpuSpeedMode::Burst,
        _ => match cpu {
            CpuModel::Mc68000 => CpuSpeedMode::Mhz10,
            CpuModel::Mc68030 => CpuSpeedMode::Mhz25,
        },
    }
}

fn parse_cpu_option(s: Option<&str>) -> CpuModel {
    match s {
        Some("68030") => CpuModel::Mc68030,
        _ => CpuModel::Mc68000,
    }
}

fn parse_ram_option(s: Option<&str>) -> Option<usize> {
    s.and_then(|v| v.parse::<usize>().ok())
        .map(|mb| mb.clamp(1, 12))
}

fn parse_drive_option() -> usize {
    match get_variable(VAR_DRIVE_K).as_deref() {
        Some("FDD1") => 1,
        _ => 0,
    }
}

fn parse_renderer_option(s: Option<&str>) -> ShaderGeneration {
    match s.map(|v| v.to_ascii_lowercase()).as_deref() {
        Some("gles2") => ShaderGeneration::Gles2,
        Some("cpu") => ShaderGeneration::Cpu,
        _ => ShaderGeneration::Auto,
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum GpuScaleMode {
    Off,
    FitPar,
    Fit4x3,
    Axis,
}

fn parse_gpu_scale_option(s: Option<&str>) -> GpuScaleMode {
    match s.map(|v| v.to_ascii_lowercase()).as_deref() {
        Some("fit_par") => GpuScaleMode::FitPar,
        Some("fit_4x3") => GpuScaleMode::Fit4x3,
        Some("axis") => GpuScaleMode::Axis,
        _ => GpuScaleMode::Off,
    }
}

fn compute_gpu_scale_target(mode: GpuScaleMode, native_w: u32, native_h: u32) -> Option<(u32, u32)> {
    const LCD_W: u32 = 320;
    const LCD_H: u32 = 170;
    match mode {
        GpuScaleMode::Off => None,
        GpuScaleMode::Axis => {
            let dw = native_w.min(LCD_W).max(1);
            let dh = native_h.min(LCD_H).max(1);
            (dw < native_w || dh < native_h).then_some((dw, dh))
        }
        GpuScaleMode::FitPar => {
            let aspect = aspect_for_resolution(native_w, native_h);
            let eff_w = if aspect > 0.0 {
                (native_h as f64) * (aspect as f64)
            } else {
                native_w as f64
            };
            let eff_h = native_h as f64;
            let scale = (1.0f64)
                .min((LCD_W as f64) / eff_w)
                .min((LCD_H as f64) / eff_h);
            let dw = (eff_w * scale).round().clamp(1.0, LCD_W as f64) as u32;
            let dh = (eff_h * scale).round().clamp(1.0, LCD_H as f64) as u32;
            (dw != native_w || dh != native_h).then_some((dw, dh))
        }
        GpuScaleMode::Fit4x3 => {
            let eff_w = (native_h as f64) * (4.0 / 3.0);
            let eff_h = native_h as f64;
            let scale = (1.0f64)
                .min((LCD_W as f64) / eff_w)
                .min((LCD_H as f64) / eff_h);
            let dw = (eff_w * scale).round().clamp(1.0, LCD_W as f64) as u32;
            let dh = (eff_h * scale).round().clamp(1.0, LCD_H as f64) as u32;
            (dw != native_w || dh != native_h).then_some((dw, dh))
        }
    }
}

fn find_file_ci(dir: &Path, filename: &str) -> Option<PathBuf> {
    let exact = dir.join(filename);
    if exact.is_file() {
        return Some(exact);
    }
    let entries = std::fs::read_dir(dir).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if let Some(name) = path.file_name().and_then(|n| n.to_str()) {
            if name.eq_ignore_ascii_case(filename) && path.is_file() {
                return Some(path);
            }
        }
    }
    None
}

fn is_hdd_ext(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_ascii_lowercase())
            .as_deref(),
        Some("hds") | Some("hdf") | Some("img")
    )
}

fn is_m3u_ext(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_ascii_lowercase())
            .as_deref(),
        Some("m3u")
    )
}

fn aspect_for_resolution(w: u32, h: u32) -> f32 {
    if w == 0 || h == 0 {
        return 4.0 / 3.0;
    }
    if w == h || w >= h.saturating_mul(2) {
        4.0 / 3.0
    } else {
        0.0
    }
}

fn map_retrok_to_x68k(keycode: u32) -> Option<u8> {
    let sc = match keycode {
        RETROK_ESCAPE => 0x01,
        RETROK_1 => 0x02,
        RETROK_2 => 0x03,
        RETROK_3 => 0x04,
        RETROK_4 => 0x05,
        RETROK_5 => 0x06,
        RETROK_6 => 0x07,
        RETROK_7 => 0x08,
        RETROK_8 => 0x09,
        RETROK_9 => 0x0A,
        RETROK_0 => 0x0B,
        RETROK_MINUS => 0x0C,
        RETROK_EQUALS => 0x0D,
        RETROK_BACKSLASH => 0x0E,
        RETROK_BACKSPACE => 0x0F,
        RETROK_TAB => 0x10,
        RETROK_q => 0x11,
        RETROK_w => 0x12,
        RETROK_e => 0x13,
        RETROK_r => 0x14,
        RETROK_t => 0x15,
        RETROK_y => 0x16,
        RETROK_u => 0x17,
        RETROK_i => 0x18,
        RETROK_o => 0x19,
        RETROK_p => 0x1A,
        RETROK_LEFTBRACKET => 0x1B,
        RETROK_RIGHTBRACKET => 0x1C,
        RETROK_RETURN => 0x1D,
        RETROK_a => 0x1E,
        RETROK_s => 0x1F,
        RETROK_d => 0x20,
        RETROK_f => 0x21,
        RETROK_g => 0x22,
        RETROK_h => 0x23,
        RETROK_j => 0x24,
        RETROK_k => 0x25,
        RETROK_l => 0x26,
        RETROK_SEMICOLON => 0x27,
        RETROK_QUOTE => 0x28,
        RETROK_BACKQUOTE => 0x29,
        RETROK_z => 0x2A,
        RETROK_x => 0x2B,
        RETROK_c => 0x2C,
        RETROK_v => 0x2D,
        RETROK_b => 0x2E,
        RETROK_n => 0x2F,
        RETROK_m => 0x30,
        RETROK_COMMA => 0x31,
        RETROK_PERIOD => 0x32,
        RETROK_SLASH => 0x33,
        RETROK_SPACE => 0x35,
        RETROK_HOME => 0x36,
        RETROK_DELETE => 0x37,
        RETROK_PAGEUP => 0x38,
        RETROK_PAGEDOWN => 0x39,
        RETROK_END | RETROK_HELP => 0x3A,
        RETROK_LEFT => 0x3B,
        RETROK_UP => 0x3C,
        RETROK_RIGHT => 0x3D,
        RETROK_DOWN => 0x3E,
        RETROK_KP_DIVIDE => 0x40,
        RETROK_KP_MULTIPLY => 0x41,
        RETROK_KP_MINUS => 0x42,
        RETROK_KP7 => 0x43,
        RETROK_KP8 => 0x44,
        RETROK_KP9 => 0x45,
        RETROK_KP_PLUS => 0x46,
        RETROK_KP4 => 0x47,
        RETROK_KP5 => 0x48,
        RETROK_KP6 => 0x49,
        RETROK_KP_EQUALS => 0x4A,
        RETROK_KP1 => 0x4B,
        RETROK_KP2 => 0x4C,
        RETROK_KP3 => 0x4D,
        RETROK_KP_ENTER => 0x4E,
        RETROK_KP0 => 0x4F,
        RETROK_KP_PERIOD => 0x51,
        RETROK_LALT => 0x55,
        RETROK_RALT => 0x56,
        RETROK_CAPSLOCK => 0x5D,
        RETROK_INSERT => 0x5E,
        RETROK_BREAK | RETROK_PAUSE => 0x61,
        RETROK_PRINT => 0x62,
        RETROK_F1 => 0x63,
        RETROK_F2 => 0x64,
        RETROK_F3 => 0x65,
        RETROK_F4 => 0x66,
        RETROK_F5 => 0x67,
        RETROK_F6 => 0x68,
        RETROK_F7 => 0x69,
        RETROK_F8 => 0x6A,
        RETROK_F9 => 0x6B,
        RETROK_F10 => 0x6C,
        RETROK_F11 => 0x57,
        RETROK_F12 => 0x58,
        RETROK_LSHIFT | RETROK_RSHIFT => 0x70,
        RETROK_LCTRL | RETROK_RCTRL => 0x71,
        _ => return None,
    };
    Some(sc)
}

impl CoreState {
    fn flush_persistent(&mut self, force: bool) {
        if self.vm.bus.take_sram_dirty() || (force && !self.sram_path.exists()) {
            if let Some(parent) = self.sram_path.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            if let Err(e) = std::fs::write(&self.sram_path, self.vm.bus.sram()) {
                log_msg(
                    RETRO_LOG_WARN,
                    &format!("Failed to write SRAM '{}': {e}", self.sram_path.display()),
                );
            }
        }
        if self.vm.bus.scsi.take_dirty() {
            if let (Some(hdd_path), Some(hdd_data)) =
                (&self.hdd0_path, self.vm.bus.scsi.hdd_data(0))
            {
                if let Err(e) = std::fs::write(hdd_path, hdd_data) {
                    log_msg(
                        RETRO_LOG_WARN,
                        &format!("Failed to write HDD0 '{}': {e}", hdd_path.display()),
                    );
                }
            }
        }
    }

    fn set_led_mask(&mut self, mask: i32) {
        if mask != self.last_led_mask {
            self.last_led_mask = mask;
            if let Some(led_fn) = CALLBACKS.lock().ok().and_then(|c| c.led) {
                unsafe {
                    led_fn(1, mask);
                }
            }
        }
    }
}

unsafe extern "C" fn on_retro_keyboard(down: bool, keycode: c_uint, _character: u32, _mods: u16) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let Some(sc) = map_retrok_to_x68k(keycode) else {
            return;
        };
        if let Ok(mut guard) = CORE_STATE.lock() {
            if let Some(st) = guard.as_mut() {
                let idx = (sc & 0x7F) as usize;
                if down {
                    if st.held_scancodes[idx] {
                        return;
                    }
                    st.held_scancodes[idx] = true;
                    st.vm.bus.mfp.push_keyboard_byte(sc);
                } else {
                    st.held_scancodes[idx] = false;
                    st.vm.bus.mfp.push_keyboard_byte(sc | 0x80);
                }
            }
        }
    }));
}

unsafe extern "C" fn disk_set_eject_state(ejected: bool) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let unit = parse_drive_option();
        let Ok(mut guard) = CORE_STATE.lock() else {
            return false;
        };
        let Some(st) = guard.as_mut() else {
            return false;
        };
        st.swap_drive = unit;
        st.ejected[unit] = ejected;
        if ejected {
            st.vm.bus.eject_floppy(unit);
            true
        } else if let Some(data) = st.pending_floppy[unit].take() {
            let ok = st.vm.bus.insert_floppy(unit, data, false);
            if !ok {
                log_msg(
                    RETRO_LOG_ERROR,
                    &format!("Unsupported floppy image size for FDD{unit}"),
                );
            }
            ok
        } else {
            true
        }
    }))
    .unwrap_or(false)
}

unsafe extern "C" fn disk_get_eject_state() -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let unit = parse_drive_option();
        CORE_STATE
            .lock()
            .ok()
            .and_then(|g| g.as_ref().map(|st| st.ejected[unit]))
            .unwrap_or(false)
    }))
    .unwrap_or(false)
}

unsafe extern "C" fn disk_get_image_index() -> c_uint {
    0
}

unsafe extern "C" fn disk_set_image_index(_index: c_uint) -> bool {
    true
}

unsafe extern "C" fn disk_get_num_images() -> c_uint {
    1
}

unsafe extern "C" fn disk_replace_image_index(_index: c_uint, info: *const RetroGameInfo) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let unit = parse_drive_option();
        let Ok(mut guard) = CORE_STATE.lock() else {
            return false;
        };
        let Some(st) = guard.as_mut() else {
            return false;
        };
        st.swap_drive = unit;
        if info.is_null() {
            st.pending_floppy[unit] = None;
            return true;
        }
        let info_ref = unsafe { &*info };
        if !info_ref.path.is_null() {
            let path_str = unsafe { CStr::from_ptr(info_ref.path) }.to_string_lossy();
            if path_str.is_empty() {
                st.pending_floppy[unit] = None;
                return true;
            }
            match std::fs::read(path_str.as_ref()) {
                Ok(data) => {
                    st.pending_floppy[unit] = Some(data);
                    return true;
                }
                Err(e) => {
                    log_msg(
                        RETRO_LOG_ERROR,
                        &format!("Failed to read floppy '{}': {e}", path_str),
                    );
                    return false;
                }
            }
        }
        if !info_ref.data.is_null() && info_ref.size > 0 {
            let slice = unsafe { std::slice::from_raw_parts(info_ref.data as *const u8, info_ref.size) };
            st.pending_floppy[unit] = Some(slice.to_vec());
            return true;
        }
        false
    }))
    .unwrap_or(false)
}

unsafe extern "C" fn disk_add_image_index() -> bool {
    true
}

fn mount_single_media(vm: &mut X68kVm, hdd0_path: &mut Option<PathBuf>, path: &Path, fd_slot: &mut usize) -> bool {
    if is_hdd_ext(path) {
        match std::fs::read(path) {
            Ok(data) => {
                if vm.bus.insert_scsi_hdd(0, data) {
                    *hdd0_path = Some(path.to_path_buf());
                    log_msg(RETRO_LOG_INFO, &format!("Mounted SCSI HDD0: {}", path.display()));
                    return true;
                }
                log_msg(RETRO_LOG_ERROR, &format!("Failed to mount SCSI HDD0: {}", path.display()));
                false
            }
            Err(e) => {
                log_msg(RETRO_LOG_ERROR, &format!("Failed to read HDD0 '{}': {e}", path.display()));
                false
            }
        }
    } else {
        if *fd_slot >= 2 {
            log_msg(RETRO_LOG_WARN, &format!("Ignoring extra floppy image: {}", path.display()));
            return true;
        }
        match std::fs::read(path) {
            Ok(data) => {
                let unit = *fd_slot;
                if vm.bus.insert_floppy(unit, data, false) {
                    log_msg(RETRO_LOG_INFO, &format!("Mounted FDD{unit}: {}", path.display()));
                    *fd_slot += 1;
                    true
                } else {
                    log_msg(
                        RETRO_LOG_ERROR,
                        &format!("Unsupported floppy image format/size for FDD{unit}: {}", path.display()),
                    );
                    false
                }
            }
            Err(e) => {
                log_msg(
                    RETRO_LOG_ERROR,
                    &format!("Failed to read floppy '{}': {e}", path.display()),
                );
                false
            }
        }
    }
}

fn load_game_inner(game: *const RetroGameInfo) -> bool {
    let mut fmt = RETRO_PIXEL_FORMAT_RGB565;
    if !env_call(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &mut fmt) {
        log_msg(RETRO_LOG_ERROR, "Frontend rejected RGB565 pixel format");
        return false;
    }

    let system_dir = get_directory(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY)
        .unwrap_or_else(|| PathBuf::from("."));
    let save_dir = get_directory(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY)
        .unwrap_or_else(|| system_dir.clone());

    let mut cpu_model = parse_cpu_option(get_variable(VAR_CPU_K).as_deref());
    let mode_sx = matches!(get_variable(VAR_MODE_K).as_deref(), Some("sx"));
    let swap_drive = parse_drive_option();

    let Some(cgrom_path) = find_file_ci(&system_dir, "CGROM.DAT") else {
        log_msg(
            RETRO_LOG_ERROR,
            &format!("Missing CGROM.DAT in {}", system_dir.display()),
        );
        return false;
    };
    let ipl_path = match cpu_model {
        CpuModel::Mc68030 => find_file_ci(&system_dir, "IPLROM30.DAT")
            .or_else(|| find_file_ci(&system_dir, "IPLROM.DAT")),
        CpuModel::Mc68000 => {
            if let Some(p) = find_file_ci(&system_dir, "IPLROM.DAT") {
                Some(p)
            } else if let Some(p30) = find_file_ci(&system_dir, "IPLROM30.DAT") {
                cpu_model = CpuModel::Mc68030;
                Some(p30)
            } else {
                None
            }
        }
    };
    let Some(ipl_path) = ipl_path else {
        log_msg(
            RETRO_LOG_ERROR,
            &format!("Missing IPLROM.DAT in {}", system_dir.display()),
        );
        return false;
    };
    let speed_mode = parse_speed_option(get_variable(VAR_SPEED_K).as_deref(), cpu_model);

    let cgrom = match std::fs::read(&cgrom_path) {
        Ok(b) => b,
        Err(e) => {
            log_msg(
                RETRO_LOG_ERROR,
                &format!("Failed to read CGROM '{}': {e}", cgrom_path.display()),
            );
            return false;
        }
    };
    let ipl = match std::fs::read(&ipl_path) {
        Ok(b) => b,
        Err(e) => {
            log_msg(
                RETRO_LOG_ERROR,
                &format!("Failed to read IPLROM '{}': {e}", ipl_path.display()),
            );
            return false;
        }
    };

    let sram_path = save_dir.join("msx68k_sram.dat");
    let saved_sram = std::fs::read(&sram_path)
        .ok()
        .filter(|d| d.len() == 16 * 1024)
        .or_else(|| {
            find_file_ci(&system_dir, "SRAM.DAT")
                .and_then(|p| std::fs::read(p).ok())
                .filter(|d| d.len() == 16 * 1024)
        });

    let opt_ram = parse_ram_option(get_variable(VAR_RAM_K).as_deref());
    let ram_mb = opt_ram.unwrap_or_else(|| match &saved_sram {
        Some(sram) => {
            let ram_end = u32::from_be_bytes([sram[8], sram[9], sram[10], sram[11]]);
            (ram_end as usize).div_ceil(1024 * 1024).clamp(1, 12)
        }
        None => 4,
    });

    let base_cfg = if mode_sx {
        VmConfig::primary_sx()
    } else {
        VmConfig::app_vm()
    };
    let mut vm = X68kVm::new(
        base_cfg
            .with_cpu_model(cpu_model)
            .with_speed_mode(speed_mode)
            .with_ram_mb(ram_mb),
    );
    vm.bus.load_cgrom(&cgrom);
    vm.bus.load_ipl_rom(&ipl);

    if let Some(sram_data) = &saved_sram {
        vm.bus.sram_mut().copy_from_slice(sram_data);
        let ram_bytes = (ram_mb * 1024 * 1024) as u32;
        vm.bus.sram_mut()[8..12].copy_from_slice(&ram_bytes.to_be_bytes());
    }

    let mut hdd0_path: Option<PathBuf> = None;
    let mut fd_slot = 0usize;

    if !game.is_null() {
        let game_ref = unsafe { &*game };
        if !game_ref.path.is_null() {
            let raw = unsafe { CStr::from_ptr(game_ref.path) }.to_string_lossy();
            if !raw.is_empty() {
                let media_path = Path::new(raw.as_ref());
                if is_m3u_ext(media_path) {
                    let content = match std::fs::read_to_string(media_path) {
                        Ok(c) => c,
                        Err(e) => {
                            log_msg(
                                RETRO_LOG_ERROR,
                                &format!("Failed to read playlist '{}': {e}", media_path.display()),
                            );
                            return false;
                        }
                    };
                    let base_dir = media_path.parent().unwrap_or_else(|| Path::new("."));
                    for line in content.lines() {
                        let trimmed = line.trim();
                        if trimmed.is_empty() || trimmed.starts_with('#') {
                            continue;
                        }
                        let entry_path = {
                            let p = Path::new(trimmed);
                            if p.is_absolute() {
                                p.to_path_buf()
                            } else {
                                base_dir.join(p)
                            }
                        };
                        if !mount_single_media(&mut vm, &mut hdd0_path, &entry_path, &mut fd_slot) {
                            return false;
                        }
                    }
                } else if !mount_single_media(&mut vm, &mut hdd0_path, media_path, &mut fd_slot) {
                    return false;
                }
            }
        }
    }

    if vm.bus.fdc.drives[0].is_inserted() {
        vm.bus.set_boot_from_scsi(false);
    } else if hdd0_path.is_some() {
        vm.bus.set_boot_from_scsi(true);
    } else {
        vm.bus.set_boot_from_scsi(false);
    }

    if let Err(e) = vm.reset() {
        log_msg(RETRO_LOG_ERROR, &format!("X68000 VM reset failed: {e:?}"));
        return false;
    }
    vm.set_audio_enabled(true);

    let (cur_w, cur_h) = vm.bus.crtc.active_resolution();
    let fd_accesses = [
        Arc::clone(&vm.bus.fdc.drives[0].accesses),
        Arc::clone(&vm.bus.fdc.drives[1].accesses),
        Arc::clone(&vm.bus.fdc.drives[2].accesses),
        Arc::clone(&vm.bus.fdc.drives[3].accesses),
    ];
    let fd_seen = [
        fd_accesses[0].load(Ordering::Relaxed),
        fd_accesses[1].load(Ordering::Relaxed),
        fd_accesses[2].load(Ordering::Relaxed),
        fd_accesses[3].load(Ordering::Relaxed),
    ];
    let scsi_accesses = vm.bus.drive_access_counter();
    let scsi_seen = scsi_accesses.load(Ordering::Relaxed);
    let perf_raw = std::env::var("CZX_PERF_LOG").ok();
    let perf_log = perf_raw
        .as_deref()
        .is_some_and(|v| !v.is_empty() && v != "0");
    let perf_detail = perf_raw
        .as_deref()
        .is_some_and(|v| v == "2" || v.eq_ignore_ascii_case("detail") || v.eq_ignore_ascii_case("full"));

    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.hw_get_fbo = None;
        cbs.hw_get_proc = None;
    }

    let generation = parse_renderer_option(get_variable(VAR_RENDERER_K).as_deref());
    let gpu_scale = parse_gpu_scale_option(get_variable(VAR_GPU_SCALE_K).as_deref());

    let mut hw_render_enabled = false;
    let mut renderer_gen = ShaderGeneration::Gles2;

    if generation != ShaderGeneration::Cpu {
        let mut hw_cb = RetroHwRenderCallback {
            context_type: RETRO_HW_CONTEXT_OPENGLES2,
            context_reset: Some(hw_context_reset),
            get_current_framebuffer: None,
            get_proc_address: None,
            depth: false,
            stencil: false,
            bottom_left_origin: false,
            version_major: 2,
            version_minor: 0,
            cache_context: false,
            context_destroy: Some(hw_context_destroy),
            debug_context: false,
        };
        if env_call(RETRO_ENVIRONMENT_SET_HW_RENDER, &mut hw_cb) {
            if let Ok(mut cbs) = CALLBACKS.lock() {
                cbs.hw_get_fbo = hw_cb.get_current_framebuffer;
                cbs.hw_get_proc = hw_cb.get_proc_address;
            }
            hw_render_enabled = true;
        }
    }

    let compositor_opt = if hw_render_enabled {
        None
    } else {
        let mut compositor = ViconCompositor::new(RendererConfig {
            generation,
            max_width: 1024,
            max_height: 1024,
            output_format: PixelFormat::Rgb565,
        });
        compositor.set_collect_timing(perf_detail);
        renderer_gen = compositor.active_generation();
        match compositor.renderer_name() {
            Some(name) if !name.is_empty() => {
                log_msg(RETRO_LOG_INFO, &format!("VICON renderer: {renderer_gen:?} ({name})"));
            }
            _ => {
                log_msg(RETRO_LOG_INFO, &format!("VICON renderer: {renderer_gen:?}"));
            }
        }
        Some(compositor)
    };

    let cur_aspect_bits = aspect_for_resolution(cur_w, cur_h).to_bits();
    let cur_fps = compute_crtc_fps(&vm.bus.crtc);

    let state = CoreState {
        vm,
        sram_path,
        hdd0_path,
        swap_drive,
        pending_floppy: [None, None],
        ejected: [false, false],
        held_scancodes: [false; 128],
        joy_sent: 0xFF,
        mouse_left: false,
        mouse_right: false,
        cur_w,
        cur_h,
        cur_aspect_bits,
        cur_fps,
        gpu_scale,
        pending_scaled: false,
        need_full_vram_upload: hw_render_enabled,
        hw_render_enabled,
        renderer_gen,
        fd_accesses,
        fd_seen,
        fd_hold: [0; 2],
        scsi_accesses,
        scsi_seen,
        scsi_hold: 0,
        last_led_mask: -1,
        frame_counter: 0,
        audio_opm: Vec::with_capacity(2048),
        audio_pcm: Vec::with_capacity(4096),
        perf_log,
        perf_detail,
        perf_frames: 0,
        perf_cpu_ns: 0,
        perf_finish_ns: 0,
        perf_submit_ns: 0,
        perf_up_us: 0,
        perf_draw_us: 0,
        perf_rb_us: 0,
        perf_timing_samples: 0,
    };

    if let Ok(mut guard) = CORE_STATE.lock() {
        *guard = Some(state);
        COMPOSITOR.with(|cell| {
            *cell.borrow_mut() = compositor_opt;
        });
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_api_version() -> c_uint {
    RETRO_API_VERSION
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_environment(cb: Option<RetroEnvironmentFn>) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Ok(mut cbs) = CALLBACKS.lock() {
            cbs.env = cb;
        }
        let mut support_no_game = true;
        env_call(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &mut support_no_game);

        let vars = [
            RetroVariable {
                key: VAR_SPEED_K.as_ptr() as *const c_char,
                value: VAR_SPEED_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_CPU_K.as_ptr() as *const c_char,
                value: VAR_CPU_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_RAM_K.as_ptr() as *const c_char,
                value: VAR_RAM_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_MODE_K.as_ptr() as *const c_char,
                value: VAR_MODE_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_DRIVE_K.as_ptr() as *const c_char,
                value: VAR_DRIVE_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_RENDERER_K.as_ptr() as *const c_char,
                value: VAR_RENDERER_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: VAR_GPU_SCALE_K.as_ptr() as *const c_char,
                value: VAR_GPU_SCALE_V.as_ptr() as *const c_char,
            },
            RetroVariable {
                key: std::ptr::null(),
                value: std::ptr::null(),
            },
        ];
        env_call(RETRO_ENVIRONMENT_SET_VARIABLES, vars.as_ptr() as *mut RetroVariable);

        let mut kbd = RetroKeyboardCallback {
            callback: Some(on_retro_keyboard),
        };
        env_call(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &mut kbd);

        let mut disk = RetroDiskControlCallback {
            set_eject_state: Some(disk_set_eject_state),
            get_eject_state: Some(disk_get_eject_state),
            get_image_index: Some(disk_get_image_index),
            set_image_index: Some(disk_set_image_index),
            get_num_images: Some(disk_get_num_images),
            replace_image_index: Some(disk_replace_image_index),
            add_image_index: Some(disk_add_image_index),
        };
        env_call(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &mut disk);

        let mut log_if = RetroLogCallback { log: None };
        if env_call(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &mut log_if) {
            if let Ok(mut cbs) = CALLBACKS.lock() {
                cbs.log = log_if.log;
            }
        }

        let mut led_if = RetroLedInterface { set_led_state: None };
        if env_call(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &mut led_if) {
            if let Ok(mut cbs) = CALLBACKS.lock() {
                cbs.led = led_if.set_led_state;
            }
        }
    }));
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_video_refresh(cb: Option<RetroVideoRefreshFn>) {
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.video = cb;
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_audio_sample(cb: Option<RetroAudioSampleFn>) {
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.audio_sample = cb;
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_audio_sample_batch(cb: Option<RetroAudioSampleBatchFn>) {
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.audio_batch = cb;
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_input_poll(cb: Option<RetroInputPollFn>) {
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.input_poll = cb;
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_input_state(cb: Option<RetroInputStateFn>) {
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.input_state = cb;
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_set_controller_port_device(_port: c_uint, _device: c_uint) {}

#[no_mangle]
pub unsafe extern "C" fn retro_init() {
    if let Ok(mut guard) = CORE_STATE.lock() {
        *guard = None;
    }
}

fn teardown_core_state() {
    let mut state_opt = CORE_STATE.lock().ok().and_then(|mut g| g.take());
    let hw_active = state_opt.as_ref().is_some_and(|s| s.hw_render_enabled);
    COMPOSITOR.with(|cell| {
        if hw_active {
            if let Some(mut comp) = cell.borrow_mut().take() {
                comp.mark_context_lost();
            }
        } else {
            *cell.borrow_mut() = None;
        }
    });
    if let Ok(mut cbs) = CALLBACKS.lock() {
        cbs.hw_get_fbo = None;
        cbs.hw_get_proc = None;
    }
    if let Some(mut st) = state_opt.take() {
        st.flush_persistent(true);
        st.set_led_mask(0);
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_deinit() {
    let _ = catch_unwind(AssertUnwindSafe(teardown_core_state));
}

#[no_mangle]
pub unsafe extern "C" fn retro_get_system_info(info: *mut RetroSystemInfo) {
    if info.is_null() {
        return;
    }
    let out = &mut *info;
    out.library_name = LIB_NAME.as_ptr() as *const c_char;
    out.library_version = LIB_VER.as_ptr() as *const c_char;
    out.valid_extensions = VALID_EXTS.as_ptr() as *const c_char;
    out.need_fullpath = true;
    out.block_extract = false;
}

#[no_mangle]
pub unsafe extern "C" fn retro_get_system_av_info(info: *mut RetroSystemAvInfo) {
    if info.is_null() {
        return;
    }
    let (w, h, aspect, fps) = CORE_STATE
        .lock()
        .ok()
        .and_then(|g| {
            g.as_ref()
                .map(|s| (s.cur_w, s.cur_h, f32::from_bits(s.cur_aspect_bits), s.cur_fps))
        })
        .unwrap_or_else(|| {
            (
                768,
                512,
                aspect_for_resolution(768, 512),
                DEFAULT_X68K_FPS,
            )
        });
    let out = &mut *info;
    out.geometry = RetroGameGeometry {
        base_width: w,
        base_height: h,
        max_width: 1024,
        max_height: 1024,
        aspect_ratio: aspect,
    };
    out.timing = RetroSystemTiming {
        fps,
        sample_rate: OPM_SAMPLE_RATE,
    };
}

#[no_mangle]
pub unsafe extern "C" fn retro_load_game(game: *const RetroGameInfo) -> bool {
    catch_unwind(AssertUnwindSafe(|| load_game_inner(game))).unwrap_or(false)
}

#[no_mangle]
pub unsafe extern "C" fn retro_load_game_special(
    _game_type: c_uint,
    _info: *const RetroGameInfo,
    _num_info: usize,
) -> bool {
    false
}

#[no_mangle]
pub unsafe extern "C" fn retro_unload_game() {
    let _ = catch_unwind(AssertUnwindSafe(teardown_core_state));
}

#[no_mangle]
pub unsafe extern "C" fn retro_reset() {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let hw_enabled = CORE_STATE
            .lock()
            .ok()
            .and_then(|g| g.as_ref().map(|s| s.hw_render_enabled))
            .unwrap_or(false);
        if !hw_enabled {
            COMPOSITOR.with(|cell| {
                if let Some(comp) = cell.borrow_mut().as_mut() {
                    let _ = comp.finish_frame_rgb565();
                }
            });
        }
        if let Ok(mut guard) = CORE_STATE.lock() {
            if let Some(st) = guard.as_mut() {
                st.flush_persistent(true);
                st.held_scancodes = [false; 128];
                st.joy_sent = 0xFF;
                st.vm.bus.ppi.set_port_a(0xFF);
                if let Err(e) = st.vm.reset() {
                    log_msg(RETRO_LOG_ERROR, &format!("X68000 VM reset failed: {e:?}"));
                }
                st.vm.set_audio_enabled(true);
                st.audio_opm.clear();
                st.audio_pcm.clear();
                st.pending_scaled = false;
                st.need_full_vram_upload = true;
            }
        }
    }));
}

fn notify_geometry_and_fps(st: &mut CoreState, w: u32, h: u32, aspect: f32, next_fps: f64) {
    let aspect_bits = aspect.to_bits();
    let geom_changed = w != st.cur_w || h != st.cur_h || aspect_bits != st.cur_aspect_bits;
    let fps_changed = (next_fps - st.cur_fps).abs() >= 0.05;
    if geom_changed {
        st.cur_w = w;
        st.cur_h = h;
        st.cur_aspect_bits = aspect_bits;
    }
    if fps_changed {
        st.cur_fps = next_fps;
        let mut av = RetroSystemAvInfo {
            geometry: RetroGameGeometry {
                base_width: st.cur_w,
                base_height: st.cur_h,
                max_width: 1024,
                max_height: 1024,
                aspect_ratio: f32::from_bits(st.cur_aspect_bits),
            },
            timing: RetroSystemTiming {
                fps: st.cur_fps,
                sample_rate: OPM_SAMPLE_RATE,
            },
        };
        env_call(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &mut av);
    } else if geom_changed {
        let mut geom = RetroGameGeometry {
            base_width: st.cur_w,
            base_height: st.cur_h,
            max_width: 1024,
            max_height: 1024,
            aspect_ratio: f32::from_bits(st.cur_aspect_bits),
        };
        env_call(RETRO_ENVIRONMENT_SET_GEOMETRY, &mut geom);
    }
}

#[no_mangle]
pub unsafe extern "C" fn retro_run() {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let (video_cb, audio_batch_cb, input_poll_cb, input_state_cb, hw_get_fbo) = {
            let Ok(cbs) = CALLBACKS.lock() else {
                return;
            };
            (
                cbs.video,
                cbs.audio_batch,
                cbs.input_poll,
                cbs.input_state,
                cbs.hw_get_fbo,
            )
        };

        let mut vars_updated = false;
        if env_call(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &mut vars_updated) && vars_updated {
            let cpu = parse_cpu_option(get_variable(VAR_CPU_K).as_deref());
            let next_speed = parse_speed_option(get_variable(VAR_SPEED_K).as_deref(), cpu);
            let next_drive = parse_drive_option();
            let next_gpu_scale = parse_gpu_scale_option(get_variable(VAR_GPU_SCALE_K).as_deref());
            if let Ok(mut guard) = CORE_STATE.lock() {
                if let Some(st) = guard.as_mut() {
                    if st.vm.speed_mode() != next_speed {
                        st.vm.set_speed_mode(next_speed);
                    }
                    st.swap_drive = next_drive;
                    st.gpu_scale = next_gpu_scale;
                }
            }
        }

        if let Some(poll) = input_poll_cb {
            unsafe {
                poll();
            }
        }

        let Ok(mut guard) = CORE_STATE.lock() else {
            return;
        };
        let Some(st) = guard.as_mut() else {
            return;
        };

        if let Some(state_fn) = input_state_cb {
            // Joystick Port 0 -> PPI Port A (active-low bits: UP=0x01, DOWN=0x02, LEFT=0x04, RIGHT=0x08, A=0x20, B=0x40)
            let mask = unsafe { state_fn(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) } as u16;
            let mut joy_active = 0u8;
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) != 0 {
                joy_active |= 0x01;
            }
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) != 0 {
                joy_active |= 0x02;
            }
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) != 0 {
                joy_active |= 0x04;
            }
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0 {
                joy_active |= 0x08;
            }
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_A)) != 0 {
                joy_active |= 0x20;
            }
            if (mask & (1 << RETRO_DEVICE_ID_JOYPAD_B)) != 0 {
                joy_active |= 0x40;
            }
            let port_a = !joy_active;
            if port_a != st.joy_sent {
                st.joy_sent = port_a;
                st.vm.bus.ppi.set_port_a(port_a);
            }

            // Mouse Port 0 -> Z8530 SCC Channel B
            let mdx = unsafe { state_fn(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) } as i32;
            let mdy = unsafe { state_fn(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) } as i32;
            let mleft = unsafe { state_fn(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) } != 0;
            let mright = unsafe { state_fn(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) } != 0;
            let btn_changed = mleft != st.mouse_left || mright != st.mouse_right;
            if btn_changed {
                st.mouse_left = mleft;
                st.mouse_right = mright;
                st.vm.bus.scc.set_mouse_buttons(mleft, mright);
            }
            if mdx != 0 || mdy != 0 || btn_changed {
                st.vm.bus.scc.add_mouse_delta(mdx, mdy);
            }
        }

        let t0 = st.perf_log.then(Instant::now);
        let run_fps = compute_crtc_fps(&st.vm.bus.crtc);
        if st.vm.speed_mode().is_burst() {
            let burst_ticks = (10_000_000.0 / run_fps).round().clamp(1.0, 500_000.0) as u32;
            st.vm.run_burst_frame(160_000, burst_ticks);
        } else {
            let cpu_mhz = st.vm.timing_config().cpu_mhz.max(10) as f64;
            let frame_cycles = ((cpu_mhz * 1_000_000.0) / run_fps).round() as u32;
            st.vm.run_frame(FrameBudget::Cycles(frame_cycles.max(1)));
        }

        let t1 = st.perf_log.then(Instant::now);
        let mut presented_w = st.cur_w;
        let mut presented_h = st.cur_h;
        let next_fps = compute_crtc_fps(&st.vm.bus.crtc);

        let (timing, t2, t3): (Option<FrameTimingStats>, Option<Instant>, Option<Instant>) =
            if st.hw_render_enabled {
                let fbo = hw_get_fbo.map(|f| unsafe { f() } as u32).unwrap_or(0);
                let (hw_dims, timing, t_a, t_b) = COMPOSITOR.with(|cell| {
                    let mut borrow = cell.borrow_mut();
                    if let Some(comp) = borrow.as_mut() {
                        let dims = if st.need_full_vram_upload {
                            st.need_full_vram_upload = false;
                            let _ = st.vm.bus.take_written_vram_pages();
                            comp.render_frame_to_fbo_readonly(&st.vm.bus, fbo, false)
                        } else {
                            comp.render_frame_to_fbo(&mut st.vm.bus, fbo, false)
                        };
                        let timing = if dims.is_some() && st.perf_detail {
                            comp.last_timing()
                        } else {
                            None
                        };
                        let t_end = st.perf_log.then(Instant::now);
                        (dims, timing, t1, t_end)
                    } else {
                        (None, None, t1, t1)
                    }
                });
                if let Some((raw_w, raw_h)) = hw_dims {
                    let w = raw_w.max(1);
                    let h = raw_h.max(1);
                    notify_geometry_and_fps(st, w, h, aspect_for_resolution(w, h), next_fps);
                    presented_w = w;
                    presented_h = h;
                    if let Some(vcb) = video_cb {
                        unsafe {
                            vcb(RETRO_HW_FRAME_BUFFER_VALID, w, h, 0);
                        }
                    }
                } else {
                    notify_geometry_and_fps(
                        st,
                        st.cur_w,
                        st.cur_h,
                        f32::from_bits(st.cur_aspect_bits),
                        next_fps,
                    );
                }
                (timing, t_a, t_b)
            } else {
                let (rendered_opt, out_scaled, timing, t_a, t_b): (
                    Option<RenderedFrame565>,
                    bool,
                    Option<FrameTimingStats>,
                    Option<Instant>,
                    Option<Instant>,
                ) = COMPOSITOR.with(|cell| {
                    let mut borrow = cell.borrow_mut();
                    if let Some(comp) = borrow.as_mut() {
                        if st.need_full_vram_upload {
                            st.need_full_vram_upload = false;
                            let _ = st.vm.bus.take_written_vram_pages();
                            let frame = comp.render_frame_readonly(&st.vm.bus).to_rgb565();
                            st.pending_scaled = false;
                            let t_end = st.perf_log.then(Instant::now);
                            (Some(frame), false, None, t1, t_end)
                        } else if st.renderer_gen == ShaderGeneration::Gles2 {
                            let prev = comp.finish_frame_rgb565();
                            let prev_scaled = st.pending_scaled;
                            let timing = if prev.is_some() && st.perf_detail {
                                comp.last_timing()
                            } else {
                                None
                            };
                            let t_after_finish = st.perf_log.then(Instant::now);
                            let (raw_w, raw_h) = st.vm.bus.crtc.active_resolution();
                            let nw = raw_w.clamp(256, 1024);
                            let nh = raw_h.clamp(128, 1024);
                            if let Some((dw, dh)) = compute_gpu_scale_target(st.gpu_scale, nw, nh) {
                                comp.submit_frame_scaled_rgb565(&mut st.vm.bus, dw, dh);
                                st.pending_scaled = true;
                            } else {
                                comp.submit_frame_rgb565(&mut st.vm.bus);
                                st.pending_scaled = false;
                            }
                            let t_after_submit = st.perf_log.then(Instant::now);
                            (prev, prev_scaled, timing, t_after_finish, t_after_submit)
                        } else {
                            let frame = comp.render_frame_rgb565(&mut st.vm.bus);
                            st.pending_scaled = false;
                            let t_end = st.perf_log.then(Instant::now);
                            (Some(frame), false, None, t1, t_end)
                        }
                    } else {
                        (None, false, None, t1, t1)
                    }
                });

                if let Some(rendered) = rendered_opt {
                    let w = rendered.width.max(1);
                    let h = rendered.height.max(1);
                    let aspect = if out_scaled {
                        0.0
                    } else {
                        aspect_for_resolution(w, h)
                    };
                    notify_geometry_and_fps(st, w, h, aspect, next_fps);
                    presented_w = w;
                    presented_h = h;

                    if let Some(vcb) = video_cb {
                        unsafe {
                            vcb(
                                rendered.rgb565.as_ptr() as *const c_void,
                                w,
                                h,
                                (w as usize) * std::mem::size_of::<u16>(),
                            );
                        }
                    }
                } else {
                    notify_geometry_and_fps(
                        st,
                        st.cur_w,
                        st.cur_h,
                        f32::from_bits(st.cur_aspect_bits),
                        next_fps,
                    );
                }
                (timing, t_a, t_b)
            };

        if let (Some(a), Some(b), Some(c), Some(d)) = (t0, t1, t2, t3) {
            st.perf_cpu_ns += b.duration_since(a).as_nanos() as u64;
            st.perf_finish_ns += c.duration_since(b).as_nanos() as u64;
            st.perf_submit_ns += d.duration_since(c).as_nanos() as u64;
            if let Some(t) = timing {
                st.perf_up_us += t.upload_us as u64;
                st.perf_draw_us += t.draw_us as u64;
                st.perf_rb_us += t.readback_us as u64;
                st.perf_timing_samples += 1;
            }
            st.perf_frames += 1;
            if st.perf_frames >= 60 {
                let n = st.perf_frames as f64;
                let cpu_ms = (st.perf_cpu_ns as f64) / (n * 1_000_000.0);
                let fin_ms = (st.perf_finish_ns as f64) / (n * 1_000_000.0);
                let sub_ms = (st.perf_submit_ns as f64) / (n * 1_000_000.0);
                let total_ms = cpu_ms + fin_ms + sub_ms;
                let gen = st.renderer_gen;
                if st.perf_timing_samples > 0 {
                    let tn = st.perf_timing_samples as f64;
                    let up_ms = (st.perf_up_us as f64) / (tn * 1000.0);
                    let draw_ms = (st.perf_draw_us as f64) / (tn * 1000.0);
                    let rb_ms = (st.perf_rb_us as f64) / (tn * 1000.0);
                    eprintln!(
                        "[msx68k perf] [{gen:?}] cpu={cpu_ms:.2}ms finish={fin_ms:.2}ms submit={sub_ms:.2}ms total={total_ms:.2}ms [up={up_ms:.2}ms draw={draw_ms:.2}ms rb={rb_ms:.2}ms] ({presented_w}x{presented_h})"
                    );
                } else {
                    eprintln!(
                        "[msx68k perf] [{gen:?}] cpu={cpu_ms:.2}ms finish={fin_ms:.2}ms submit={sub_ms:.2}ms total={total_ms:.2}ms ({presented_w}x{presented_h})"
                    );
                }
                st.perf_frames = 0;
                st.perf_cpu_ns = 0;
                st.perf_finish_ns = 0;
                st.perf_submit_ns = 0;
                st.perf_up_us = 0;
                st.perf_draw_us = 0;
                st.perf_rb_us = 0;
                st.perf_timing_samples = 0;
            }
        }

        st.audio_opm.clear();
        st.vm.take_audio_samples(&mut st.audio_opm);
        if !st.audio_opm.is_empty() {
            if let Some(acb) = audio_batch_cb {
                st.audio_pcm.clear();
                st.audio_pcm.reserve(st.audio_opm.len() * 2);
                for &(l, r) in &st.audio_opm {
                    st.audio_pcm.push(l);
                    st.audio_pcm.push(r);
                }
                unsafe {
                    acb(st.audio_pcm.as_ptr(), st.audio_opm.len());
                }
            }
        }

        let mut led_mask: i32 = 0;
        let mut fd_delta: u64 = 0;
        for unit in 0..4 {
            let cur = st.fd_accesses[unit].load(Ordering::Relaxed);
            let delta = cur.wrapping_sub(st.fd_seen[unit]);
            if delta != 0 {
                st.fd_seen[unit] = cur;
                fd_delta = fd_delta.wrapping_add(delta);
            }
            if unit < 2 {
                if delta != 0 {
                    st.fd_hold[unit] = 8;
                } else if st.fd_hold[unit] > 0 {
                    st.fd_hold[unit] -= 1;
                }
                if st.fd_hold[unit] > 0 {
                    led_mask |= 1 << unit;
                }
            }
        }
        let total_cur = st.scsi_accesses.load(Ordering::Relaxed);
        let total_delta = total_cur.wrapping_sub(st.scsi_seen);
        st.scsi_seen = total_cur;
        if total_delta > fd_delta {
            st.scsi_hold = 8;
        } else if st.scsi_hold > 0 {
            st.scsi_hold -= 1;
        }
        if st.scsi_hold > 0 {
            led_mask |= 0x04;
        }
        st.set_led_mask(led_mask);

        st.frame_counter = st.frame_counter.wrapping_add(1);
        if st.frame_counter % 60 == 0 {
            st.flush_persistent(false);
        }
    }));
}

#[no_mangle]
pub unsafe extern "C" fn retro_get_region() -> c_uint {
    RETRO_REGION_NTSC
}

#[no_mangle]
pub unsafe extern "C" fn retro_serialize_size() -> usize {
    0
}

#[no_mangle]
pub unsafe extern "C" fn retro_serialize(_data: *mut c_void, _size: usize) -> bool {
    false
}

#[no_mangle]
pub unsafe extern "C" fn retro_unserialize(_data: *const c_void, _size: usize) -> bool {
    false
}

#[no_mangle]
pub unsafe extern "C" fn retro_cheat_reset() {}

#[no_mangle]
pub unsafe extern "C" fn retro_cheat_set(_index: c_uint, _enabled: bool, _code: *const c_char) {}

#[no_mangle]
pub unsafe extern "C" fn retro_get_memory_data(_id: c_uint) -> *mut c_void {
    std::ptr::null_mut()
}

#[no_mangle]
pub unsafe extern "C" fn retro_get_memory_size(_id: c_uint) -> usize {
    0
}

#[allow(dead_code)]
const fn _assert_retro_device_mask() {
    let _ = RETRO_DEVICE_MASK;
}
