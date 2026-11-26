package com.czx.android

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.czx.android.keyboard.KeyboardRepository
import com.czx.android.keyboard.RetroKeyboardView
import com.czx.android.ui.BottomLayoutMode
import com.czx.android.ui.TouchMenuBar
import com.czx.android.ui.VirtualGamepad
import java.io.File
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var activeCoreId: String = ""

    private val openMediaLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        if (uri != null) {
            importMediaUri(uri)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val dataDir = File(filesDir, "czx").apply { mkdirs() }.absolutePath
        val coreDir = applicationInfo.nativeLibraryDir
        CzxNative.nativeStart(dataDir, coreDir, "")

        val layouts = KeyboardRepository.loadAll(this)

        setContent {
            var snapshot by remember { mutableStateOf(UiSnapshot()) }
            var bottomMode by remember { mutableStateOf(BottomLayoutMode.KBD_AND_PAD) }
            var previewLayoutIdx by remember { mutableIntStateOf(0) }

            LaunchedEffect(Unit) {
                while (true) {
                    val next = CzxNative.getUiSnapshot()
                    snapshot = next
                    activeCoreId = next.coreId
                    delay(50)
                }
            }

            val autoLayout = remember(layouts, snapshot.coreId) {
                KeyboardRepository.selectForCore(layouts, snapshot.coreId)
            }
            val canTogglePreview = snapshot.coreId == "test_pattern" && layouts.size > 1
            val activeLayout = if (canTogglePreview) {
                layouts[previewLayoutIdx % layouts.size]
            } else {
                autoLayout
            }

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0xFF0C0F14))
                    .systemBarsPadding()
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16f / 10f)
                        .heightIn(max = 320.dp)
                        .background(Color.Black)
                ) {
                    AndroidView(
                        modifier = Modifier.fillMaxSize(),
                        factory = { ctx ->
                            SurfaceView(ctx).apply {
                                holder.addCallback(object : SurfaceHolder.Callback {
                                    override fun surfaceCreated(holder: SurfaceHolder) {
                                        CzxNative.nativeOnSurfaceCreated(holder.surface)
                                    }

                                    override fun surfaceChanged(
                                        holder: SurfaceHolder,
                                        format: Int,
                                        width: Int,
                                        height: Int,
                                    ) {
                                        CzxNative.nativeOnSurfaceChanged(width, height)
                                    }

                                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                                        CzxNative.nativeOnSurfaceDestroyed()
                                    }
                                })
                            }
                        },
                    )
                }

                TouchMenuBar(
                    snapshot = snapshot,
                    bottomMode = bottomMode,
                    activeKeyboardName = activeLayout?.name ?: "Keyboard",
                    canTogglePreviewLayout = canTogglePreview,
                    onToggleMenu = { CzxNative.nativeSendHostCommand(HostCommands.TOGGLE_MENU) },
                    onCycleAspect = {
                        val nextCmd = when (snapshot.aspectPolicy) {
                            0 -> HostCommands.SET_MODE_OVERFLOW_FIT
                            1 -> HostCommands.SET_MODE_ASPECT_FIT
                            else -> HostCommands.SET_MODE_NATIVE_PAN
                        }
                        CzxNative.nativeSendHostCommand(nextCmd)
                    },
                    onCycleBottomMode = {
                        val all = BottomLayoutMode.entries
                        bottomMode = all[(bottomMode.ordinal + 1) % all.size]
                    },
                    onCyclePreviewKeyboard = {
                        if (layouts.isNotEmpty()) {
                            previewLayoutIdx = (previewLayoutIdx + 1) % layouts.size
                        }
                    },
                    onImportMedia = { openMediaLauncher.launch(arrayOf("*/*")) },
                )

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                ) {
                    if (snapshot.uiActive) {
                        VirtualGamepad(
                            compact = false,
                            uiGuideMode = true,
                            onButtonsChanged = { mask -> CzxNative.nativeSetGamepadButtons(0, mask) },
                            modifier = Modifier.fillMaxSize(),
                        )
                    } else {
                        when (bottomMode) {
                            BottomLayoutMode.KBD_AND_PAD -> {
                                Column(modifier = Modifier.fillMaxSize()) {
                                    RetroKeyboardView(
                                        layout = activeLayout,
                                        modifier = Modifier
                                            .weight(0.56f)
                                            .fillMaxWidth(),
                                        onSendKey = { key, pressed ->
                                            if (key.nativeKey != 0) {
                                                CzxNative.nativeSendNativeKey(
                                                    key.nativeKey,
                                                    pressed,
                                                    key.codepoint,
                                                )
                                            } else {
                                                CzxNative.nativeSendKey(
                                                    key.czxKey,
                                                    pressed,
                                                    key.codepoint,
                                                )
                                            }
                                        },
                                    )
                                    VirtualGamepad(
                                        compact = true,
                                        uiGuideMode = false,
                                        onButtonsChanged = { mask ->
                                            CzxNative.nativeSetGamepadButtons(0, mask)
                                        },
                                        modifier = Modifier
                                            .weight(0.44f)
                                            .fillMaxWidth(),
                                    )
                                }
                            }
                            BottomLayoutMode.KBD_LARGE -> {
                                RetroKeyboardView(
                                    layout = activeLayout,
                                    modifier = Modifier.fillMaxSize(),
                                    onSendKey = { key, pressed ->
                                        if (key.nativeKey != 0) {
                                            CzxNative.nativeSendNativeKey(
                                                key.nativeKey,
                                                pressed,
                                                key.codepoint,
                                            )
                                        } else {
                                            CzxNative.nativeSendKey(
                                                key.czxKey,
                                                pressed,
                                                key.codepoint,
                                            )
                                        }
                                    },
                                )
                            }
                            BottomLayoutMode.PAD_LARGE -> {
                                VirtualGamepad(
                                    compact = false,
                                    uiGuideMode = false,
                                    onButtonsChanged = { mask ->
                                        CzxNative.nativeSetGamepadButtons(0, mask)
                                    },
                                    modifier = Modifier.fillMaxSize(),
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        CzxNative.nativeSetPaused(false)
    }

    override fun onPause() {
        CzxNative.nativeSetPaused(true)
        super.onPause()
    }

    override fun onDestroy() {
        if (isFinishing) {
            CzxNative.nativeStop()
        }
        super.onDestroy()
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val czxKey = CzxKeys.fromAndroidKeyCode(event.keyCode)
        if (czxKey != CzxKeys.NONE) {
            val pressed = event.action == KeyEvent.ACTION_DOWN
            val cp = if (pressed && event.unicodeChar in 0x20..0x7E) event.unicodeChar else 0
            CzxNative.nativeSendKey(czxKey, pressed, cp)
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    private fun importMediaUri(uri: Uri) {
        val name = queryFileName(uri) ?: "imported.bin"
        val subDir = if (activeCoreId.isNotBlank()) activeCoreId else "g800"
        val dstDir = File(filesDir, "czx/media/$subDir").apply { mkdirs() }
        val dstFile = File(dstDir, name)
        runCatching {
            contentResolver.openInputStream(uri)?.use { input ->
                dstFile.outputStream().use { output -> input.copyTo(output) }
            }
        }.onSuccess {
            Toast.makeText(this, "Imported: $name -> media/$subDir/", Toast.LENGTH_SHORT).show()
        }.onFailure { err ->
            Toast.makeText(this, "Import failed: ${err.message}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun queryFileName(uri: Uri): String? {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (idx >= 0) return c.getString(idx)
            }
        }
        return uri.lastPathSegment?.substringAfterLast('/')
    }
}
