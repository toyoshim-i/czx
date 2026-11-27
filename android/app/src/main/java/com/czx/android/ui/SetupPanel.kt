package com.czx.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.czx.android.setup.ImportReport
import com.czx.android.setup.MachineBootMode
import com.czx.android.setup.MachineRomSpec
import com.czx.android.setup.RomCatalog
import com.czx.android.setup.RomInstaller
import com.czx.android.setup.RomSlot
import java.io.File

@Composable
fun SetupPanel(
    dataRoot: File,
    selectedMachineId: String,
    onSelectMachine: (MachineRomSpec) -> Unit,
    refreshNonce: Int,
    importReport: ImportReport?,
    onDismissReport: () -> Unit,
    canReloadCore: Boolean,
    showCloseButton: Boolean,
    onReloadCore: () -> Unit,
    onClose: () -> Unit,
    onPickSingleRom: (MachineRomSpec, RomSlot) -> Unit,
    onDeleteRom: (MachineRomSpec, RomSlot) -> Unit,
    onPickZip: (MachineRomSpec?) -> Unit,
    onPickMultipleRoms: (MachineRomSpec) -> Unit,
    onAddMedia: (MachineRomSpec) -> Unit,
    onDeleteMedia: (File) -> Unit,
    modifier: Modifier = Modifier,
) {
    var activeTab by remember { mutableIntStateOf(0) } // 0=ROM, 1=Media, 2=Guide
    val machine = remember(selectedMachineId) {
        RomCatalog.machines.firstOrNull { it.id == selectedMachineId } ?: RomCatalog.machines.first()
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Color(0xFF10151E))
            .padding(horizontal = 8.dp, vertical = 6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        // Top header / sub-tabs
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            SubTabChip("ROM管理", activeTab == 0, Color(0xFF58D4FF)) { activeTab = 0 }
            SubTabChip("メディア", activeTab == 1, Color(0xFF8BE28B)) { activeTab = 1 }
            SubTabChip("ガイド", activeTab == 2, Color(0xFFB98EFF)) { activeTab = 2 }
            Spacer(modifier = Modifier.weight(1f))
            if (canReloadCore) {
                ActionButton("再起動して試す", Color(0xFF4AF2A1), onClick = onReloadCore)
            }
            if (showCloseButton) {
                ActionButton("閉じる", Color(0xFFFF8A80), onClick = onClose)
            }
        }

        if (importReport != null) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF1A3348))
                    .border(1.dp, Color(0xFF58D4FF), RoundedCornerShape(8.dp))
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = importReport.formatSummary(),
                    color = Color(0xFFEAF4FF),
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f),
                )
                Spacer(modifier = Modifier.width(8.dp))
                ActionButton("OK", Color(0xFF58D4FF), onClick = onDismissReport)
            }
        }

        if (activeTab == 0 || activeTab == 1) {
            val mediaMachines = remember(activeTab) {
                if (activeTab == 1) RomCatalog.machines.filter { it.mediaSubDir.isNotEmpty() }
                else RomCatalog.machines
            }
            val chipRowState = rememberLazyListState()
            LaunchedEffect(machine.id, activeTab) {
                val idx = mediaMachines.indexOfFirst { it.id == machine.id }
                if (idx >= 0) {
                    chipRowState.animateScrollToItem(idx)
                }
            }
            LazyRow(
                state = chipRowState,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                contentPadding = PaddingValues(vertical = 2.dp),
            ) {
                items(mediaMachines, key = { it.id }) { m ->
                    val (reqFound, reqTotal, optFound) = remember(m.id, refreshNonce) {
                        RomInstaller.machineStatusSummary(dataRoot, m)
                    }
                    val dotColor = when {
                        reqTotal > 0 && reqFound >= reqTotal -> Color(0xFF4AF2A1)
                        reqTotal > 0 -> Color(0xFFFFB74D)
                        optFound > 0 -> Color(0xFF4AF2A1)
                        m.bootMode == MachineBootMode.BUILTIN_BIOS -> Color(0xFF58D4FF)
                        else -> Color(0xFF6B778C)
                    }
                    val selected = (m.id == machine.id)
                    Row(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (selected) Color(0xFF273950) else Color(0xFF1A202C))
                            .border(
                                1.dp,
                                if (selected) Color(0xFF58D4FF) else Color(0xFF2E3849),
                                RoundedCornerShape(8.dp),
                            )
                            .clickable { onSelectMachine(m) }
                            .padding(horizontal = 9.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(5.dp),
                    ) {
                        Box(
                            modifier = Modifier
                                .size(7.dp)
                                .clip(CircleShape)
                                .background(dotColor)
                        )
                        Text(
                            text = m.title,
                            color = if (selected) Color.White else Color(0xFFC8D4E6),
                            fontSize = 11.sp,
                            fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
                        )
                    }
                }
            }
        }

        when (activeTab) {
            0 -> RomSetupContent(
                dataRoot = dataRoot,
                machine = machine,
                refreshNonce = refreshNonce,
                onPickSingleRom = onPickSingleRom,
                onDeleteRom = onDeleteRom,
                onPickZip = onPickZip,
                onPickMultipleRoms = onPickMultipleRoms,
                modifier = Modifier.weight(1f),
            )
            1 -> MediaSetupContent(
                dataRoot = dataRoot,
                machine = machine,
                refreshNonce = refreshNonce,
                onAddMedia = onAddMedia,
                onDeleteMedia = onDeleteMedia,
                modifier = Modifier.weight(1f),
            )
            else -> GuideContent(modifier = Modifier.weight(1f))
        }
    }
}

@Composable
private fun RomSetupContent(
    dataRoot: File,
    machine: MachineRomSpec,
    refreshNonce: Int,
    onPickSingleRom: (MachineRomSpec, RomSlot) -> Unit,
    onDeleteRom: (MachineRomSpec, RomSlot) -> Unit,
    onPickZip: (MachineRomSpec?) -> Unit,
    onPickMultipleRoms: (MachineRomSpec) -> Unit,
    modifier: Modifier = Modifier,
) {
    val (reqFound, reqTotal, optFound) = remember(machine.id, refreshNonce) {
        RomInstaller.machineStatusSummary(dataRoot, machine)
    }

    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        // Machine card + bulk ZIP actions
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(Color(0xFF171D28))
                .border(1.dp, Color(0xFF2B3547), RoundedCornerShape(8.dp))
                .padding(8.dp),
            verticalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "${machine.title}  (roms/${machine.romSubDir}/)",
                    color = Color.White,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                val statusText = when {
                    reqTotal > 0 && reqFound >= reqTotal -> "必須ROM完備 ($reqFound/$reqTotal)"
                    reqTotal > 0 -> "必須ROM不足 ($reqFound/$reqTotal)"
                    machine.bootMode == MachineBootMode.BUILTIN_BIOS -> "単体起動OK (+${optFound})"
                    else -> "任意 ($optFound)"
                }
                val statusColor = when {
                    reqTotal > 0 && reqFound < reqTotal -> Color(0xFFFFB74D)
                    else -> Color(0xFF4AF2A1)
                }
                Text(
                    text = statusText,
                    color = statusColor,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                )
            }
            Text(
                text = machine.summary,
                color = Color(0xFFB9C6D8),
                fontSize = 11.sp,
                lineHeight = 14.sp,
            )
            if (machine.warning.isNotEmpty()) {
                Text(
                    text = machine.warning,
                    color = Color(0xFFFFCC66),
                    fontSize = 10.sp,
                    lineHeight = 13.sp,
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                ActionButton("ZIPから自動配置", Color(0xFF58D4FF)) { onPickZip(machine) }
                ActionButton("全機種ZIP一括", Color(0xFF4AF2A1)) { onPickZip(null) }
                ActionButton("複数ファイル選択", Color(0xFFD1A4FF)) { onPickMultipleRoms(machine) }
            }
        }

        if (machine.slots.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF151B24))
                    .padding(14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "この機種に必須のシステム ROM はありません。\n「メディア」タブからゲームファイル (.zip/.exe 等) を追加して起動できます。",
                    color = Color(0xFF9FB0C7),
                    fontSize = 12.sp,
                    lineHeight = 17.sp,
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(5.dp),
            ) {
                items(machine.slots, key = { it.id }) { slot ->
                    val installed = remember(machine.id, slot.id, refreshNonce) {
                        RomInstaller.findInstalledFile(dataRoot, machine, slot)
                    }
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (installed != null) Color(0xFF142420) else Color(0xFF171C26))
                            .border(
                                1.dp,
                                if (installed != null) Color(0xFF2B6F53) else Color(0xFF2A3342),
                                RoundedCornerShape(8.dp),
                            )
                            .padding(horizontal = 9.dp, vertical = 7.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(5.dp),
                            ) {
                                Text(
                                    text = slot.canonical,
                                    color = Color.White,
                                    fontSize = 12.sp,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                )
                                val tag = when {
                                    slot.required && slot.requiredNote.isNotEmpty() -> slot.requiredNote
                                    slot.required -> "必須"
                                    slot.requiredNote.isNotEmpty() -> slot.requiredNote
                                    else -> "任意"
                                }
                                val tagBg = if (slot.required) Color(0xFF5A3318) else Color(0xFF253042)
                                val tagFg = if (slot.required) Color(0xFFFFC168) else Color(0xFF9DB2CC)
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(tagBg)
                                        .padding(horizontal = 5.dp, vertical = 1.dp)
                                ) {
                                    Text(text = tag, color = tagFg, fontSize = 9.sp, fontWeight = FontWeight.Bold)
                                }
                            }
                            val subText = if (installed != null) {
                                "✓ 配置済: ${installed.name} (${installed.length() / 1024}KB)"
                            } else {
                                buildString {
                                    append(slot.description)
                                    if (slot.aliases.isNotEmpty()) {
                                        append(" / 別名: ${slot.aliases.take(3).joinToString(", ")}")
                                    }
                                }
                            }
                            Text(
                                text = subText,
                                color = if (installed != null) Color(0xFF7BE8AC) else Color(0xFF93A4BC),
                                fontSize = 10.sp,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis,
                            )
                        }
                        Spacer(modifier = Modifier.width(6.dp))
                        if (installed != null) {
                            ActionButton("削除", Color(0xFFFF7A7A)) { onDeleteRom(machine, slot) }
                            Spacer(modifier = Modifier.width(5.dp))
                        }
                        ActionButton(if (installed != null) "差替" else "配置", Color(0xFF58D4FF)) {
                            onPickSingleRom(machine, slot)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun MediaSetupContent(
    dataRoot: File,
    machine: MachineRomSpec,
    refreshNonce: Int,
    onAddMedia: (MachineRomSpec) -> Unit,
    onDeleteMedia: (File) -> Unit,
    modifier: Modifier = Modifier,
) {
    val active = if (machine.mediaSubDir.isEmpty()) RomCatalog.machines.first() else machine
    val files = remember(active.id, refreshNonce) {
        RomInstaller.listMediaFiles(dataRoot, active)
    }

    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(Color(0xFF171D28))
                .border(1.dp, Color(0xFF2B3547), RoundedCornerShape(8.dp))
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "${active.title}  (media/${active.mediaSubDir}/)",
                    color = Color.White,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                )
                Text(
                    text = "対応形式: ${active.mediaExts}",
                    color = Color(0xFFA7B8CE),
                    fontSize = 10.sp,
                )
            }
            Spacer(modifier = Modifier.width(8.dp))
            ActionButton("+ メディア追加", Color(0xFF8BE28B)) { onAddMedia(active) }
        }

        if (files.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color(0xFF151B24))
                    .padding(14.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "media/${active.mediaSubDir}/ にファイルがありません。\n右上の「+ メディア追加」からディスク・テープ・ROM イメージを取り込めます。",
                    color = Color(0xFF9FB0C7),
                    fontSize = 12.sp,
                    lineHeight = 17.sp,
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                items(files, key = { it.absolutePath }) { f ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .background(Color(0xFF171C26))
                            .border(1.dp, Color(0xFF2A3342), RoundedCornerShape(8.dp))
                            .padding(horizontal = 9.dp, vertical = 7.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = "${f.name} (${(f.length() / 1024).coerceAtLeast(1)}KB)",
                            color = Color(0xFFDDE7F5),
                            fontSize = 11.sp,
                            modifier = Modifier.weight(1f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        ActionButton("削除", Color(0xFFFF7A7A)) { onDeleteMedia(f) }
                    }
                }
            }
        }
    }
}

@Composable
private fun GuideContent(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF151B24))
            .verticalScroll(rememberScrollState())
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        GuideBlock(
            title = "1. すぐ起動できる機種 vs ROM が必要な機種",
            body = "• 単体起動OK: PC-8801 (擬似BIOS), PC-9801 (HLE BIOS), MSX (C-BIOS 同梱), PC-G850 (互換BASIC), IBM PC / DOS\n" +
                "• ROM必須: Apple II, Commodore 64, MZ-700, FM-7/77AV, X68000\n" +
                "• 実機 ROM を「ROM管理」タブから配置すると互換性や漢字・FD 機能が拡張されます。",
        )
        GuideBlock(
            title = "2. ROM の簡単な入れ方",
            body = "• 「ZIPから自動配置」または「全機種ZIP一括」で手持ちの BIOS ZIP を選ぶと、サブフォルダや大文字・小文字の違いを自動で吸収して正しいパスに保存します。\n" +
                "• 1ファイルずつ配置したい場合は各行の「配置」ボタンを押して選んでください。",
        )
        GuideBlock(
            title = "3. 操作方法（縦持ち UI）",
            body = "• ☰ MENU: システムメニュー開閉（メニュー・ランチャー中は下部が大型ゲームパッドになり A=決定 / B=戻る / X=上階層 で操作可能）\n" +
                "• PAR / 4:3 / FULL: 画面アスペクト・スケーリング切替\n" +
                "• MAIN / NUM: 実機キーボードのメインキーとテンキー/編集キーを切替\n" +
                "• KBD+PAD / KBD / PAD: 下部表示モード切替\n" +
                "• SETUP: いつでもこの画面（ROM・メディア追加・ガイド）を開きます",
        )
        GuideBlock(
            title = "4. Roland MT-32 / .d88 / .m3u 機能",
            body = "• PC-9801 / X68000 / DOS では「Roland MT-32 / CM-32L」スロットに ROM を配置し、メニューから MT-32 音源を有効にできます。\n" +
                "• PC-88 / PC-98 / FM-7 の .d88・.d77 マルチディスクや .m3u プレイリストはシステムメニューの Drive 1/2 から選択できます。",
        )
        GuideBlock(
            title = "5. 詳細ドキュメント",
            body = "CZX は著作権付き ROM を同梱していません。仕様・対応フォーマットの詳細は https://github.com/toyoshim-i/czx をご参照ください。",
        )
    }
}

@Composable
private fun GuideBlock(title: String, body: String) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(text = title, color = Color(0xFF7AD8FF), fontSize = 12.sp, fontWeight = FontWeight.Bold)
        Text(text = body, color = Color(0xFFD2DCEB), fontSize = 11.sp, lineHeight = 16.sp)
    }
}

@Composable
private fun SubTabChip(label: String, selected: Boolean, accent: Color, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(7.dp))
            .background(if (selected) accent.copy(alpha = 0.24f) else Color(0xFF1B222F))
            .border(1.dp, if (selected) accent else Color(0xFF334055), RoundedCornerShape(7.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(
            text = label,
            color = if (selected) Color.White else Color(0xFFB5C2D4),
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
        )
    }
}

@Composable
private fun ActionButton(label: String, accent: Color, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(accent.copy(alpha = 0.18f))
            .border(1.dp, accent.copy(alpha = 0.75f), RoundedCornerShape(6.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 5.dp),
    ) {
        Text(
            text = label,
            color = Color.White,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            maxLines = 1,
        )
    }
}
