package com.czx.android.setup

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
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

data class KnownRomHash(
    val sha1: String,
    val sizeBytes: Long,
    val machineId: String,
    val slotId: String,
    val saveName: String,
    val label: String,
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

    val knownRomHashes: List<KnownRomHash> = listOf(
        // Apple II (AppleWin)
        KnownRomHash("b8ea90abe135a0031065e01697c4a3a20d51198b", 16384L, "apple2", "a2_main", "Apple2e_Enhanced.rom", "Apple IIe Enhanced System ROM"),
        KnownRomHash("b2b5d87f52693817fc747df087a4aa1ddcdb1f10", 4096L, "apple2", "a2_vid", "Apple2e_Enhanced_Video.rom", "Apple IIe Enhanced Video ROM"),
        KnownRomHash("d4181c9f046aafc3fb326b381baac809d9e38d16", 256L, "apple2", "a2_disk", "DISK2.rom", "Apple II Disk II 16-sector ROM"),

        // Commodore 64 (VICE)
        KnownRomHash("1d503e56df85a62fee696e7618dc5b4e781df1bb", 8192L, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.3 (901227-03)"),
        KnownRomHash("0e2e4ee3f2d41f00bed72f9ab588b83e306fdb13", 8192L, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.2 (901227-02)"),
        KnownRomHash("87cc04d61fc748b82df09856847bb5c2754a2033", 8192L, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.1 (901227-01)"),
        KnownRomHash("79015323128650c742a3694c9429aa91f355905e", 8192L, "c64", "c64_basic", "basic-901226-01.bin", "C64 BASIC V2 (901226-01)"),
        KnownRomHash("adc7c31e18c7c7413d54802ef2f4193da14711aa", 4096L, "c64", "c64_chargen", "chargen-901225-01.bin", "C64 Chargen (901225-01)"),
        KnownRomHash("d3b78c3dbac55f5199f33f3fe0036439811f7fb3", 16384L, "c64", "c64_1541", "dos1541ii-251968-03.bin", "Commodore 1541-II DOS (251968-03)"),
        KnownRomHash("ab16f56989b27d89babe5f89c5a8cb3da71a82f0", 16384L, "c64", "c64_1541", "dos1541ii-251968-03.bin", "Commodore 1541 DOS (325302-01+901229-05)"),

        // PC-8801 (QUASI88)
        KnownRomHash("3518193b8207bdebf22c1380c2db8c554baff329", 32768L, "quasi88", "q88_n88", "N88.ROM", "PC-8801 N88-BASIC ROM (MAME)"),
        KnownRomHash("3b31fc68fa7f47b21c1a1cb027b86b9e87afbfff", 32768L, "quasi88", "q88_n88", "N88.ROM", "PC-8801mkIISR N88-BASIC ROM"),
        KnownRomHash("d1ae642aed4f0584eeb81ff50180db694e5101d4", 8192L, "quasi88", "q88_n88_0", "N88_0.ROM", "PC-8801 4th ROM Bank 0 (MAME)"),
        KnownRomHash("d239c26ad7ac5efac6e947b0e9549b1534aa970d", 8192L, "quasi88", "q88_n88_0", "N88_0.ROM", "PC-8801mkIISR 4th ROM Bank 0"),
        KnownRomHash("4ae4d37409ff99411a623da9f6a44192170a854e", 8192L, "quasi88", "q88_n88_1", "N88_1.ROM", "PC-8801 4th ROM Bank 1 (Libretro)"),
        KnownRomHash("8528eef7946edf6501a6ccb1f416b60c64efac7c", 8192L, "quasi88", "q88_n88_1", "N88_1.ROM", "PC-8801mkIISR 4th ROM Bank 1"),
        KnownRomHash("e94278682ef9e9bbb82201f72c50382748dcea2a", 8192L, "quasi88", "q88_n88_2", "N88_2.ROM", "PC-8801 4th ROM Bank 2 (Libretro)"),
        KnownRomHash("b7c8bcea219b77d9cc3ee0efafe343cc307425d1", 8192L, "quasi88", "q88_n88_2", "N88_2.ROM", "PC-8801mkIISR 4th ROM Bank 2"),
        KnownRomHash("af452aed79b072c4d17985830b7c5dca64d4b412", 8192L, "quasi88", "q88_n88_3", "N88_3.ROM", "PC-8801 4th ROM Bank 3 (Libretro)"),
        KnownRomHash("efce0b51cab9f0da6cf68507757f1245a2867a72", 8192L, "quasi88", "q88_n88_3", "N88_3.ROM", "PC-8801mkIISR 4th ROM Bank 3"),
        KnownRomHash("063609dd518c124a4fc9ba35d1bae35771666a34", 32768L, "quasi88", "q88_n88n", "N88N.ROM", "PC-8801 N-BASIC ROM (MAME)"),
        KnownRomHash("5b922ed9de07d2a729bdf1da7b57c50ddf08809a", 32768L, "quasi88", "q88_n88n", "N88N.ROM", "PC-8801mkIISR N-BASIC ROM"),
        KnownRomHash("bb7103a0818850a039c67ff666a31ce49a8d516f", 2048L, "quasi88", "q88_disk", "DISK.ROM", "PC-8801 Floppy Subsystem ROM"),
        KnownRomHash("84b90f69671d4b72e8f219e1fe7cd667e976cf7f", 2048L, "quasi88", "q88_font", "FONT.ROM", "PC-8801 Font ROM"),
        KnownRomHash("82e11a177af6a5091dd67f50a2f4bafda84d6556", 131072L, "quasi88", "q88_kanji1", "KANJI1.ROM", "PC-8801 Kanji ROM Level 1"),
        KnownRomHash("b86b932cb4ce34472e0fe0f01a4a4350e15dcb53", 131072L, "quasi88", "q88_kanji2", "KANJI2.ROM", "PC-8801 Kanji ROM Level 2"),

        // PC-9801 (NP2kai)
        KnownRomHash("76a7360502fa0296ea93b4c537174610a834d367", 98304L, "np2kai", "np2_bios", "bios.rom", "PC-9801 System BIOS"),
        KnownRomHash("a2fb11c000ed7c976520622cfb7940ed6ddc904e", 32768L, "np2kai", "np2_itf", "itf.rom", "PC-9801 ITF ROM"),
        KnownRomHash("d5dbc4fea3b8367024d363f5351baecd6adcd8ef", 16384L, "np2kai", "np2_sound", "sound.rom", "PC-9801-26K/86 Sound BIOS"),
        KnownRomHash("78ba9960f135372825ab7244b5e4e73a810002ff", 288768L, "np2kai", "np2_font", "font.rom", "PC-9801 Font ROM"),
        KnownRomHash("b4f14e58030ed40fff2dc312b58ea4440bdf8cc5", 524350L, "np2kai", "np2_font", "font.bmp", "PC-9801 Font BMP"),

        // MSX / MSX2+ (blueMSX Shared Roms)
        KnownRomHash("e998f0c441f4f1800ef44e42cd1659150206cf79", 32768L, "bluemsx", "msx_1", "MSX.rom", "MSX1 BIOS"),
        KnownRomHash("6103b39f1e38d1aa2d84b1c3219c44f1abb5436e", 32768L, "bluemsx", "msx_2", "MSX2.rom", "MSX2 Main BIOS"),
        KnownRomHash("5c1f9c7fb655e43d38e5dd1fcc6b942b2ff68b02", 16384L, "bluemsx", "msx_2ext", "MSX2EXT.rom", "MSX2 Sub/ExtROM"),
        KnownRomHash("e2fbd56e42da637609d23ae9df9efd1b4241b18a", 32768L, "bluemsx", "msx_2p", "MSX2P.rom", "MSX2+ Main BIOS (Libretro)"),
        KnownRomHash("e90f80a61d94c617850c415e12ad70ac41e66bb7", 32768L, "bluemsx", "msx_2p", "MSX2P.rom", "MSX2+ Main BIOS (blueMSX)"),
        KnownRomHash("fe0254cbfc11405b79e7c86c7769bd6322b04995", 16384L, "bluemsx", "msx_2pext", "MSX2PEXT.rom", "MSX2+ Sub/ExtROM"),
        KnownRomHash("7ed7c55e0359737ac5e68d38cb6903f9e5d7c2b6", 16384L, "bluemsx", "msx_pdisk", "PANASONICDISK.rom", "Panasonic MSX Disk BIOS"),
        KnownRomHash("c3efedda7ab947a06d9345f7b8261076fa7ceeef", 16384L, "bluemsx", "msx_phdisk", "PHILIPSDISK.rom", "Philips MSX Disk BIOS"),
        KnownRomHash("6354ccc5c100b1c558c9395fa8c00784d2e9b0a3", 16384L, "bluemsx", "msx_music", "MSX2PMUS.rom", "MSX-MUSIC FM-PAC BIOS"),
        KnownRomHash("47370bec7ca1f0615a54eda548b07fbc0c7ef398", 16384L, "bluemsx", "msx_xbasic", "XBASIC2.rom", "MSX2+ XBASIC2 ROM"),
        KnownRomHash("32760893ce06dbe3930627755ba065cc3d8ec6ca", 2097152L, "bluemsx", "msx_moon", "MOONSOUND.rom", "OPL4 MoonSound Wave ROM (YRW801)"),

        // SHARP MZ-700 (EmuZ-700)
        KnownRomHash("7b10d7965c541393e33a265bcf71a00314d2db7a", 4096L, "emuz700", "mz_ipl", "1Z-009B.ROM", "MZ-700 Monitor 1Z-009B (JP)"),
        KnownRomHash("bd2cc750f2d2f63e50a59786668509e81a276e32", 4096L, "emuz700", "mz_font", "MZ700FON.JP", "MZ-700 CGROM (JP)"),
        KnownRomHash("5eb1d42d273b1fd2cab120486279ab8ff6c85dc7", 4096L, "emuz700", "mz_font", "MZ700FON.JP", "MZ-700 CGROM (JP alt)"),
        KnownRomHash("5128ad179a702f8e0bd9910a58bad8fbe4c20167", 4096L, "emuz700", "mz_font", "MZ700FON.DAT", "MZ-700 CGROM (INT)"),

        // FM-7 / FM77AV / FM77AV40
        KnownRomHash("50b7283db6fe1342c6063fc94046283f4feddc1c", 10240L, "fm7", "fm_subc", "SUBSYS_C.ROM", "FM-7/77AV Subsys C ROM"),
        KnownRomHash("8d5f0cfe7e0d39bf2ab7e4c798a13004769c28b2", 31744L, "fm7", "fm_fbas", "FBASIC30.ROM", "FM-7/77AV F-BASIC V3.0 ROM"),
        KnownRomHash("53b63a301cba7e3030e79c59a4d4291eab6e64b0", 512L, "fm7", "fm_boot", "BOOT_BAS.ROM", "FM-7 BASIC Boot ROM"),
        KnownRomHash("037e5881bd3fed472a210ee894a6446965a8d2ef", 512L, "fm7", "fm_boot", "BOOT_DOS.ROM", "FM-NEW7 DOS Boot ROM"),
        KnownRomHash("616c17155f84fb0e3731a31ef0eb0cbb664a5600", 512L, "fm7", "fm_boot", "BOOT_DOS.ROM", "FM-7 DOS Boot ROM"),
        KnownRomHash("b65987e98a9564a82c85eadb86f0204eee5a5c93", 8192L, "fm7", "fm_init", "INITIATE.ROM", "FM77AV Initiate ROM"),
        KnownRomHash("8ab466b1546d023ba54987790a79e9815d2b7bb2", 8192L, "fm7", "fm_cg", "SUBSYSCG.ROM", "FM77AV Subsys CG ROM"),
        KnownRomHash("bf52d22b119d54410dad4949b0687bb0edf3e143", 131072L, "fm7", "fm_kanji", "KANJI.ROM", "FM-7/77AV Kanji ROM Level 1"),
        KnownRomHash("038cb0b42aee9e933b20fccd6f19942e2f476c83", 8192L, "fm7", "fm_suba", "SUBSYS_A.ROM", "FM77AV Subsys A ROM"),
        KnownRomHash("0305bdd44e7d9b7b6a17675aff0a3330a08d21a8", 8192L, "fm7", "fm_suba", "SUBSYS_B.ROM", "FM77AV Subsys B ROM"),
        KnownRomHash("a1304457eeb400b4edd3c20af948d66a04df255e", 49152L, "fm7", "fm_suba", "EXTSUB.ROM", "FM77AV40 ExtSub ROM"),
        KnownRomHash("fe9f92a8a2750bcba0a1d2895e75e83858e4f97f", 262144L, "fm7", "fm_suba", "DICROM.ROM", "FM77AV40 Dictionary ROM"),

        // SHARP X68000 (PX68k / msx68k)
        KnownRomHash("0ed038ed2133b9f78c6e37256807424e0d927560", 131072L, "msx68k", "x68_ipl", "IPLROM.DAT", "X68000 IPL-ROM v1.0"),
        KnownRomHash("239e9124568c862c31d9ec0605e32373ea74b86a", 131072L, "msx68k", "x68_ipl", "IPLROM30.DAT", "X68030 IPL-ROM v1.3"),
        KnownRomHash("e33cdcdb69cd257b0b211ef46e7a8b144637db57", 131072L, "msx68k", "x68_scsi", "IPLROMXV.DAT", "X68000 XVI IPL-ROM v1.1"),
        KnownRomHash("77511fc58798404701f66b6bbc9cbde06596eba7", 131072L, "msx68k", "x68_scsi", "IPLROMCO.DAT", "X68000 Compact IPL-ROM v1.2"),
        KnownRomHash("8d72c5b4d63bb14c5dbdac495244d659aa1498b6", 786432L, "msx68k", "x68_cg", "CGROM.DAT", "X68000 CGROM"),
        KnownRomHash("3f063d4231cdf53da6adc4db96533725e260076a", 8192L, "msx68k", "x68_scsi", "SCSIINROM.DAT", "X68000 Internal SCSI ROM"),

        // Roland MT-32 / CM-32L (munt)
        KnownRomHash("73683d585cd6948cc19547942ca0e14a0319456d", 65536L, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32L/LAPC-I Control v1.00"),
        KnownRomHash("a439fbb390da38cada95a7cbb1d6ca199cd66ef8", 65536L, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32L/LAPC-I Control v1.02"),
        KnownRomHash("dc1c5b1b90a4646d00f7daf3679733c7badc7077", 65536L, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32LN/CM-500 Control v1.00"),
        KnownRomHash("5a5cb5a77d7d55ee69657c2f870416daed52dea7", 65536L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.04"),
        KnownRomHash("e17a3a6d265bf1fa150312061134293d2b58288c", 65536L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.05"),
        KnownRomHash("a553481f4e2794c10cfe597fef154eef0d8257de", 65536L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.06"),
        KnownRomHash("b083518fffb7f66b03c23b7eb4f868e62dc5a987", 65536L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.07"),
        KnownRomHash("7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92", 65536L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control BlueRidge"),
        KnownRomHash("5837064c9df4741a55f7c4d8787ac158dff2d3ce", 131072L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.03"),
        KnownRomHash("2c16432b6c73dd2a3947cba950a0f4c19d6180eb", 131072L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.04"),
        KnownRomHash("2869cf4c235d671668cfcb62415e2ce8323ad4ed", 131072L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.06"),
        KnownRomHash("47b52adefedaec475c925e54340e37673c11707c", 131072L, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.07"),
        KnownRomHash("289cc298ad532b702461bfc738009d9ebe8025ea", 1048576L, "mt32", "mt_pcm", "CM32L_PCM.ROM", "CM-32L/CM-64/LAPC-I PCM ROM"),
        KnownRomHash("f6b1eebc4b2d200ec6d3d21d51325d5b48c60252", 524288L, "mt32", "mt_pcm", "MT32_PCM.ROM", "MT-32 PCM ROM"),
    )
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

    fun sha1Hex(bytes: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-1").digest(bytes)
        val hex = StringBuilder(40)
        for (b in digest) {
            hex.append(String.format(Locale.ROOT, "%02x", b.toInt() and 0xff))
        }
        return hex.toString()
    }

    fun identifyRomByHash(bytes: ByteArray, scopeMachine: MachineRomSpec? = null): KnownRomHash? {
        if (bytes.isEmpty()) return null
        val len = bytes.size.toLong()
        val scoped = scopeMachine?.id
        val anySize = RomCatalog.knownRomHashes.any {
            it.sizeBytes == len && (scoped == null || it.machineId == scoped)
        }
        if (!anySize) return null
        val digest = sha1Hex(bytes)
        return RomCatalog.knownRomHashes.firstOrNull {
            it.sizeBytes == len && (scoped == null || it.machineId == scoped) && it.sha1 == digest
        }
    }

    fun identifyInstalledFile(file: File, machine: MachineRomSpec, slot: RomSlot): KnownRomHash? {
        if (!file.isFile || file.length() <= 0L || file.length() > MAX_ENTRY_BYTES) return null
        val bytes = runCatching { file.readBytes() }.getOrNull() ?: return null
        val hit = identifyRomByHash(bytes, machine) ?: return null
        return if (hit.slotId == slot.id) hit else null
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
        val placed = mutableListOf<String>()
        val warnings = mutableListOf<String>()
        var skipped = 0
        var unknownWarnings = 0
        val buf = ByteArray(16 * 1024)
        while (true) {
            val entry = zis.nextEntry ?: break
            try {
                if (entry.isDirectory) continue
                val base = entry.name.substringAfterLast('/').substringAfterLast('\\')
                if (base.isEmpty() || base.startsWith(".") || entry.name.startsWith("__MACOSX/")) {
                    continue
                }
                if (entry.size > MAX_ENTRY_BYTES) {
                    warnings.add("$base: 32MB を超えるためスキップ")
                    skipped++
                    continue
                }
                val low = base.lowercase(Locale.ROOT)
                if (low == "d8000.rom") {
                    warnings.add("d8000.rom は FD 起動ハング防止のため自動スキップしました")
                    skipped++
                    continue
                }
                val baos = ByteArrayOutputStream()
                var total = 0L
                var tooBig = false
                while (true) {
                    val n = zis.read(buf)
                    if (n <= 0) break
                    total += n
                    if (total > MAX_ENTRY_BYTES) {
                        tooBig = true
                        break
                    }
                    baos.write(buf, 0, n)
                }
                if (tooBig || total == 0L) {
                    if (tooBig) warnings.add("$base: 32MB を超えるためスキップ")
                    skipped++
                    continue
                }
                val payload = baos.toByteArray()
                val match = identifyRomByHash(payload, scopeMachine)
                if (match == null) {
                    skipped++
                    if (unknownWarnings < 8) {
                        val h = sha1Hex(payload).take(8)
                        warnings.add("$base: 未登録SHA-1 ($h…) — 独自ROMは個別スロットから配置してください")
                    } else if (unknownWarnings == 8) {
                        warnings.add("他の一致しないファイルの警告は省略しました")
                    }
                    unknownWarnings++
                    continue
                }
                val machine = RomCatalog.machines.firstOrNull { it.id == match.machineId }
                val slot = machine?.slots?.firstOrNull { it.id == match.slotId }
                if (machine == null || slot == null) {
                    skipped++
                    continue
                }
                val dir = slotDir(dataRoot, machine, slot).apply { mkdirs() }
                val dst = File(dir, match.saveName)
                dst.writeBytes(payload)
                placed.add("${machine.title}:${match.saveName} [${match.label}]")
            } finally {
                zis.closeEntry()
            }
        }
        return ImportReport(placed = placed, warnings = warnings, skippedCount = skipped)
    }

    fun importMultipleUris(
        context: Context,
        uris: List<Uri>,
        dataRoot: File,
        scopeMachine: MachineRomSpec?,
    ): ImportReport {
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
            val payload = runCatching {
                context.contentResolver.openInputStream(uri)?.use { input ->
                    val baos = ByteArrayOutputStream()
                    val buf = ByteArray(16 * 1024)
                    var total = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        total += n
                        if (total > MAX_ENTRY_BYTES) error("32MB を超えるファイルです")
                        baos.write(buf, 0, n)
                    }
                    baos.toByteArray()
                }
            }.getOrElse { err ->
                warnings.add("$name: ${err.message}")
                skipped++
                return@forEach
            }
            if (payload == null || payload.isEmpty()) {
                skipped++
                return@forEach
            }
            val match = identifyRomByHash(payload, scopeMachine)
            if (match == null) {
                val h = sha1Hex(payload).take(8)
                warnings.add("$name: 未登録SHA-1 ($h…) — 独自ROMは個別スロットから配置してください")
                skipped++
                return@forEach
            }
            val machine = RomCatalog.machines.firstOrNull { it.id == match.machineId }
            val slot = machine?.slots?.firstOrNull { it.id == match.slotId }
            if (machine == null || slot == null) {
                skipped++
                return@forEach
            }
            runCatching {
                val dir = slotDir(dataRoot, machine, slot).apply { mkdirs() }
                File(dir, match.saveName).writeBytes(payload)
            }.onSuccess {
                placed.add("${machine.title}:${match.saveName} [${match.label}]")
            }.onFailure { err ->
                warnings.add("$name: ${err.message}")
            }
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
