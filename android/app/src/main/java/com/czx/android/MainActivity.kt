package com.czx.android

import android.net.Uri
import android.os.Bundle
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
import com.czx.android.setup.ImportReport
import com.czx.android.setup.MachineRomSpec
import com.czx.android.setup.RomCatalog
import com.czx.android.setup.RomInstaller
import com.czx.android.setup.RomSlot
import com.czx.android.ui.BottomLayoutMode
import com.czx.android.ui.SetupPanel
import com.czx.android.ui.TouchMenuBar
import com.czx.android.ui.VirtualGamepad
import java.io.File
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var refreshNonceState = mutableIntStateOf(0)
    private var importReportState = mutableStateOf<ImportReport?>(null)
    private var pendingSingleSlot: Pair<MachineRomSpec, RomSlot>? = null
    private var pendingZipMachine: MachineRomSpec? = null
    private var pendingMultiMachine: MachineRomSpec? = null
    private var pendingMediaMachine: MachineRomSpec? = null

    private fun czxRoot(): File = File(filesDir, "czx").apply { mkdirs() }

    private val pickSingleRomLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        val target = pendingSingleSlot ?: return@registerForActivityResult
        pendingSingleSlot = null
        if (uri == null) return@registerForActivityResult
        val (machine, slot) = target
        RomInstaller.installSingleRom(this, uri, czxRoot(), machine, slot)
            .onSuccess { dst ->
                refreshNonceState.intValue++
                importReportState.value = ImportReport(placed = listOf("${machine.title}:${dst.name}"))
            }
            .onFailure { err ->
                Toast.makeText(this, "配置失敗: ${err.message}", Toast.LENGTH_SHORT).show()
            }
    }

    private val pickZipLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        val scope = pendingZipMachine
        pendingZipMachine = null
        if (uri == null) return@registerForActivityResult
        val report = RomInstaller.importFromZip(this, uri, czxRoot(), scope)
        refreshNonceState.intValue++
        importReportState.value = report
    }

    private val pickMultiRomLauncher = registerForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris: List<Uri> ->
        val scope = pendingMultiMachine
        pendingMultiMachine = null
        if (uris.isEmpty()) return@registerForActivityResult
        val report = RomInstaller.importMultipleUris(this, uris, czxRoot(), scope)
        refreshNonceState.intValue++
        importReportState.value = report
    }

    private val pickMediaLauncher = registerForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris: List<Uri> ->
        val machine = pendingMediaMachine ?: return@registerForActivityResult
        pendingMediaMachine = null
        if (uris.isEmpty()) return@registerForActivityResult
        val count = RomInstaller.importMediaFiles(this, uris, czxRoot(), machine)
        refreshNonceState.intValue++
        Toast.makeText(this, "${machine.title}: メディア ${count}件追加", Toast.LENGTH_SHORT).show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val dataDir = File(filesDir, "czx").apply { mkdirs() }.absolutePath
        seedBluemsxOpenData(File(dataDir, "roms/bluemsx"))
        val coreDir = applicationInfo.nativeLibraryDir
        CzxNative.nativeStart(dataDir, coreDir, "")

        val layouts = KeyboardRepository.loadAll(this)

        setContent {
            var snapshot by remember { mutableStateOf(UiSnapshot()) }
            var bottomMode by remember { mutableStateOf(BottomLayoutMode.KBD_AND_PAD) }
            var previewLayoutIdx by remember { mutableIntStateOf(0) }
            var keyboardPageIdx by remember { mutableIntStateOf(0) }
            var setupSheetOpen by remember { mutableStateOf(false) }
            var selectedSetupMachineId by remember { mutableStateOf(RomCatalog.machines.first().id) }

            LaunchedEffect(Unit) {
                while (true) {
                    val next = CzxNative.getUiSnapshot()
                    snapshot = next
                    delay(50)
                }
            }
            LaunchedEffect(snapshot.coreId, snapshot.inLauncher) {
                if (snapshot.inLauncher) {
                    setupSheetOpen = false
                } else if (snapshot.coreId.isNotBlank() && snapshot.coreId != "setup") {
                    selectedSetupMachineId = RomCatalog.specForCoreId(snapshot.coreId).id
                }
            }
            LaunchedEffect(snapshot.menuOpen, snapshot.browserOpen) {
                if (snapshot.menuOpen || snapshot.browserOpen) {
                    setupSheetOpen = false
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
            val activePageLabel = if (activeLayout != null && activeLayout.pages.size > 1) {
                activeLayout.page(keyboardPageIdx).label
            } else {
                null
            }
            LaunchedEffect(activeLayout?.id) {
                keyboardPageIdx = 0
            }
            val showSetupPanel = setupSheetOpen || (!snapshot.uiActive && snapshot.coreId == "setup")

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
                    activePageLabel = activePageLabel,
                    canTogglePreviewLayout = canTogglePreview,
                    setupOpen = showSetupPanel,
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
                    onCycleKeyboardPage = {
                        if (activeLayout != null && activeLayout.pages.isNotEmpty()) {
                            keyboardPageIdx = (keyboardPageIdx + 1) % activeLayout.pages.size
                        }
                    },
                    onCyclePreviewKeyboard = {
                        if (layouts.isNotEmpty()) {
                            previewLayoutIdx = (previewLayoutIdx + 1) % layouts.size
                        }
                    },
                    onToggleSetup = {
                        if (snapshot.coreId != "setup") {
                            if (!setupSheetOpen && !snapshot.inLauncher && snapshot.coreId.isNotBlank()) {
                                selectedSetupMachineId = RomCatalog.specForCoreId(snapshot.coreId).id
                            }
                            setupSheetOpen = !setupSheetOpen
                        }
                    },
                )

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                ) {
                    val dataRoot = remember { czxRoot() }
                    val refreshNonce = refreshNonceState.intValue
                    val activeReport = importReportState.value
                    if (showSetupPanel) {
                        SetupPanel(
                            dataRoot = dataRoot,
                            selectedMachineId = selectedSetupMachineId,
                            onSelectMachine = { m -> selectedSetupMachineId = m.id },
                            refreshNonce = refreshNonce,
                            importReport = activeReport,
                            onDismissReport = { importReportState.value = null },
                            canReloadCore = !snapshot.inLauncher && snapshot.coreId != "setup",
                            showCloseButton = setupSheetOpen && snapshot.coreId != "setup",
                            onReloadCore = {
                                setupSheetOpen = false
                                CzxNative.nativeSendHostCommand(HostCommands.RELOAD_CORE)
                            },
                            onClose = { setupSheetOpen = false },
                            onPickSingleRom = { machine, slot ->
                                pendingSingleSlot = machine to slot
                                pickSingleRomLauncher.launch(arrayOf("*/*"))
                            },
                            onDeleteRom = { machine, slot ->
                                if (RomInstaller.deleteSlotRom(dataRoot, machine, slot)) {
                                    refreshNonceState.intValue++
                                    Toast.makeText(this@MainActivity, "${slot.canonical} を削除しました", Toast.LENGTH_SHORT).show()
                                }
                            },
                            onPickZip = { scopeMachine ->
                                pendingZipMachine = scopeMachine
                                pickZipLauncher.launch(
                                    arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream", "*/*")
                                )
                            },
                            onPickMultipleRoms = { machine ->
                                pendingMultiMachine = machine
                                pickMultiRomLauncher.launch(arrayOf("*/*"))
                            },
                            onAddMedia = { machine ->
                                pendingMediaMachine = machine
                                pickMediaLauncher.launch(arrayOf("*/*"))
                            },
                            onDeleteMedia = { file ->
                                if (file.delete()) {
                                    refreshNonceState.intValue++
                                    Toast.makeText(this@MainActivity, "${file.name} を削除しました", Toast.LENGTH_SHORT).show()
                                }
                            },
                            modifier = Modifier.fillMaxSize(),
                        )
                    } else if (snapshot.uiActive) {
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
                                        pageIndex = keyboardPageIdx,
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
                                    pageIndex = keyboardPageIdx,
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

    private fun seedBluemsxOpenData(dstRoot: File) {
        val hasCbios = File(dstRoot, "Machines/MSX2+ - C-BIOS/cbios_main_msx2+.rom").isFile
        val hasDb = File(dstRoot, "Databases/msxromdb.xml").isFile
        if (hasCbios && hasDb) return
        copyAssetTree("bluemsx-data/Databases", File(dstRoot, "Databases"))
        copyAssetTree("bluemsx-data/Machines", File(dstRoot, "Machines"))
    }

    private fun copyAssetTree(assetPath: String, dst: File) {
        val children = runCatching { assets.list(assetPath) }.getOrNull()
        if (children != null && children.isNotEmpty()) {
            dst.mkdirs()
            children.forEach { name ->
                copyAssetTree("$assetPath/$name", File(dst, name))
            }
            return
        }
        if (dst.exists()) return
        runCatching {
            assets.open(assetPath).use { input ->
                dst.parentFile?.mkdirs()
                dst.outputStream().use { output -> input.copyTo(output) }
            }
        }
    }
}
