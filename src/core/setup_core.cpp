#include "core/setup_core.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace czx::core {

namespace {

constexpr int kW = 320;
constexpr int kH = 170;

enum class RomTier {
    RequiredMissing = 0,
    RequiredOk,
    BuiltinNoRoms,
    BuiltinPlusRoms,
    OptionalEmpty,
    OptionalOk,
};

struct RomSlotSpec {
    const char* canonical;
    const char* rel_dir;  // relative to roms_sub_dir (empty = directly inside roms_sub_dir)
    bool required;
    bool case_insensitive;
    const char* note;
    std::array<const char*, 7> aliases;
};

struct MachineDoc {
    const char* name;
    const char* subtitle;
    const char* roms_sub_dir;
    const char* case_rule;
    const char* builtin_tag;  // nullptr if external ROMs are required
    const char* media_dir;
    const char* media_exts;
    const char* tip1;
    const char* tip2;
    const RomSlotSpec* slots;
    size_t slot_count;
};

constexpr RomSlotSpec kAppleSlots[] = {
    {"Apple2e_Enhanced.rom", "", true, true, "Main Enhanced IIe ROM", {}},
    {"Apple2e_Enhanced_Video.rom", "", true, true, "Character / video ROM", {}},
    {"DISK2.rom", "", true, true, "Disk II controller ROM", {}},
};

constexpr RomSlotSpec kC64Slots[] = {
    {"kernal-901227-03.bin", "C64", true, false, "Lowercase in C64/", {}},
    {"basic-901226-01.bin", "C64", true, false, "Lowercase in C64/", {}},
    {"chargen-901225-01.bin", "C64", true, false, "Lowercase in C64/", {}},
    {"dos1541ii-251968-03.bin", "DRIVES", true, false, "Lowercase in DRIVES/", {}},
};

constexpr RomSlotSpec kPc88Slots[] = {
    {"N88.ROM", "", false, true, "N88-BASIC v1/v2 ROM", {}},
    {"N88_0.ROM", "", false, true, "4th ROM bank 0", {"N88_1.ROM", "N88_2.ROM", "N88_3.ROM"}},
    {"N88N.ROM", "", false, true, "N-BASIC mode ROM", {}},
    {"DISK.ROM", "", false, true, "5-inch floppy subsystem", {}},
    {"FONT.ROM", "", false, true, "ANK / semigraphics font", {}},
    {"KANJI1.ROM", "", false, true, "JIS Kanji ROM level 1", {"KANJI2.ROM"}},
};

constexpr RomSlotSpec kPc98Slots[] = {
    {"bios.rom", "", false, false, "ITF + System BIOS (lowercase)", {}},
    {"itf.rom", "", false, false, "ITF ROM if split", {}},
    {"sound.rom", "", false, false, "PC-9801-26K/86 OPNA BIOS", {}},
    {"font.rom", "", false, false, "or font.bmp (JIS font)", {"font.bmp"}},
};

constexpr RomSlotSpec kMsxSlots[] = {
    {"MSX2P.rom", "Machines/Shared Roms", false, true, "MSX2+ main BIOS", {}},
    {"MSX2PEXT.rom", "Machines/Shared Roms", false, true, "MSX2+ sub/ExtROM", {}},
    {"MSX2.rom", "Machines/Shared Roms", false, true, "MSX2 main BIOS", {"MSX2EXT.rom"}},
    {"MSX.rom", "Machines/Shared Roms", false, true, "MSX1 BIOS (standalone)", {}},
    {"PANASONICDISK.rom", "Machines/Shared Roms", false, true, "Needed for .dsk floppies", {"PHILIPSDISK.rom"}},
    {"MSX2PMUS.rom", "Machines/Shared Roms", false, true, "MSX-MUSIC FM + Kanji/MoonSound", {"XBASIC2.rom", "KANJI.rom", "MOONSOUND.rom"}},
};

constexpr RomSlotSpec kG800Slots[] = {
    {"base.bin", "", false, true, "or base.txt (SHARP ROM)", {"base.txt"}},
    {"rom00.bin", "", false, true, "or rom00.txt (ext banks)", {"rom00.txt", "rom01.bin"}},
};

constexpr RomSlotSpec kMz700Slots[] = {
    {"IPL.ROM", "", true, true, "1Z-009A/B, NEWMON7.ROM", {"1Z-009A.ROM", "1Z-009B.ROM", "1Z009A.ROM", "1Z009B.ROM", "MZ700IPL.ROM", "NEWMON7.ROM"}},
    {"FONT.ROM", "", true, true, "MZ700FON.JP/.DAT/.ROM", {"MZ700FON.JP", "MZ700FON.DAT", "MZ700FON.ROM", "CGROM.ROM", "MZ700FONT.ROM"}},
};

constexpr RomSlotSpec kFm7Slots[] = {
    {"SUBSYS_C.ROM", "", true, true, "Required (all models)", {}},
    {"FBASIC30.ROM", "", true, true, "or 300/301/302 (all models)", {"FBASIC300.ROM", "FBASIC301.ROM", "FBASIC302.ROM"}},
    {"BOOT_BAS.ROM", "", true, true, "Required for FM-7", {"BOOT_DOS.ROM", "BOOT_TL11_11.ROM"}},
    {"INITIATE.ROM", "", false, true, "Required for FM77AV / AV40", {}},
    {"SUBSYSCG.ROM", "", false, true, "Required for FM77AV / AV40", {}},
    {"KANJI.ROM", "", false, true, "Optional (FM77AV+)", {"SUBSYS_A.ROM", "SUBSYS_B.ROM", "EXTSUB.ROM"}},
};

constexpr RomSlotSpec kX68kSlots[] = {
    {"IPLROM.DAT", "", true, true, "or IPLROM30.DAT", {"IPLROM30.DAT"}},
    {"CGROM.DAT", "", true, true, "Character font ROM", {}},
    {"SCSIINROM.DAT", "", false, true, "Optional internal/ext SCSI", {"SCSIEXROM.DAT", "IPLROMXV.DAT", "IPLROMCO.DAT"}},
};

constexpr const RomSlotSpec* kDosSlots = nullptr;

constexpr RomSlotSpec kMt32Slots[] = {
    {"CM32L_CONTROL.ROM", "", false, true, "Preferred CM-32L control ROM", {"MT32_CONTROL.ROM"}},
    {"CM32L_PCM.ROM", "", false, true, "Preferred CM-32L wave ROM", {"MT32_PCM.ROM"}},
};

constexpr std::array<MachineDoc, 11> kMachines = {{
    {
        "Apple II",
        "applewin",
        "apple2",
        "Case-insensitive",
        nullptr,
        "media/apple2/",
        ".dsk .do .po .nib .woz .2mg .hdv .zip",
        "3 ROMs above required before boot.",
        "2 drives supported; use Menu for Disk 1/2.",
        kAppleSlots,
        std::size(kAppleSlots),
    },
    {
        "Commodore 64",
        "vice_x64 / x64sc",
        "vice",
        "EXACT lowercase",
        nullptr,
        "media/c64/",
        ".d64 .g64 .x64 .d71 .d81 .prg .crt .t64 .tap",
        "Put C64 ROMs in C64/, drive ROM in DRIVES/.",
        "Region (PAL/NTSC) & x64/x64sc switch in Menu.",
        kC64Slots,
        std::size(kC64Slots),
    },
    {
        "PC-8801",
        "quasi88",
        "quasi88",
        "Case-insensitive",
        "PSEUDO",
        "media/pc88/",
        ".d88 .88d .t88 .cmt .m3u",
        "Boots standalone with built-in pseudo BIOS.",
        "Real ROMs unlock N88-BASIC, Kanji & F12 games.",
        kPc88Slots,
        std::size(kPc88Slots),
    },
    {
        "PC-9801",
        "np2kai",
        "np2kai",
        "EXACT lowercase",
        "HLE",
        "media/pc98/",
        ".d88 .fdi .hdm .nhd .hdi .thd .nfd .98d .m3u",
        "Boots with HLE BIOS; real ROMs use lowercase.",
        "WARN: Avoid d8000.rom unless booting an HDD!",
        kPc98Slots,
        std::size(kPc98Slots),
    },
    {
        "MSX / MSX2+",
        "bluemsx",
        "bluemsx",
        "Case-insensitive",
        "C-BIOS",
        "media/msx/",
        ".rom .mx1 .mx2 .dsk .cas .m3u",
        "Bundled C-BIOS runs carts; real ROMs auto-detect.",
        "Floppy (.dsk) needs PANASONIC/PHILIPSDISK.rom.",
        kMsxSlots,
        std::size(kMsxSlots),
    },
    {
        "PC-G850 / E200",
        "g800",
        "g800",
        "Case-insensitive",
        "COMPAT",
        "media/g800/",
        ".ihx .hex .bas .txt",
        "Boots built-in compat BASIC/IOCS immediately.",
        "Per-model dirs g850/, g815/, e200/ checked 1st.",
        kG800Slots,
        std::size(kG800Slots),
    },
    {
        "MZ-700",
        "emuz700",
        "mz700",
        "Case-insensitive",
        nullptr,
        "media/mz700/",
        ".mzt .mzf .m12 .t77 .q20 .d88 .2d .wav",
        "Requires 1 IPL alias + 1 FONT alias.",
        "F6=KANA, F7=EISUU, F8=GRAPH, ESC=BREAK.",
        kMz700Slots,
        std::size(kMz700Slots),
    },
    {
        "FM-7 / FM77AV",
        "efm7 / 77av / 40",
        "fm7",
        "Case-insensitive",
        nullptr,
        "media/fm7/",
        ".d77 .77d .2d .t77",
        "Switch FM-7 / FM77AV / FM77AV40 in Menu.",
        "77AV/AV40 also need INITIATE.ROM & SUBSYSCG.ROM.",
        kFm7Slots,
        std::size(kFm7Slots),
    },
    {
        "X68000",
        "msx68k",
        "x68000",
        "Case-insensitive",
        nullptr,
        "media/x68000/",
        ".dim .xdf .hdm .2hd .hdf .hds .m3u",
        "IPLROM.DAT (or 30) + CGROM.DAT required.",
        "Optional Roland MT-32 / CM-32L MIDI in Menu.",
        kX68kSlots,
        std::size(kX68kSlots),
    },
    {
        "IBM PC / DOS",
        "dosbox_pure",
        "dos",
        "No BIOS needed",
        "READY",
        "media/dos/",
        ".zip .dosz .exe .com .bat .ima .img .iso .chd",
        "Boots straight into DOS; .zip runs directly!",
        "Put SoundFonts (.sf2) in roms/dos/ if desired.",
        kDosSlots,
        0,
    },
    {
        "Roland MT-32",
        "munt synth",
        "mt32",
        "Case-insensitive",
        "OPT",
        "Shared Audio",
        "PC-98 / X68000 / DOS MIDI out",
        "CM32L_CONTROL + PCM (or MT32_CONTROL + PCM).",
        "Enable MT-32 synth inside the System Menu.",
        kMt32Slots,
        std::size(kMt32Slots),
    },
}};

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

bool rom_file_exists_ci(const fs::path& dir, const char* name, bool ci) {
    if (!name || name[0] == '\0') return false;
    std::error_code ec;
    if (fs::is_regular_file(dir / name, ec) && !ec) {
        return true;
    }
    if (!ci || !fs::is_directory(dir, ec)) return false;
    const std::string want = to_lower_copy(name);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (to_lower_copy(entry.path().filename().string()) == want) {
            return true;
        }
    }
    return false;
}

bool slot_present(const fs::path& base_roms, const RomSlotSpec& slot) {
    const fs::path dir = (slot.rel_dir && slot.rel_dir[0] != '\0') ? (base_roms / slot.rel_dir) : base_roms;
    if (rom_file_exists_ci(dir, slot.canonical, slot.case_insensitive)) return true;
    for (const char* alias : slot.aliases) {
        if (alias && rom_file_exists_ci(dir, alias, slot.case_insensitive)) return true;
    }
    return false;
}

std::string resolve_default_data_dir() {
    if (const char* dd = std::getenv("CZX_DATA_DIR"); dd && dd[0] != '\0') {
        return dd;
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return (fs::path(home) / "czx").string();
    }
    return "/tmp/czx";
}

struct DetailLine {
    std::string text;
    uint16_t color;
};

struct MachineStatus {
    RomTier tier = RomTier::BuiltinNoRoms;
    int req_found = 0;
    int req_total = 0;
    int opt_found = 0;
    int opt_total = 0;
    std::vector<bool> slot_ok;
};

struct SetupCoreState {
    czx_host_t host{};
    std::string data_dir;
    int tab = 0;  // 0=ROMS, 1=CONTROLS, 2=FILES, 3=ABOUT
    int machine_cursor = 0;
    int list_scroll = 0;
    bool in_detail = false;
    int detail_scroll = 0;
    int info_scroll = 0;
    uint32_t prev_joy = 0;
    uint64_t frame_num = 0;
    std::vector<MachineStatus> statuses;
    std::vector<uint16_t> fb;
    std::vector<int16_t> audio_buf;

    void refresh_statuses() {
        if (data_dir.empty()) {
            data_dir = resolve_default_data_dir();
        }
        statuses.resize(kMachines.size());
        const fs::path roms_root = fs::path(data_dir) / "roms";
        for (size_t i = 0; i < kMachines.size(); ++i) {
            const MachineDoc& m = kMachines[i];
            MachineStatus st{};
            st.slot_ok.assign(m.slot_count, false);
            const fs::path sub = roms_root / m.roms_sub_dir;
            for (size_t s = 0; s < m.slot_count; ++s) {
                const bool ok = slot_present(sub, m.slots[s]);
                st.slot_ok[s] = ok;
                if (m.slots[s].required) {
                    ++st.req_total;
                    if (ok) ++st.req_found;
                } else {
                    ++st.opt_total;
                    if (ok) ++st.opt_found;
                }
            }
            if (st.req_total > 0) {
                st.tier = (st.req_found >= st.req_total) ? RomTier::RequiredOk : RomTier::RequiredMissing;
            } else if (m.builtin_tag && std::strcmp(m.builtin_tag, "OPT") == 0) {
                st.tier = (st.opt_found > 0) ? RomTier::OptionalOk : RomTier::OptionalEmpty;
            } else if (m.builtin_tag) {
                st.tier = (st.opt_found > 0) ? RomTier::BuiltinPlusRoms : RomTier::BuiltinNoRoms;
            } else {
                st.tier = RomTier::OptionalEmpty;
            }
            statuses[i] = std::move(st);
        }
    }

    std::vector<DetailLine> build_detail_lines(size_t idx) const {
        using czx::video::pack_rgb565;
        const MachineDoc& m = kMachines[idx];
        const MachineStatus& st = statuses[idx];
        std::vector<DetailLine> lines;
        const uint16_t cHead = pack_rgb565(120, 225, 255);
        const uint16_t cText = pack_rgb565(225, 232, 240);
        const uint16_t cMuted = pack_rgb565(160, 175, 195);
        const uint16_t cOk = pack_rgb565(96, 230, 145);
        const uint16_t cMiss = pack_rgb565(255, 195, 85);

        lines.push_back({std::string("ROM DIR: roms/") + m.roms_sub_dir + "/", cHead});
        lines.push_back({std::string("CASE   : ") + m.case_rule, cMuted});
        if (m.builtin_tag && std::strcmp(m.builtin_tag, "OPT") != 0) {
            lines.push_back({std::string("BOOT   : Ready standalone (") + m.builtin_tag + ")", cOk});
        } else if (st.req_total > 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "REQUIRED: %d / %d installed", st.req_found, st.req_total);
            lines.push_back({buf, (st.req_found >= st.req_total) ? cOk : cMiss});
        }

        if (m.slot_count > 0) {
            lines.push_back({"", cText});
            lines.push_back({"ROM FILES (*=required):", cHead});
            for (size_t s = 0; s < m.slot_count; ++s) {
                const RomSlotSpec& sl = m.slots[s];
                const bool ok = st.slot_ok[s];
                std::string prefix = ok ? "[OK] " : (sl.required ? "[!!] " : "[--] ");
                std::string line = prefix + (sl.required ? "* " : "  ") + sl.canonical;
                if (line.size() > 49) line.resize(49);
                lines.push_back({line, ok ? cOk : (sl.required ? cMiss : cText)});
                if (sl.note && sl.note[0] != '\0') {
                    lines.push_back({std::string("       ") + sl.note, cMuted});
                }
            }
        }

        lines.push_back({"", cText});
        lines.push_back({std::string("MEDIA  : ") + m.media_dir, cHead});
        lines.push_back({std::string("EXTS   : ") + m.media_exts, cMuted});
        if (m.tip1 && m.tip1[0] != '\0') lines.push_back({m.tip1, cText});
        if (m.tip2 && m.tip2[0] != '\0') lines.push_back({m.tip2, cMiss});
#if defined(__ANDROID__)
        lines.push_back({"Android: Tap SETUP panel below to import!", cOk});
#else
        lines.push_back({"Tip: Use WEB tab to upload ROMs via browser!", cOk});
#endif
        return lines;
    }

    std::vector<DetailLine> build_info_lines(int t) const {
        using czx::video::pack_rgb565;
        const uint16_t cHead = pack_rgb565(120, 225, 255);
        const uint16_t cText = pack_rgb565(225, 232, 240);
        const uint16_t cOk = pack_rgb565(96, 230, 145);
        const uint16_t cMuted = pack_rgb565(165, 180, 200);
        std::vector<DetailLine> lines;
        if (t == 1) {
            lines.push_back({"CARDPUTER ZERO / DESKTOP:", cHead});
            lines.push_back({"  Alt+4: 1:1 Pan (Fn+Arrows to pan)", cText});
            lines.push_back({"  Alt+5: Axis Fit   Alt+6: Aspect Fit", cText});
            lines.push_back({"  Alt+7: Input Mode (Kbd/Joy1/Joy2/Mouse)", cText});
            lines.push_back({"  Alt+8: Open System Menu", cText});
            lines.push_back({"", cText});
            lines.push_back({"USB GAMEPAD & SETUP NAVIGATION:", cHead});
            lines.push_back({"  D-Pad: Move   A/Enter: OK   B/Esc: Exit/Back", cText});
            lines.push_back({"  X: Up Dir / Exit   L1/R1: PgUp / PgDn", cText});
            lines.push_back({"  UI keys: F=Up X=Down Z=Left C=Right", cMuted});
#if defined(__ANDROID__)
            lines.push_back({"", cText});
            lines.push_back({"ANDROID PORTRAIT CONTROLS:", cHead});
            lines.push_back({"  Top menu bar: MENU, PAR/4:3, MAIN/NUM, SETUP", cText});
            lines.push_back({"  Menu/Launcher auto-switches pad for UI.", cOk});
#endif
        } else if (t == 2) {
            lines.push_back({"DATA DIRECTORY (~/czx or app files/czx):", cHead});
            lines.push_back({"  roms/<target>/   System & BIOS ROMs", cText});
            lines.push_back({"  media/<target>/  Disks, Tapes, Cartridges", cText});
            lines.push_back({"  saves/<target>/  NVRAM, SRAM, Configs", cText});
            lines.push_back({"", cText});
            lines.push_back({"MULTI-DISK & PLAYLISTS:", cHead});
            lines.push_back({"  .d88 / .d77: Pick sub-disk per Drive 1/2", cText});
            lines.push_back({"  .m3u: Lists disks 1 per line; auto Drive1/2", cText});
            lines.push_back({"", cText});
            lines.push_back({"ROLAND MT-32 / CM-32L SYNTH:", cHead});
            lines.push_back({"  Put CM32L_CONTROL.ROM + CM32L_PCM.ROM", cText});
            lines.push_back({"  (or MT32_*) in roms/mt32/ (built-in munt).", cText});
            lines.push_back({"", cText});
            lines.push_back({"BLUE-MSX DATA:", cHead});
            lines.push_back({"  Open C-BIOS & Databases auto-seeded.", cOk});
            lines.push_back({"  Put real MSX ROMs in roms/bluemsx/Machines/", cMuted});
            lines.push_back({"  Shared Roms/ for full BASIC/Disk BIOS.", cMuted});
        } else {
            lines.push_back({"CZX RETRO COMPUTER MULTI-EMULATOR", cHead});
            lines.push_back({"  Version: 0.9.1   License: GPL-3.0-or-later", cText});
            lines.push_back({"", cText});
            lines.push_back({"ROM POLICY:", cHead});
            lines.push_back({"  No copyrighted BIOS/ROMs are bundled.", cMissOrOk(true)});
            lines.push_back({"  Open fallback BIOS included for PC-88/98,", cText});
            lines.push_back({"  MSX (C-BIOS), PC-G850, and FreeDOS.", cText});
            lines.push_back({"  Other targets require user-supplied ROMs.", cText});
            lines.push_back({"", cText});
            lines.push_back({"FULL DOCS & SOURCE REPOSITORY:", cHead});
            lines.push_back({"  https://github.com/toyoshim-i/czx", cOk});
            lines.push_back({"  docs/*.md covers every machine in detail.", cMuted});
        }
        return lines;
    }

    static uint16_t cMissOrOk(bool highlight) {
        return highlight ? czx::video::pack_rgb565(255, 200, 95)
                         : czx::video::pack_rgb565(225, 232, 240);
    }

    void handle_nav(int dy, int dx, bool ok, bool back, int pg) {
        if (tab == 0 && in_detail) {
            const int max_s = std::max(0, static_cast<int>(build_detail_lines(static_cast<size_t>(machine_cursor)).size()) - 11);
            if (back || dx < 0) {
                in_detail = false;
                detail_scroll = 0;
                return;
            }
            if (dy != 0) {
                detail_scroll = std::clamp(detail_scroll + dy, 0, max_s);
            } else if (pg != 0) {
                detail_scroll = std::clamp(detail_scroll + pg * 6, 0, max_s);
            }
            return;
        }

        if (dx != 0) {
            tab = (tab + 4 + (dx > 0 ? 1 : -1)) % 4;
            info_scroll = 0;
            return;
        }

        if (back) {
            if (host.return_to_launcher) {
                host.return_to_launcher(host.user_data);
            }
            return;
        }

        if (tab == 0) {
            const int n = static_cast<int>(kMachines.size());
            if (dy != 0) {
                machine_cursor = (machine_cursor + n + (dy > 0 ? 1 : -1)) % n;
            } else if (pg != 0) {
                machine_cursor = std::clamp(machine_cursor + pg * 5, 0, n - 1);
            } else if (ok) {
                in_detail = true;
                detail_scroll = 0;
            }
        } else {
            const int max_s = static_cast<int>(build_info_lines(tab).size()) - 11;
            const int clamped_max = std::max(0, max_s);
            if (dy != 0) {
                info_scroll = std::clamp(info_scroll + dy, 0, clamped_max);
            } else if (pg != 0) {
                info_scroll = std::clamp(info_scroll + pg * 6, 0, clamped_max);
            }
        }
    }

    void render() {
        using czx::video::pack_rgb565;
        fb.resize(static_cast<size_t>(kW * kH));
        std::fill(fb.begin(), fb.end(), pack_rgb565(14, 18, 26));
        czx::video::Rgb565Canvas c(fb.data(), kW, kH);

        // Top tab bar
        c.fill_rect(0, 0, kW, 15, pack_rgb565(22, 32, 48));
        c.draw_hline(0, 15, kW, pack_rgb565(68, 160, 210));
        c.draw_text(5, 4, "SETUP", pack_rgb565(115, 225, 255));

        static constexpr std::array<const char*, 4> kTabs = {"ROMS", "CTRL", "FILES", "ABOUT"};
        int tx = 54;
        for (int i = 0; i < 4; ++i) {
            const int tw = static_cast<int>(std::strlen(kTabs[static_cast<size_t>(i)])) * 6 + 10;
            if (i == tab) {
                c.fill_rect(tx, 2, tw, 12, pack_rgb565(46, 108, 152));
                c.draw_rect(tx, 2, tw, 12, pack_rgb565(120, 225, 255));
                c.draw_text(tx + 5, 4, kTabs[static_cast<size_t>(i)], pack_rgb565(255, 255, 255));
            } else {
                c.draw_text(tx + 5, 4, kTabs[static_cast<size_t>(i)], pack_rgb565(165, 180, 200));
            }
            tx += tw + 6;
        }

        if (tab == 0 && !in_detail) {
            constexpr int kVisible = 9;
            const int count = static_cast<int>(kMachines.size());
            if (machine_cursor < list_scroll) list_scroll = machine_cursor;
            if (machine_cursor >= list_scroll + kVisible) list_scroll = machine_cursor - kVisible + 1;
            list_scroll = std::clamp(list_scroll, 0, std::max(0, count - kVisible));

            int y = 20;
            for (int r = 0; r < kVisible; ++r) {
                const int idx = list_scroll + r;
                if (idx >= count) break;
                const MachineDoc& m = kMachines[static_cast<size_t>(idx)];
                const MachineStatus& st = statuses[static_cast<size_t>(idx)];
                const bool sel = (idx == machine_cursor);
                if (sel) {
                    c.fill_rect(4, y - 1, kW - 8, 12, pack_rgb565(34, 58, 86));
                }
                c.draw_text(8, y + 1, sel ? ">" : " ", pack_rgb565(120, 225, 255));
                c.draw_text(16, y + 1, m.name, sel ? pack_rgb565(255, 255, 255) : pack_rgb565(220, 228, 238));
                c.draw_text(114, y + 1, m.roms_sub_dir, pack_rgb565(145, 165, 185));

                const char* badge = "";
                uint16_t bcol = pack_rgb565(160, 175, 195);
                char rbuf[24];
                if (st.tier == RomTier::RequiredOk) {
                    std::snprintf(rbuf, sizeof(rbuf), "OK %d/%d", st.req_found, st.req_total);
                    badge = rbuf;
                    bcol = pack_rgb565(96, 230, 145);
                } else if (st.tier == RomTier::RequiredMissing) {
                    std::snprintf(rbuf, sizeof(rbuf), "MISS %d/%d", st.req_found, st.req_total);
                    badge = rbuf;
                    bcol = pack_rgb565(255, 190, 80);
                } else if (st.tier == RomTier::BuiltinPlusRoms || st.tier == RomTier::OptionalOk) {
                    std::snprintf(rbuf, sizeof(rbuf), "%s +%d", m.builtin_tag, st.opt_found);
                    badge = rbuf;
                    bcol = pack_rgb565(96, 230, 145);
                } else if (m.builtin_tag) {
                    badge = m.builtin_tag;
                    bcol = (st.tier == RomTier::OptionalEmpty) ? pack_rgb565(150, 165, 185)
                                                               : pack_rgb565(100, 215, 255);
                }
                const int bw = static_cast<int>(std::strlen(badge)) * 6;
                c.draw_text(kW - 10 - bw, y + 1, badge, bcol);
                y += 14;
            }
            c.draw_hline(6, kH - 16, kW - 12, pack_rgb565(45, 62, 85));
            c.draw_text(8, kH - 11, "Up/Dn:Sel  Enter:Info  L/R:Tab  Esc:Exit", pack_rgb565(160, 180, 205));
        } else {
            std::vector<DetailLine> lines;
            int scroll = 0;
            const char* footer = nullptr;
            if (tab == 0) {
                lines = build_detail_lines(static_cast<size_t>(machine_cursor));
                const int max_s = std::max(0, static_cast<int>(lines.size()) - 11);
                detail_scroll = std::clamp(detail_scroll, 0, max_s);
                scroll = detail_scroll;
                c.fill_rect(4, 18, kW - 8, 12, pack_rgb565(28, 42, 62));
                c.draw_text(8, 20, kMachines[static_cast<size_t>(machine_cursor)].name, pack_rgb565(255, 255, 255));
                c.draw_text(124, 20, kMachines[static_cast<size_t>(machine_cursor)].subtitle, pack_rgb565(120, 220, 255));
                footer = "Up/Dn:Scroll   Left/B/Esc:Back to List";
            } else {
                lines = build_info_lines(tab);
                const int max_s = std::max(0, static_cast<int>(lines.size()) - 11);
                info_scroll = std::clamp(info_scroll, 0, max_s);
                scroll = info_scroll;
                footer = "L/R:Switch Tab   Up/Dn:Scroll   Esc:Exit";
            }

            int y = (tab == 0) ? 34 : 22;
            const int max_rows = (tab == 0) ? 11 : 12;
            for (int r = 0; r < max_rows; ++r) {
                const int idx = scroll + r;
                if (idx >= static_cast<int>(lines.size())) break;
                if (!lines[static_cast<size_t>(idx)].text.empty()) {
                    c.draw_text(8, y, lines[static_cast<size_t>(idx)].text.c_str(), lines[static_cast<size_t>(idx)].color);
                }
                y += 11;
            }
            c.draw_hline(6, kH - 16, kW - 12, pack_rgb565(45, 62, 85));
            c.draw_text(8, kH - 11, footer, pack_rgb565(160, 180, 205));
        }
    }
};

const czx_core_desc_t kDesc = {
    CZX_CORE_API_VERSION,
    "setup",
    "CZX Setup & Guide",
    60,
    1,
    48000,
};

const czx_core_desc_t* setup_get_desc(const czx_core_api_t*) {
    return &kDesc;
}

czx_core_instance_t* setup_create(const czx_core_api_t*, const czx_host_t* host) {
    auto* st = new SetupCoreState();
    if (host) st->host = *host;
    st->refresh_statuses();
    return reinterpret_cast<czx_core_instance_t*>(st);
}

void setup_destroy(czx_core_instance_t* inst) {
    delete reinterpret_cast<SetupCoreState*>(inst);
}

void setup_reset(czx_core_instance_t* inst, bool hard) {
    auto* st = reinterpret_cast<SetupCoreState*>(inst);
    if (!st) return;
    if (hard) {
        st->tab = 0;
        st->in_detail = false;
        st->detail_scroll = 0;
        st->info_scroll = 0;
    }
    st->refresh_statuses();
}

void setup_set_data_dir(czx_core_instance_t* inst, const char* dir) {
    auto* st = reinterpret_cast<SetupCoreState*>(inst);
    if (!st || !dir || dir[0] == '\0') return;
    st->data_dir = dir;
    st->refresh_statuses();
}

bool setup_load_media(czx_core_instance_t*, const char*) {
    return true;
}

void setup_run_frame(czx_core_instance_t* inst) {
    auto* st = reinterpret_cast<SetupCoreState*>(inst);
    if (!st) return;

    if ((st->frame_num % 60u) == 0u) {
        st->refresh_statuses();
    }
    st->render();

    czx_frame_t frame{};
    frame.data = st->fb.data();
    frame.width = kW;
    frame.height = kH;
    frame.pitch_bytes = static_cast<uint32_t>(kW * sizeof(uint16_t));
    frame.par_num = 1;
    frame.par_den = 1;
    frame.format = CZX_PIXEL_RGB565;
    if (st->host.video_frame) {
        st->host.video_frame(st->host.user_data, &frame);
    }

    constexpr size_t kAudioFrames = 48000 / 60;
    st->audio_buf.assign(kAudioFrames * 2u, 0);
    if (st->host.audio_samples) {
        st->host.audio_samples(st->host.user_data, st->audio_buf.data(), kAudioFrames);
    }
    ++st->frame_num;
}

void setup_on_key(czx_core_instance_t* inst, const czx_key_event_t* ev) {
    auto* st = reinterpret_cast<SetupCoreState*>(inst);
    if (!st || !ev || !ev->pressed) return;

    int dy = 0;
    int dx = 0;
    int pg = 0;
    bool ok = false;
    bool back = false;

    if (ev->key == CZX_KEY_UP || ev->key == CZX_KEY_F) dy = -1;
    else if (ev->key == CZX_KEY_DOWN || ev->key == CZX_KEY_X) dy = 1;
    else if (ev->key == CZX_KEY_LEFT || ev->key == CZX_KEY_Z) dx = -1;
    else if (ev->key == CZX_KEY_RIGHT || ev->key == CZX_KEY_C) dx = 1;
    else if (ev->key == CZX_KEY_PAGEUP) pg = -1;
    else if (ev->key == CZX_KEY_PAGEDOWN) pg = 1;
    else if (ev->key == CZX_KEY_TAB && !ev->repeat) {
        st->tab = (st->tab + 1) % 4;
        st->in_detail = false;
        st->info_scroll = 0;
        return;
    } else if ((ev->key == CZX_KEY_ENTER || ev->key == CZX_KEY_SPACE) && !ev->repeat) {
        ok = true;
    } else if ((ev->key == CZX_KEY_ESC || ev->key == CZX_KEY_BACKSPACE) && !ev->repeat) {
        back = true;
    }

    st->handle_nav(dy, dx, ok, back, pg);
}

void setup_on_joystick(czx_core_instance_t* inst, uint32_t port, uint32_t buttons) {
    auto* st = reinterpret_cast<SetupCoreState*>(inst);
    if (!st || port != 0) return;

    const uint32_t rising = buttons & ~st->prev_joy;
    st->prev_joy = buttons;
    if (rising == 0u) return;

    int dy = 0;
    int dx = 0;
    if ((rising & CZX_JOY_UP) != 0u) dy = -1;
    else if ((rising & CZX_JOY_DOWN) != 0u) dy = 1;
    if ((rising & CZX_JOY_LEFT) != 0u) dx = -1;
    else if ((rising & CZX_JOY_RIGHT) != 0u) dx = 1;
    const bool ok = (rising & CZX_JOY_FIRE1) != 0u;
    const bool back = (rising & CZX_JOY_FIRE2) != 0u;
    st->handle_nav(dy, dx, ok, back, 0);
}

const czx_core_api_t kSetupCoreApi = {
    nullptr,
    setup_get_desc,
    setup_create,
    setup_destroy,
    setup_reset,
    setup_load_media,
    nullptr,
    setup_run_frame,
    setup_on_key,
    setup_on_joystick,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    setup_set_data_dir,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

const czx_core_api_t* get_setup_core_api() noexcept {
    return &kSetupCoreApi;
}

}  // namespace czx::core
