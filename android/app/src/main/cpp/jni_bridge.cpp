#include "android_platform.hpp"

#include "app/emulator_app.hpp"

#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::mutex g_app_mtx;
std::unique_ptr<czx::app::EmulatorApp> g_app;
std::thread g_emu_thread;

std::string jstring_to_std(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* utf = env->GetStringUTFChars(js, nullptr);
    if (!utf) return {};
    std::string out(utf);
    env->ReleaseStringUTFChars(js, utf);
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) >= 0x20u) {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeStart(JNIEnv* env,
                                           jobject,
                                           jstring data_dir,
                                           jstring core_dir,
                                           jstring initial_core) {
    const std::string data_str = jstring_to_std(env, data_dir);
    const std::string core_str = jstring_to_std(env, core_dir);
    const std::string init_core_str = jstring_to_std(env, initial_core);

    std::lock_guard<std::mutex> lock(g_app_mtx);
    if (g_app || g_emu_thread.joinable()) {
        return;
    }

    czx::app::AppConfig cfg{};
    cfg.data_dir = data_str;
    cfg.core_dir = core_str;
    cfg.core_spec = init_core_str;
    cfg.force_desktop = true;
    cfg.platform.desktop_native = true;
    cfg.platform.fullscreen = true;
    cfg.platform.suppress_alt_bar = true;
    cfg.platform.disable_host_shortcuts = true;
    cfg.platform.vsync = true;

    g_app = std::make_unique<czx::app::EmulatorApp>(cfg);
    g_emu_thread = std::thread([]() {
        g_app->run();
        czx::platform::android::AndroidBridge::instance().release_egl();
    });
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeStop(JNIEnv*, jobject) {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(g_app_mtx);
        if (g_app) {
            g_app->request_stop();
        }
        to_join = std::move(g_emu_thread);
    }
    czx::platform::android::AndroidBridge::instance().set_paused(false);
    if (to_join.joinable()) {
        to_join.join();
    }
    std::lock_guard<std::mutex> lock(g_app_mtx);
    g_app.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeSetPaused(JNIEnv*, jobject, jboolean paused) {
    czx::platform::android::AndroidBridge::instance().set_paused(paused == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeOnSurfaceCreated(JNIEnv* env, jobject, jobject surface) {
    if (!surface) return;
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return;
    czx::platform::android::AndroidBridge::instance().set_window(win);
    ANativeWindow_release(win);
    std::lock_guard<std::mutex> lock(g_app_mtx);
    if (g_app) {
        g_app->notify_surface_resized();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeOnSurfaceChanged(JNIEnv*, jobject, jint width, jint height) {
    czx::platform::android::AndroidBridge::instance().on_surface_resized(width, height);
    std::lock_guard<std::mutex> lock(g_app_mtx);
    if (g_app) {
        g_app->notify_surface_resized();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeOnSurfaceDestroyed(JNIEnv*, jobject) {
    czx::platform::android::AndroidBridge::instance().clear_window();
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeSendKey(JNIEnv*,
                                             jobject,
                                             jint czx_key,
                                             jboolean pressed,
                                             jint codepoint) {
    czx::input::RawKeyEvent ev{};
    ev.key = static_cast<czx_key_t>(czx_key);
    ev.pressed = (pressed == JNI_TRUE);
    ev.repeat = false;
    ev.codepoint = static_cast<uint32_t>(codepoint);
    ev.native_key = 0u;
    czx::platform::android::AndroidBridge::instance().push_key_event(ev);
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeSendNativeKey(JNIEnv*,
                                                   jobject,
                                                   jint native_key,
                                                   jboolean pressed,
                                                   jint codepoint) {
    czx::input::RawKeyEvent ev{};
    ev.key = CZX_KEY_NONE;
    ev.pressed = (pressed == JNI_TRUE);
    ev.repeat = false;
    ev.codepoint = static_cast<uint32_t>(codepoint);
    ev.native_key = static_cast<uint32_t>(native_key);
    czx::platform::android::AndroidBridge::instance().push_key_event(ev);
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeSetGamepadButtons(JNIEnv*, jobject, jint port, jint mask) {
    czx::platform::android::AndroidBridge::instance().set_gamepad_buttons(
        static_cast<uint32_t>(port), static_cast<uint32_t>(mask));
}

extern "C" JNIEXPORT void JNICALL
Java_com_czx_android_CzxNative_nativeSendHostCommand(JNIEnv*, jobject, jint cmd_ord) {
    std::lock_guard<std::mutex> lock(g_app_mtx);
    if (!g_app) return;
    g_app->enqueue_host_command(static_cast<czx::input::HostCommand>(cmd_ord));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_czx_android_CzxNative_nativeGetUiSnapshot(JNIEnv* env, jobject) {
    czx::app::EmulatorApp::UiSnapshot snap{};
    {
        std::lock_guard<std::mutex> lock(g_app_mtx);
        if (g_app) {
            snap = g_app->get_ui_snapshot();
        }
    }
    char buf[512];
    std::snprintf(buf,
                  sizeof(buf),
                  "{\"uiActive\":%s,\"inLauncher\":%s,\"menuOpen\":%s,\"browserOpen\":%s,"
                  "\"coreId\":\"%s\",\"coreName\":\"%s\",\"presetIndex\":%d,"
                  "\"aspectPolicy\":%d,\"inputMode\":%d,\"activityFlags\":%u}",
                  snap.ui_active ? "true" : "false",
                  snap.in_launcher ? "true" : "false",
                  snap.menu_open ? "true" : "false",
                  snap.browser_open ? "true" : "false",
                  json_escape(snap.core_id).c_str(),
                  json_escape(snap.core_name).c_str(),
                  snap.preset_index,
                  snap.aspect_policy,
                  snap.input_mode,
                  snap.activity_flags);
    return env->NewStringUTF(buf);
}
