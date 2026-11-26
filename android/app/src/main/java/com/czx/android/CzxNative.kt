package com.czx.android

import android.view.KeyEvent
import android.view.Surface
import org.json.JSONObject

data class UiSnapshot(
    val uiActive: Boolean = true,
    val inLauncher: Boolean = true,
    val menuOpen: Boolean = false,
    val browserOpen: Boolean = false,
    val coreId: String = "",
    val coreName: String = "",
    val presetIndex: Int = 0,
    val aspectPolicy: Int = 0,
    val inputMode: Int = 0,
    val activityFlags: Int = 0,
)

object HostCommands {
    const val NONE = 0
    const val SET_MODE_NATIVE_PAN = 1
    const val SET_MODE_OVERFLOW_FIT = 2
    const val SET_MODE_ASPECT_FIT = 3
    const val TOGGLE_INPUT_MODE = 4
    const val TOGGLE_MENU = 5
}

object CzxPad {
    const val UP = 1 shl 0
    const val DOWN = 1 shl 1
    const val LEFT = 1 shl 2
    const val RIGHT = 1 shl 3
    const val SOUTH = 1 shl 4
    const val EAST = 1 shl 5
    const val WEST = 1 shl 6
    const val NORTH = 1 shl 7
    const val SELECT = 1 shl 8
    const val START = 1 shl 9
    const val META = 1 shl 10
    const val L1 = 1 shl 11
    const val R1 = 1 shl 12
}

object CzxKeys {
    const val NONE = 0
    const val A = 1
    const val Z = 26
    const val NUM_0 = 27
    const val NUM_9 = 36
    const val ENTER = 37
    const val ESC = 38
    const val BACKSPACE = 39
    const val TAB = 40
    const val SPACE = 41
    const val MINUS = 42
    const val EQUAL = 43
    const val LEFTBRACE = 44
    const val RIGHTBRACE = 45
    const val BACKSLASH = 46
    const val SEMICOLON = 47
    const val APOSTROPHE = 48
    const val GRAVE = 49
    const val COMMA = 50
    const val DOT = 51
    const val SLASH = 52
    const val UP = 53
    const val DOWN = 54
    const val LEFT = 55
    const val RIGHT = 56
    const val HOME = 57
    const val END = 58
    const val PAGEUP = 59
    const val PAGEDOWN = 60
    const val INSERT = 61
    const val DELETE = 62
    const val F1 = 63
    const val F12 = 74
    const val LSHIFT = 75
    const val RSHIFT = 76
    const val LCTRL = 77
    const val RCTRL = 78
    const val LALT = 79
    const val RALT = 80

    fun fromAndroidKeyCode(keyCode: Int): Int {
        if (keyCode in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z) {
            return A + (keyCode - KeyEvent.KEYCODE_A)
        }
        if (keyCode in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9) {
            return NUM_0 + (keyCode - KeyEvent.KEYCODE_0)
        }
        if (keyCode in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12) {
            return F1 + (keyCode - KeyEvent.KEYCODE_F1)
        }
        return when (keyCode) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> ENTER
            KeyEvent.KEYCODE_ESCAPE -> ESC
            KeyEvent.KEYCODE_DEL -> BACKSPACE
            KeyEvent.KEYCODE_TAB -> TAB
            KeyEvent.KEYCODE_SPACE -> SPACE
            KeyEvent.KEYCODE_MINUS -> MINUS
            KeyEvent.KEYCODE_EQUALS -> EQUAL
            KeyEvent.KEYCODE_LEFT_BRACKET -> LEFTBRACE
            KeyEvent.KEYCODE_RIGHT_BRACKET -> RIGHTBRACE
            KeyEvent.KEYCODE_BACKSLASH -> BACKSLASH
            KeyEvent.KEYCODE_SEMICOLON -> SEMICOLON
            KeyEvent.KEYCODE_APOSTROPHE -> APOSTROPHE
            KeyEvent.KEYCODE_GRAVE -> GRAVE
            KeyEvent.KEYCODE_COMMA -> COMMA
            KeyEvent.KEYCODE_PERIOD -> DOT
            KeyEvent.KEYCODE_SLASH -> SLASH
            KeyEvent.KEYCODE_DPAD_UP -> UP
            KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
            KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
            KeyEvent.KEYCODE_MOVE_HOME -> HOME
            KeyEvent.KEYCODE_MOVE_END -> END
            KeyEvent.KEYCODE_PAGE_UP -> PAGEUP
            KeyEvent.KEYCODE_PAGE_DOWN -> PAGEDOWN
            KeyEvent.KEYCODE_INSERT -> INSERT
            KeyEvent.KEYCODE_FORWARD_DEL -> DELETE
            KeyEvent.KEYCODE_SHIFT_LEFT -> LSHIFT
            KeyEvent.KEYCODE_SHIFT_RIGHT -> RSHIFT
            KeyEvent.KEYCODE_CTRL_LEFT -> LCTRL
            KeyEvent.KEYCODE_CTRL_RIGHT -> RCTRL
            KeyEvent.KEYCODE_ALT_LEFT -> LALT
            KeyEvent.KEYCODE_ALT_RIGHT -> RALT
            else -> NONE
        }
    }
}

object CzxNative {
    init {
        System.loadLibrary("czx_android")
    }

    external fun nativeStart(dataDir: String, coreDir: String, initialCore: String)
    external fun nativeStop()
    external fun nativeSetPaused(paused: Boolean)
    external fun nativeOnSurfaceCreated(surface: Surface)
    external fun nativeOnSurfaceChanged(width: Int, height: Int)
    external fun nativeOnSurfaceDestroyed()
    external fun nativeSendKey(czxKey: Int, pressed: Boolean, codepoint: Int)
    external fun nativeSendNativeKey(nativeKey: Int, pressed: Boolean, codepoint: Int)
    external fun nativeSetGamepadButtons(port: Int, mask: Int)
    external fun nativeSendHostCommand(cmdOrd: Int)
    external fun nativeGetUiSnapshot(): String

    fun getUiSnapshot(): UiSnapshot {
        val raw = nativeGetUiSnapshot()
        if (raw.isBlank()) return UiSnapshot()
        return runCatching {
            val obj = JSONObject(raw)
            UiSnapshot(
                uiActive = obj.optBoolean("uiActive", true),
                inLauncher = obj.optBoolean("inLauncher", true),
                menuOpen = obj.optBoolean("menuOpen", false),
                browserOpen = obj.optBoolean("browserOpen", false),
                coreId = obj.optString("coreId", ""),
                coreName = obj.optString("coreName", ""),
                presetIndex = obj.optInt("presetIndex", 0),
                aspectPolicy = obj.optInt("aspectPolicy", 0),
                inputMode = obj.optInt("inputMode", 0),
                activityFlags = obj.optInt("activityFlags", 0),
            )
        }.getOrDefault(UiSnapshot())
    }
}
