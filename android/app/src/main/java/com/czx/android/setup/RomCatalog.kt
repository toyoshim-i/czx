package com.czx.android.setup

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.Locale
import java.util.zip.ZipInputStream

enum class MachineBootMode {
    REQUIRES_ROMS,
    BUILTIN_BIOS,
    OPTIONAL_SHARED,
}

data class RomSlot(
    val id: String,
    val canonical: String,
    val relDir: String = "",
    val required: Boolean = false,
    val requiredNote: String = "",
    val description: String = "",
    val aliases: List<String> = emptyList(),
)

data class MachineRomSpec(
    val id: String,
    val title: String,
    val coreIds: List<String>,
    val romSubDir: String,
    val mediaSubDir: String,
    val bootMode: MachineBootMode,
    val caseInsensitive: Boolean,
    val summary: String,
    val mediaExts: String,
    val warning: String = "",
    val slots: List<RomSlot>,
)

data class ImportReport(
    val placed: List<String> = emptyList(),
    val warnings: List<String> = emptyList(),
    val skippedCount: Int = 0,
) {
    fun formatSummary(): String {
        val parts = mutableListOf<String>()
        if (placed.isNotEmpty()) {
            parts.add("配置: ${placed.size}件 (${placed.take(4).joinToString(", ")}${if (placed.size > 4) ", ..." else ""})")
        } else {
            parts.add("一致するROMが見つかりませんでした")
        }
        if (skippedCount > 0) parts.add("スキップ: ${skippedCount}件")
        if (warnings.isNotEmpty()) parts.add(warnings.joinToString(" / "))
        return parts.joinToString("\n")
    }
}

object RomCatalog {
    val machines: List<MachineRomSpec> = listOf(
        MachineRomSpec(
            id = "apple2",
            title = "Apple II",
            coreIds = listOf("applewin"),
            romSubDir = "apple2",
            mediaSubDir = "apple2",
            bootMode = MachineBootMode.REQUIRES_ROMS,
            caseInsensitive = true,
            summary = "Enhanced Apple IIe 構成には 3 つの必須 ROM が必要です。",
            mediaExts = ".dsk .do .po .nib .woz .2mg .hdv .zip",
            slots = listOf(
                RomSlot("a2_main", "Apple2e_Enhanced.rom", required = true, description = "Enhanced Apple IIe メイン ROM"),
                RomSlot("a2_vid", "Apple2e_Enhanced_Video.rom", required = true, description = "キャラクタ / ビデオ ROM"),
                RomSlot("a2_disk", "DISK2.rom", required = true, description = "Disk II コントローラ ROM", aliases = listOf("DISK2.BIN")),
            ),
        ),
        MachineRomSpec(
            id = "c64",
            title = "Commodore 64",
            coreIds = listOf("vice_x64", "vice_x64sc"),
            romSubDir = "vice",
            mediaSubDir = "c64",
            bootMode = MachineBootMode.REQUIRES_ROMS,
            caseInsensitive = false,
            summary = "VICE 用システム ROM 4 個を小文字・専用サブディレクトリに配置します（個別・ZIP 配置で自動正規化）。",
            mediaExts = ".d64 .g64 .x64 .d71 .d81 .prg .crt .t64 .tap .zip",
            slots = listOf(
                RomSlot("c64_kernal", "kernal-901227-03.bin", relDir = "C64", required = true, description = "C64 Kernal ROM (C64/)", aliases = listOf("kernal", "kernal.bin", "kernal-901227-03")),
                RomSlot("c64_basic", "basic-901226-01.bin", relDir = "C64", required = true, description = "C64 BASIC ROM (C64/)", aliases = listOf("basic", "basic.bin", "basic-901226-01")),
                RomSlot("c64_chargen", "chargen-901225-01.bin", relDir = "C64", required = true, description = "C64 Chargen ROM (C64/)", aliases = listOf("chargen", "chargen.bin", "chargen-901225-01")),
                RomSlot("c64_1541", "dos1541ii-251968-03.bin", relDir = "DRIVES", required = true, description = "1541-II Drive ROM (DRIVES/)", aliases = listOf("dos1541ii", "dos1541", "dos1541-ii.rom", "1541-ii.bin")),
            ),
        ),
        MachineRomSpec(
            id = "quasi88",
            title = "PC-8801",
            coreIds = listOf("quasi88"),
            romSubDir = "quasi88",
            mediaSubDir = "pc88",
            bootMode = MachineBootMode.BUILTIN_BIOS,
            caseInsensitive = true,
            summary = "内蔵疑似 BIOS でそのまま起動できます。実機 ROM があると N88-BASIC・漢字・多くの商用ソフトが動作します。",
            mediaExts = ".d88 .88d .t88 .cmt .m3u",
            slots = listOf(
                RomSlot("q88_n88", "N88.ROM", description = "N88-BASIC メイン ROM"),
                RomSlot("q88_n88_0", "N88_0.ROM", description = "第4 ROM バンク 0"),
                RomSlot("q88_n88_1", "N88_1.ROM", description = "第4 ROM バンク 1"),
                RomSlot("q88_n88_2", "N88_2.ROM", description = "第4 ROM バンク 2"),
                RomSlot("q88_n88_3", "N88_3.ROM", description = "第4 ROM バンク 3"),
                RomSlot("q88_n88n", "N88N.ROM", description = "N-BASIC モード ROM"),
                RomSlot("q88_disk", "DISK.ROM", description = "5インチ FD サブシステム ROM"),
                RomSlot("q88_font", "FONT.ROM", description = "ANK / グラフィック文字 ROM"),
                RomSlot("q88_kanji1", "KANJI1.ROM", description = "JIS 第一水準漢字 ROM"),
                RomSlot("q88_kanji2", "KANJI2.ROM", description = "JIS 第二水準漢字 ROM"),
            ),
        ),
        MachineRomSpec(
            id = "np2kai",
            title = "PC-9801",
            coreIds = listOf("np2kai"),
            romSubDir = "np2kai",
            mediaSubDir = "pc98",
            bootMode = MachineBootMode.BUILTIN_BIOS,
            caseInsensitive = false,
            summary = "内蔵 HLE BIOS で起動可能。実機 ROM は小文字ファイル名で配置します（配置時に自動小文字化）。",
            mediaExts = ".d88 .fdi .hdm .nhd .hdi .thd .nfd .98d .m3u",
            warning = "注意: d8000.rom を入れると FD のみの起動時にハングする場合があるため自動配置からは除外しています。",
            slots = listOf(
                RomSlot("np2_bios", "bios.rom", description = "PC-98 システム BIOS（小文字）"),
                RomSlot("np2_itf", "itf.rom", description = "ITF イニシャルテスト ROM"),
                RomSlot("np2_sound", "sound.rom", description = "PC-9801-26K / 86 FM 音源 BIOS"),
                RomSlot("np2_font", "font.rom", description = "漢字・ANK フォント (または font.bmp)", aliases = listOf("font.bmp")),
                RomSlot("np2_9821", "bios9821.rom", description = "PC-9821 拡張 BIOS (任意)"),
            ),
        ),
        MachineRomSpec(
            id = "bluemsx",
            title = "MSX / MSX2+",
            coreIds = listOf("bluemsx"),
            romSubDir = "bluemsx",
            mediaSubDir = "msx",
            bootMode = MachineBootMode.BUILTIN_BIOS,
            caseInsensitive = true,
            summary = "オープンソース C-BIOS を初期展開済みでカートリッジはすぐ動きます。Shared Roms へ実機 ROM を入れると MSX-DOS / ディスク起動が可能になります。",
            mediaExts = ".rom .mx1 .mx2 .dsk .cas .m3u",
            slots = listOf(
                RomSlot("msx_2p", "MSX2P.rom", relDir = "Machines/Shared Roms", description = "MSX2+ メイン BIOS"),
                RomSlot("msx_2pext", "MSX2PEXT.rom", relDir = "Machines/Shared Roms", description = "MSX2+ サブ / ExtROM"),
                RomSlot("msx_2", "MSX2.rom", relDir = "Machines/Shared Roms", description = "MSX2 メイン BIOS"),
                RomSlot("msx_2ext", "MSX2EXT.rom", relDir = "Machines/Shared Roms", description = "MSX2 サブ / ExtROM"),
                RomSlot("msx_1", "MSX.rom", relDir = "Machines/Shared Roms", description = "MSX1 単体 BIOS"),
                RomSlot("msx_pdisk", "PANASONICDISK.rom", relDir = "Machines/Shared Roms", description = "Panasonic Disk BIOS (.dsk 用)"),
                RomSlot("msx_phdisk", "PHILIPSDISK.rom", relDir = "Machines/Shared Roms", description = "Philips Disk BIOS (.dsk 用)"),
                RomSlot("msx_music", "MSX2PMUS.rom", relDir = "Machines/Shared Roms", description = "MSX-MUSIC (FM-PAC) ROM"),
                RomSlot("msx_xbasic", "XBASIC2.rom", relDir = "Machines/Shared Roms", description = "MSX2+ XBASIC2"),
                RomSlot("msx_kanji", "KANJI.rom", relDir = "Machines/Shared Roms", description = "漢字 ROM", aliases = listOf("MSXKANJI.rom")),
                RomSlot("msx_moon", "MOONSOUND.rom", relDir = "Machines/Shared Roms", description = "OPL4 MoonSound BIOS"),
            ),
        ),
        MachineRomSpec(
            id = "g800",
            title = "PC-G850 / E200",
            coreIds = listOf("g800"),
            romSubDir = "g800",
            mediaSubDir = "g800",
            bootMode = MachineBootMode.BUILTIN_BIOS,
            caseInsensitive = true,
            summary = "内蔵互換 BASIC / IOCS / モニタで単体起動できます。実機 ROM (base.bin 等) の併用も可能です。",
            mediaExts = ".ihx .hex .bas .txt",
            slots = listOf(
                RomSlot("g8_base", "base.bin", description = "実機ベース ROM (または base.txt)", aliases = listOf("base.txt")),
                RomSlot("g8_rom0", "rom00.bin", description = "拡張バンク 0 (または rom00.txt)", aliases = listOf("rom00.txt")),
                RomSlot("g8_rom1", "rom01.bin", description = "拡張バンク 1", aliases = listOf("rom01.txt")),
            ),
        ),
        MachineRomSpec(
            id = "emuz700",
            title = "SHARP MZ-700",
            coreIds = listOf("emuz700"),
            romSubDir = "mz700",
            mediaSubDir = "mz700",
            bootMode = MachineBootMode.REQUIRES_ROMS,
            caseInsensitive = true,
            summary = "IPL ROM（モニタ）と FONT ROM の 2 つが必要です（別名ファイルの配置で自動認識）。",
            mediaExts = ".mzt .mzf .m12 .t77 .q20 .d88 .2d .wav",
            slots = listOf(
                RomSlot(
                    "mz_ipl",
                    "IPL.ROM",
                    required = true,
                    description = "モニタ / IPL ROM",
                    aliases = listOf("1Z-009A.ROM", "1Z-009B.ROM", "1Z009A.ROM", "1Z009B.ROM", "MZ700IPL.ROM", "NEWMON7.ROM"),
                ),
                RomSlot(
                    "mz_font",
                    "FONT.ROM",
                    required = true,
                    description = "キャラクタジェネレータ FONT ROM",
                    aliases = listOf("MZ700FON.JP", "MZ700FON.DAT", "MZ700FON.ROM", "CGROM.ROM", "MZ700FONT.ROM"),
                ),
            ),
        ),
        MachineRomSpec(
            id = "fm7",
            title = "FM-7 / FM77AV",
            coreIds = listOf("efm7", "efm77av", "efm77av40"),
            romSubDir = "fm7",
            mediaSubDir = "fm7",
            bootMode = MachineBootMode.REQUIRES_ROMS,
            caseInsensitive = true,
            summary = "FM-7 では SUBSYS_C + FBASIC30 + BOOT_*、FM77AV/AV40 では加えて INITIATE + SUBSYSCG が必要です。",
            mediaExts = ".d77 .77d .2d .t77",
            slots = listOf(
                RomSlot("fm_subc", "SUBSYS_C.ROM", required = true, description = "200 ライン・サブシステム ROM"),
                RomSlot("fm_fbas", "FBASIC30.ROM", required = true, description = "F-BASIC V3.0 ROM", aliases = listOf("FBASIC300.ROM", "FBASIC301.ROM", "FBASIC302.ROM")),
                RomSlot("fm_boot", "BOOT_BAS.ROM", required = true, requiredNote = "FM-7 必須", description = "ブート ROM (BASIC / DOS)", aliases = listOf("BOOT_DOS.ROM", "BOOT_TL11_11.ROM")),
                RomSlot("fm_init", "INITIATE.ROM", requiredNote = "77AV/40 必須", description = "FM77AV イニシエート ROM"),
                RomSlot("fm_cg", "SUBSYSCG.ROM", requiredNote = "77AV/40 必須", description = "FM77AV サブ CG ROM"),
                RomSlot("fm_kanji", "KANJI.ROM", description = "JIS 漢字 ROM (任意)"),
                RomSlot("fm_suba", "SUBSYS_A.ROM", description = "400 ライン・サブシステム ROM A (AV40)", aliases = listOf("SUBSYS_B.ROM", "EXTSUB.ROM", "DICROM.ROM")),
            ),
        ),
        MachineRomSpec(
            id = "msx68k",
            title = "SHARP X68000",
            coreIds = listOf("msx68k"),
            romSubDir = "x68000",
            mediaSubDir = "x68000",
            bootMode = MachineBootMode.REQUIRES_ROMS,
            caseInsensitive = true,
            summary = "IPLROM.DAT（または IPLROM30.DAT）と CGROM.DAT が必須です。",
            mediaExts = ".dim .xdf .hdm .2hd .hdf .hds .m3u",
            slots = listOf(
                RomSlot("x68_ipl", "IPLROM.DAT", required = true, description = "IPL BIOS (128KB)", aliases = listOf("IPLROM30.DAT")),
                RomSlot("x68_cg", "CGROM.DAT", required = true, description = "キャラクタジェネレータ ROM"),
                RomSlot("x68_scsi", "SCSIINROM.DAT", description = "内蔵 / 拡張 SCSI BIOS (任意)", aliases = listOf("SCSIEXROM.DAT", "IPLROMXV.DAT", "IPLROMCO.DAT")),
            ),
        ),
        MachineRomSpec(
            id = "dosbox_pure",
            title = "IBM PC / DOS",
            coreIds = listOf("dosbox_pure"),
            romSubDir = "dos",
            mediaSubDir = "dos",
            bootMode = MachineBootMode.BUILTIN_BIOS,
            caseInsensitive = true,
            summary = "外部 ROM 不要で起動し、メディアタブから追加した .zip / .exe / .ima 等をそのまま起動できます。",
            mediaExts = ".zip .dosz .exe .com .bat .ima .img .iso .chd",
            slots = emptyList(),
        ),
        MachineRomSpec(
            id = "mt32",
            title = "Roland MT-32 / CM-32L",
            coreIds = emptyList(),
            romSubDir = "mt32",
            mediaSubDir = "",
            bootMode = MachineBootMode.OPTIONAL_SHARED,
            caseInsensitive = true,
            summary = "PC-98 / X68000 / DOS で共有される内蔵 munt シンセ用 ROM（任意、CM-32L 優先）。システムメニューで有効化します。",
            mediaExts = "共通 MIDI 音源",
            slots = listOf(
                RomSlot("mt_ctrl", "CM32L_CONTROL.ROM", description = "CM-32L (または MT32_CONTROL.ROM)", aliases = listOf("MT32_CONTROL.ROM")),
                RomSlot("mt_pcm", "CM32L_PCM.ROM", description = "CM-32L 波形 ROM (または MT32_PCM.ROM)", aliases = listOf("MT32_PCM.ROM")),
            ),
        ),
    )

    fun specForCoreId(coreId: String): MachineRomSpec {
        if (coreId.isNotBlank()) {
            machines.firstOrNull { coreId in it.coreIds }?.let { return it }
        }
        return machines.first()
    }
}

object RomInstaller {
    private const val MAX_ENTRY_BYTES = 32L * 1024L * 1024L

    fun machineStatusSummary(dataRoot: File, machine: MachineRomSpec): Triple<Int, Int, Int> {
        var reqFound = 0
        var reqTotal = 0
        var optFound = 0
        machine.slots.forEach { slot ->
            val ok = findInstalledFile(dataRoot, machine, slot) != null
            if (slot.required) {
                reqTotal++
                if (ok) reqFound++
            } else if (ok) {
                optFound++
            }
        }
        return Triple(reqFound, reqTotal, optFound)
    }

    fun slotDir(dataRoot: File, machine: MachineRomSpec, slot: RomSlot): File {
        val base = File(File(dataRoot, "roms"), machine.romSubDir)
        return if (slot.relDir.isEmpty()) base else File(base, slot.relDir)
    }

    fun findInstalledFile(dataRoot: File, machine: MachineRomSpec, slot: RomSlot): File? {
        val dir = slotDir(dataRoot, machine, slot)
        if (!dir.isDirectory) return null
        val names = listOf(slot.canonical) + slot.aliases
        for (name in names) {
            val direct = File(dir, name)
            if (direct.isFile && direct.length() > 0L) return direct
        }
        if (!machine.caseInsensitive) return null
        val wanted = names.map { it.lowercase(Locale.ROOT) }.toSet()
        return dir.listFiles()?.firstOrNull {
            it.isFile && it.length() > 0L && it.name.lowercase(Locale.ROOT) in wanted
        }
    }

    fun deleteSlotRom(dataRoot: File, machine: MachineRomSpec, slot: RomSlot): Boolean {
        val dir = slotDir(dataRoot, machine, slot)
        if (!dir.isDirectory) return false
        val names = (listOf(slot.canonical) + slot.aliases).map { it.lowercase(Locale.ROOT) }.toSet()
        var deleted = false
        dir.listFiles()?.forEach { file ->
            if (!file.isFile) return@forEach
            val match = if (machine.caseInsensitive) {
                file.name.lowercase(Locale.ROOT) in names
            } else {
                file.name == slot.canonical || file.name in slot.aliases
            }
            if (match && file.delete()) {
                deleted = true
            }
        }
        return deleted
    }

    fun installSingleRom(
        context: Context,
        uri: Uri,
        dataRoot: File,
        machine: MachineRomSpec,
        slot: RomSlot,
    ): Result<File> = runCatching {
        val dir = slotDir(dataRoot, machine, slot).apply { mkdirs() }
        val pickedName = queryDisplayName(context, uri)
        val targetName = resolveSaveFilename(pickedName, machine, slot)
        val dst = File(dir, targetName)
        context.contentResolver.openInputStream(uri)?.use { input ->
            dst.outputStream().use { out -> input.copyTo(out) }
        } ?: error("ファイルを開けませんでした")
        dst
    }

    fun importFromZip(
        context: Context,
        zipUri: Uri,
        dataRoot: File,
        scopeMachine: MachineRomSpec?,
    ): ImportReport {
        return runCatching {
            context.contentResolver.openInputStream(zipUri)?.use { raw ->
                ZipInputStream(raw.buffered()).use { zis ->
                    importZipStream(zis, dataRoot, scopeMachine)
                }
            } ?: ImportReport(warnings = listOf("ZIP ファイルを開けませんでした"))
        }.getOrElse { err ->
            ImportReport(warnings = listOf("ZIP 読み込み失敗: ${err.message}"))
        }
    }

    fun importZipStream(
        zis: ZipInputStream,
        dataRoot: File,
        scopeMachine: MachineRomSpec?,
    ): ImportReport {
        val candidates = if (scopeMachine != null) listOf(scopeMachine) else RomCatalog.machines
        val placed = mutableListOf<String>()
        val warnings = mutableListOf<String>()
        var skipped = 0
        val buf = ByteArray(16 * 1024)
        while (true) {
            val entry = zis.nextEntry ?: break
            if (entry.isDirectory) {
                zis.closeEntry()
                continue
            }
            val base = entry.name.substringAfterLast('/').substringAfterLast('\\')
            if (base.isEmpty() || base.startsWith(".") || entry.name.startsWith("__MACOSX/")) {
                zis.closeEntry()
                continue
            }
            if (entry.size > MAX_ENTRY_BYTES) {
                skipped++
                zis.closeEntry()
                continue
            }
            val low = base.lowercase(Locale.ROOT)
            if (low == "d8000.rom") {
                warnings.add("d8000.rom は FD 起動ハング防止のため自動スキップしました")
                skipped++
                zis.closeEntry()
                continue
            }
            val match = findMatchingSlot(base, candidates)
            if (match == null) {
                skipped++
                zis.closeEntry()
                continue
            }
            val (m, slot) = match
            val dir = slotDir(dataRoot, m, slot).apply { mkdirs() }
            val dstName = resolveSaveFilename(base, m, slot)
            val dst = File(dir, dstName)
            var total = 0L
            dst.outputStream().use { out ->
                while (true) {
                    val n = zis.read(buf)
                    if (n <= 0) break
                    total += n
                    if (total > MAX_ENTRY_BYTES) break
                    out.write(buf, 0, n)
                }
            }
            placed.add("${m.title}:$dstName")
            zis.closeEntry()
        }
        return ImportReport(placed = placed, warnings = warnings, skippedCount = skipped)
    }

    fun importMultipleUris(
        context: Context,
        uris: List<Uri>,
        dataRoot: File,
        scopeMachine: MachineRomSpec?,
    ): ImportReport {
        val candidates = if (scopeMachine != null) listOf(scopeMachine) else RomCatalog.machines
        val placed = mutableListOf<String>()
        val warnings = mutableListOf<String>()
        var skipped = 0

        uris.forEach { uri ->
            val name = queryDisplayName(context, uri) ?: return@forEach
            val low = name.lowercase(Locale.ROOT)
            if (low.endsWith(".zip")) {
                val r = importFromZip(context, uri, dataRoot, scopeMachine)
                placed.addAll(r.placed)
                warnings.addAll(r.warnings)
                skipped += r.skippedCount
                return@forEach
            }
            if (low == "d8000.rom") {
                warnings.add("d8000.rom は自動スキップしました")
                skipped++
                return@forEach
            }
            val match = findMatchingSlot(name, candidates)
            if (match == null) {
                skipped++
                return@forEach
            }
            val (m, slot) = match
            installSingleRom(context, uri, dataRoot, m, slot)
                .onSuccess { dst -> placed.add("${m.title}:${dst.name}") }
                .onFailure { err -> warnings.add("$name: ${err.message}") }
        }
        return ImportReport(placed = placed, warnings = warnings, skippedCount = skipped)
    }

    fun listMediaFiles(dataRoot: File, machine: MachineRomSpec): List<File> {
        if (machine.mediaSubDir.isEmpty()) return emptyList()
        val dir = File(File(dataRoot, "media"), machine.mediaSubDir)
        if (!dir.isDirectory) return emptyList()
        return dir.listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase(Locale.ROOT) }.orEmpty()
    }

    fun importMediaFiles(
        context: Context,
        uris: List<Uri>,
        dataRoot: File,
        machine: MachineRomSpec,
    ): Int {
        if (machine.mediaSubDir.isEmpty()) return 0
        val dir = File(File(dataRoot, "media"), machine.mediaSubDir).apply { mkdirs() }
        var count = 0
        uris.forEach { uri ->
            val name = queryDisplayName(context, uri)?.substringAfterLast('/') ?: return@forEach
            runCatching {
                context.contentResolver.openInputStream(uri)?.use { input ->
                    File(dir, name).outputStream().use { out -> input.copyTo(out) }
                }
                count++
            }
        }
        return count
    }

    private fun resolveSaveFilename(pickedName: String?, machine: MachineRomSpec, slot: RomSlot): String {
        if (pickedName != null) {
            val lowPicked = pickedName.lowercase(Locale.ROOT)
            val matchedAlias = slot.aliases.firstOrNull { it.lowercase(Locale.ROOT) == lowPicked }
            if (matchedAlias != null) {
                return if (machine.caseInsensitive) matchedAlias else matchedAlias.lowercase(Locale.ROOT)
            }
        }
        return if (machine.caseInsensitive) slot.canonical else slot.canonical.lowercase(Locale.ROOT)
    }

    private fun findMatchingSlot(
        filename: String,
        candidates: List<MachineRomSpec>,
    ): Pair<MachineRomSpec, RomSlot>? {
        val low = filename.lowercase(Locale.ROOT)
        for (m in candidates) {
            for (slot in m.slots) {
                if (slot.canonical.lowercase(Locale.ROOT) == low) return m to slot
                if (slot.aliases.any { it.lowercase(Locale.ROOT) == low }) return m to slot
            }
        }
        return null
    }

    fun queryDisplayName(context: Context, uri: Uri): String? {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (idx >= 0) return c.getString(idx)
            }
        }
        return uri.lastPathSegment?.substringAfterLast('/')
    }
}
