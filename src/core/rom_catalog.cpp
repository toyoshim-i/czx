#include "core/rom_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#ifdef CZX_HAVE_ZLIB
#include <zlib.h>
#endif

namespace fs = std::filesystem;

namespace czx::core {

namespace {

constexpr size_t kMaxEntryBytes = 32u * 1024u * 1024u;

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

std::string base_filename_only(const std::string& raw) {
    size_t pos = raw.find_last_of("/\\");
    std::string b = (pos == std::string::npos) ? raw : raw.substr(pos + 1);
    if (b == "." || b == "..") return {};
    return b;
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    const std::string low = to_lower_copy(s);
    const std::string suf = to_lower_copy(suffix);
    return low.size() >= suf.size() && low.compare(low.size() - suf.size(), suf.size(), suf) == 0;
}

fs::path slot_dir(const std::string& data_dir, const MachineDoc& m, const RomSlotSpec& sl) {
    fs::path p = fs::path(data_dir) / "roms" / m.roms_sub_dir;
    if (sl.rel_dir && sl.rel_dir[0] != '\0') {
        p /= sl.rel_dir;
    }
    return p;
}

RomSlotStatus find_slot_file(const fs::path& dir, const RomSlotSpec& sl) {
    RomSlotStatus st{};
    std::error_code ec;
    auto check_exact = [&](const char* name) -> bool {
        if (!name || name[0] == '\0') return false;
        const fs::path full = dir / name;
        if (fs::is_regular_file(full, ec) && !ec) {
            const uintmax_t sz = fs::file_size(full, ec);
            if (!ec && sz > 0) {
                st.installed = true;
                st.filename = name;
                st.size_bytes = static_cast<uint64_t>(sz);
                return true;
            }
        }
        return false;
    };
    if (check_exact(sl.canonical)) return st;
    for (const char* a : sl.aliases) {
        if (check_exact(a)) return st;
    }
    if (!sl.case_insensitive || !fs::is_directory(dir, ec)) return st;

    std::vector<std::string> wanted;
    wanted.push_back(to_lower_copy(sl.canonical));
    for (const char* a : sl.aliases) {
        if (a && a[0] != '\0') wanted.push_back(to_lower_copy(a));
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const uintmax_t sz = entry.file_size(ec);
        if (ec || sz == 0) continue;
        const std::string fname = entry.path().filename().string();
        const std::string low = to_lower_copy(fname);
        if (std::find(wanted.begin(), wanted.end(), low) != wanted.end()) {
            st.installed = true;
            st.filename = fname;
            st.size_bytes = static_cast<uint64_t>(sz);
            return st;
        }
    }
    return st;
}

std::string resolve_save_filename(const std::string& orig_name, const MachineDoc& m, const RomSlotSpec& sl) {
    const std::string base = base_filename_only(orig_name);
    if (!base.empty()) {
        const std::string low_picked = to_lower_copy(base);
        for (const char* a : sl.aliases) {
            if (a && to_lower_copy(a) == low_picked) {
                return sl.case_insensitive ? std::string(a) : to_lower_copy(a);
            }
        }
    }
    (void)m;
    return sl.case_insensitive ? std::string(sl.canonical) : to_lower_copy(sl.canonical);
}

bool write_binary_file(const fs::path& dst, const uint8_t* data, size_t size, std::string& err_out) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) {
        err_out = "Failed to create directory: " + ec.message();
        return false;
    }
    std::ofstream ofs(dst, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        err_out = "Failed to open file for writing";
        return false;
    }
    if (size > 0 && data) {
        ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    ofs.close();
    if (!ofs) {
        err_out = "Write failed";
        return false;
    }
    return true;
}

const RomSlotSpec* find_slot_by_id(const MachineDoc& m, const std::string& slot_id) {
    for (const auto& sl : m.slots) {
        if (slot_id == sl.id) return &sl;
    }
    return nullptr;
}

std::pair<const MachineDoc*, const RomSlotSpec*> find_matching_slot(
    const std::string& filename,
    const std::vector<const MachineDoc*>& scope) {
    const std::string low = to_lower_copy(base_filename_only(filename));
    if (low.empty()) return {nullptr, nullptr};
    for (const MachineDoc* m : scope) {
        for (const auto& sl : m->slots) {
            if (to_lower_copy(sl.canonical) == low) return {m, &sl};
            for (const char* a : sl.aliases) {
                if (a && to_lower_copy(a) == low) return {m, &sl};
            }
        }
    }
    return {nullptr, nullptr};
}

uint16_t read_le16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_le32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

#ifdef CZX_HAVE_ZLIB
bool inflate_raw_deflate(const uint8_t* src,
                         size_t src_len,
                         size_t expected_hint,
                         std::vector<uint8_t>& out) {
    out.clear();
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return false;
    }
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    zs.avail_in = static_cast<uInt>(src_len);

    const size_t init_cap = std::clamp<size_t>(expected_hint > 0 ? expected_hint : (src_len * 4u), 4096u, kMaxEntryBytes);
    out.reserve(init_cap);
    std::array<uint8_t, 16384> chunk{};
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out = chunk.data();
        zs.avail_out = static_cast<uInt>(chunk.size());
        ret = inflate(&zs, Z_NO_FLUSH);
        const size_t produced = chunk.size() - zs.avail_out;
        if (out.size() + produced > kMaxEntryBytes) {
            inflateEnd(&zs);
            return false;
        }
        out.insert(out.end(), chunk.data(), chunk.data() + produced);
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}
#endif

}  // namespace

const std::vector<MachineDoc>& get_machine_docs() {
    static const std::vector<MachineDoc> kDocs = {
        {
            "apple2",
            "Apple II",
            "applewin",
            "apple2",
            "Case-insensitive",
            nullptr,
            "media/apple2/",
            ".dsk .do .po .nib .woz .2mg .hdv .zip",
            "3 ROMs above required before boot.",
            "2 drives supported; use Menu for Disk 1/2.",
            {
                {"a2_main", "Apple2e_Enhanced.rom", "", true, true, "Main Enhanced IIe ROM", {}},
                {"a2_vid", "Apple2e_Enhanced_Video.rom", "", true, true, "Character / video ROM", {}},
                {"a2_disk", "DISK2.rom", "", true, true, "Disk II controller ROM", {"DISK2.BIN"}},
            },
        },
        {
            "c64",
            "Commodore 64",
            "vice_x64 / x64sc",
            "vice",
            "EXACT lowercase",
            nullptr,
            "media/c64/",
            ".d64 .g64 .x64 .d71 .d81 .prg .crt .t64 .tap .zip",
            "Put C64 ROMs in C64/, drive ROM in DRIVES/.",
            "Region (PAL/NTSC) & x64/x64sc switch in Menu.",
            {
                {"c64_kernal", "kernal-901227-03.bin", "C64", true, false, "Lowercase in C64/", {"kernal", "kernal.bin", "kernal-901227-03"}},
                {"c64_basic", "basic-901226-01.bin", "C64", true, false, "Lowercase in C64/", {"basic", "basic.bin", "basic-901226-01"}},
                {"c64_chargen", "chargen-901225-01.bin", "C64", true, false, "Lowercase in C64/", {"chargen", "chargen.bin", "chargen-901225-01"}},
                {"c64_1541", "dos1541ii-251968-03.bin", "DRIVES", true, false, "Lowercase in DRIVES/", {"dos1541ii", "dos1541", "dos1541-ii.rom", "1541-ii.bin"}},
            },
        },
        {
            "quasi88",
            "PC-8801",
            "quasi88",
            "quasi88",
            "Case-insensitive",
            "PSEUDO",
            "media/pc88/",
            ".d88 .88d .t88 .cmt .m3u",
            "Boots standalone with built-in pseudo BIOS.",
            "Real ROMs unlock N88-BASIC, Kanji & F12 games.",
            {
                {"q88_n88", "N88.ROM", "", false, true, "N88-BASIC v1/v2 ROM", {}},
                {"q88_n88_0", "N88_0.ROM", "", false, true, "4th ROM bank 0", {}},
                {"q88_n88_1", "N88_1.ROM", "", false, true, "4th ROM bank 1", {}},
                {"q88_n88_2", "N88_2.ROM", "", false, true, "4th ROM bank 2", {}},
                {"q88_n88_3", "N88_3.ROM", "", false, true, "4th ROM bank 3", {}},
                {"q88_n88n", "N88N.ROM", "", false, true, "N-BASIC mode ROM", {}},
                {"q88_disk", "DISK.ROM", "", false, true, "5-inch floppy subsystem", {}},
                {"q88_font", "FONT.ROM", "", false, true, "ANK / semigraphics font", {}},
                {"q88_kanji1", "KANJI1.ROM", "", false, true, "JIS Kanji ROM level 1", {}},
                {"q88_kanji2", "KANJI2.ROM", "", false, true, "JIS Kanji ROM level 2", {}},
            },
        },
        {
            "np2kai",
            "PC-9801",
            "np2kai",
            "np2kai",
            "EXACT lowercase",
            "HLE",
            "media/pc98/",
            ".d88 .fdi .hdm .nhd .hdi .thd .nfd .98d .m3u",
            "Boots with HLE BIOS; real ROMs use lowercase.",
            "WARN: Avoid d8000.rom unless booting an HDD!",
            {
                {"np2_bios", "bios.rom", "", false, false, "ITF + System BIOS (lowercase)", {}},
                {"np2_itf", "itf.rom", "", false, false, "ITF ROM if split", {}},
                {"np2_sound", "sound.rom", "", false, false, "PC-9801-26K/86 OPNA BIOS", {}},
                {"np2_font", "font.rom", "", false, false, "or font.bmp (JIS font)", {"font.bmp"}},
                {"np2_9821", "bios9821.rom", "", false, false, "Optional PC-9821 BIOS", {}},
            },
        },
        {
            "bluemsx",
            "MSX / MSX2+",
            "bluemsx",
            "bluemsx",
            "Case-insensitive",
            "C-BIOS",
            "media/msx/",
            ".rom .mx1 .mx2 .dsk .cas .m3u",
            "Bundled C-BIOS runs carts; real ROMs auto-detect.",
            "Floppy (.dsk) needs PANASONIC/PHILIPSDISK.rom.",
            {
                {"msx_2p", "MSX2P.rom", "Machines/Shared Roms", false, true, "MSX2+ main BIOS", {}},
                {"msx_2pext", "MSX2PEXT.rom", "Machines/Shared Roms", false, true, "MSX2+ sub/ExtROM", {}},
                {"msx_2", "MSX2.rom", "Machines/Shared Roms", false, true, "MSX2 main BIOS", {}},
                {"msx_2ext", "MSX2EXT.rom", "Machines/Shared Roms", false, true, "MSX2 sub/ExtROM", {}},
                {"msx_1", "MSX.rom", "Machines/Shared Roms", false, true, "MSX1 BIOS (standalone)", {}},
                {"msx_pdisk", "PANASONICDISK.rom", "Machines/Shared Roms", false, true, "Needed for .dsk floppies", {}},
                {"msx_phdisk", "PHILIPSDISK.rom", "Machines/Shared Roms", false, true, "Alternate .dsk Disk BIOS", {}},
                {"msx_music", "MSX2PMUS.rom", "Machines/Shared Roms", false, true, "MSX-MUSIC FM-PAC ROM", {}},
                {"msx_xbasic", "XBASIC2.rom", "Machines/Shared Roms", false, true, "MSX2+ XBASIC2 ROM", {}},
                {"msx_kanji", "KANJI.rom", "Machines/Shared Roms", false, true, "MSX Kanji ROM", {"MSXKANJI.rom"}},
                {"msx_moon", "MOONSOUND.rom", "Machines/Shared Roms", false, true, "OPL4 MoonSound BIOS", {}},
            },
        },
        {
            "g800",
            "PC-G850 / E200",
            "g800",
            "g800",
            "Case-insensitive",
            "COMPAT",
            "media/g800/",
            ".ihx .hex .bas .txt",
            "Boots built-in compat BASIC/IOCS immediately.",
            "Per-model dirs g850/, g815/, e200/ checked 1st.",
            {
                {"g8_base", "base.bin", "", false, true, "or base.txt (SHARP ROM)", {"base.txt"}},
                {"g8_rom0", "rom00.bin", "", false, true, "or rom00.txt (ext bank 0)", {"rom00.txt"}},
                {"g8_rom1", "rom01.bin", "", false, true, "or rom01.txt (ext bank 1)", {"rom01.txt"}},
            },
        },
        {
            "emuz700",
            "MZ-700",
            "emuz700",
            "mz700",
            "Case-insensitive",
            nullptr,
            "media/mz700/",
            ".mzt .mzf .m12 .t77 .q20 .d88 .2d .wav",
            "Requires 1 IPL alias + 1 FONT alias.",
            "F6=KANA, F7=EISUU, F8=GRAPH, ESC=BREAK.",
            {
                {"mz_ipl", "IPL.ROM", "", true, true, "1Z-009A/B, NEWMON7.ROM", {"1Z-009A.ROM", "1Z-009B.ROM", "1Z009A.ROM", "1Z009B.ROM", "MZ700IPL.ROM", "NEWMON7.ROM"}},
                {"mz_font", "FONT.ROM", "", true, true, "MZ700FON.JP/.DAT/.ROM", {"MZ700FON.JP", "MZ700FON.DAT", "MZ700FON.ROM", "CGROM.ROM", "MZ700FONT.ROM"}},
            },
        },
        {
            "fm7",
            "FM-7 / FM77AV",
            "efm7 / 77av / 40",
            "fm7",
            "Case-insensitive",
            nullptr,
            "media/fm7/",
            ".d77 .77d .2d .t77",
            "Switch FM-7 / FM77AV / FM77AV40 in Menu.",
            "77AV/AV40 also need INITIATE.ROM & SUBSYSCG.ROM.",
            {
                {"fm_subc", "SUBSYS_C.ROM", "", true, true, "Required (all models)", {}},
                {"fm_fbas", "FBASIC30.ROM", "", true, true, "or 300/301/302 (all models)", {"FBASIC300.ROM", "FBASIC301.ROM", "FBASIC302.ROM"}},
                {"fm_boot", "BOOT_BAS.ROM", "", true, true, "Required for FM-7", {"BOOT_DOS.ROM", "BOOT_TL11_11.ROM"}},
                {"fm_init", "INITIATE.ROM", "", false, true, "Required for FM77AV / AV40", {}},
                {"fm_cg", "SUBSYSCG.ROM", "", false, true, "Required for FM77AV / AV40", {}},
                {"fm_kanji", "KANJI.ROM", "", false, true, "Optional Kanji ROM", {}},
                {"fm_suba", "SUBSYS_A.ROM", "", false, true, "Optional AV40 400-line ROM", {"SUBSYS_B.ROM", "EXTSUB.ROM", "DICROM.ROM"}},
            },
        },
        {
            "msx68k",
            "X68000",
            "msx68k",
            "x68000",
            "Case-insensitive",
            nullptr,
            "media/x68000/",
            ".dim .xdf .hdm .2hd .hdf .hds .m3u",
            "IPLROM.DAT (or 30) + CGROM.DAT required.",
            "Optional Roland MT-32 / CM-32L MIDI in Menu.",
            {
                {"x68_ipl", "IPLROM.DAT", "", true, true, "or IPLROM30.DAT", {"IPLROM30.DAT"}},
                {"x68_cg", "CGROM.DAT", "", true, true, "Character font ROM", {}},
                {"x68_scsi", "SCSIINROM.DAT", "", false, true, "Optional internal/ext SCSI", {"SCSIEXROM.DAT", "IPLROMXV.DAT", "IPLROMCO.DAT"}},
            },
        },
        {
            "dosbox_pure",
            "IBM PC / DOS",
            "dosbox_pure",
            "dos",
            "No BIOS needed",
            "READY",
            "media/dos/",
            ".zip .dosz .exe .com .bat .ima .img .iso .chd",
            "Boots straight into DOS; .zip runs directly!",
            "Put SoundFonts (.sf2) in roms/dos/ if desired.",
            {},
        },
        {
            "mt32",
            "Roland MT-32",
            "munt synth",
            "mt32",
            "Case-insensitive",
            "OPT",
            "",
            "PC-98 / X68000 / DOS MIDI out",
            "CM32L_CONTROL + PCM (or MT32_CONTROL + PCM).",
            "Enable MT-32 synth inside the System Menu.",
            {
                {"mt_ctrl", "CM32L_CONTROL.ROM", "", false, true, "Preferred CM-32L control ROM", {"MT32_CONTROL.ROM"}},
                {"mt_pcm", "CM32L_PCM.ROM", "", false, true, "Preferred CM-32L wave ROM", {"MT32_PCM.ROM"}},
            },
        },
    };
    return kDocs;
}

const MachineDoc* find_machine_doc(const std::string& id) {
    for (const auto& d : get_machine_docs()) {
        if (id == d.id) return &d;
    }
    return nullptr;
}

std::vector<MachineStatus> inspect_machines(const std::string& data_dir) {
    const auto& docs = get_machine_docs();
    std::vector<MachineStatus> out(docs.size());
    const fs::path roms_root = fs::path(data_dir) / "roms";
    for (size_t i = 0; i < docs.size(); ++i) {
        const MachineDoc& m = docs[i];
        MachineStatus st{};
        st.slots.resize(m.slots.size());
        for (size_t s = 0; s < m.slots.size(); ++s) {
            const fs::path dir = slot_dir(data_dir, m, m.slots[s]);
            const RomSlotStatus ss = find_slot_file(dir, m.slots[s]);
            st.slots[s] = ss;
            if (m.slots[s].required) {
                ++st.req_total;
                if (ss.installed) ++st.req_found;
            } else {
                ++st.opt_total;
                if (ss.installed) ++st.opt_found;
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
        (void)roms_root;
        out[i] = std::move(st);
    }
    return out;
}

bool install_single_rom(const std::string& data_dir,
                        const std::string& machine_id,
                        const std::string& slot_id,
                        const std::string& orig_filename,
                        const uint8_t* data,
                        size_t size,
                        std::string& saved_rel_path,
                        std::string& err_out) {
    const MachineDoc* m = find_machine_doc(machine_id);
    if (!m) {
        err_out = "Unknown machine: " + machine_id;
        return false;
    }
    const RomSlotSpec* sl = find_slot_by_id(*m, slot_id);
    if (!sl) {
        err_out = "Unknown ROM slot: " + slot_id;
        return false;
    }
    if (size == 0 || size > kMaxEntryBytes) {
        err_out = "Invalid ROM payload size";
        return false;
    }
    const std::string fname = resolve_save_filename(orig_filename, *m, *sl);
    const fs::path dst = slot_dir(data_dir, *m, *sl) / fname;
    if (!write_binary_file(dst, data, size, err_out)) {
        return false;
    }
    std::string rel = std::string("roms/") + m->roms_sub_dir + "/";
    if (sl->rel_dir && sl->rel_dir[0] != '\0') {
        rel += sl->rel_dir;
        rel += "/";
    }
    rel += fname;
    saved_rel_path = std::move(rel);
    return true;
}

bool delete_slot_rom(const std::string& data_dir,
                     const std::string& machine_id,
                     const std::string& slot_id) {
    const MachineDoc* m = find_machine_doc(machine_id);
    if (!m) return false;
    const RomSlotSpec* sl = find_slot_by_id(*m, slot_id);
    if (!sl) return false;
    const fs::path dir = slot_dir(data_dir, *m, *sl);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;

    std::vector<std::string> wanted;
    wanted.push_back(to_lower_copy(sl->canonical));
    for (const char* a : sl->aliases) {
        if (a && a[0] != '\0') wanted.push_back(to_lower_copy(a));
    }

    bool removed = false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fname = entry.path().filename().string();
        bool match = false;
        if (sl->case_insensitive) {
            match = (std::find(wanted.begin(), wanted.end(), to_lower_copy(fname)) != wanted.end());
        } else {
            if (fname == sl->canonical) match = true;
            for (const char* a : sl->aliases) {
                if (a && fname == a) match = true;
            }
        }
        if (match && fs::remove(entry.path(), ec)) {
            removed = true;
        }
    }
    return removed;
}

ImportReport import_rom_upload(const std::string& data_dir,
                               const std::string& scope_machine_id,
                               const std::string& filename,
                               const uint8_t* data,
                               size_t size) {
    ImportReport report{};
    std::vector<const MachineDoc*> scope;
    if (scope_machine_id.empty() || scope_machine_id == "all" || scope_machine_id == "auto") {
        for (const auto& d : get_machine_docs()) {
            scope.push_back(&d);
        }
    } else if (const MachineDoc* m = find_machine_doc(scope_machine_id)) {
        scope.push_back(m);
    } else {
        report.warnings.push_back("Unknown machine scope: " + scope_machine_id);
        return report;
    }

    const bool is_zip_magic = (size >= 4 && data && data[0] == 0x50 && data[1] == 0x4B &&
                               data[2] == 0x03 && data[3] == 0x04);
    if (!ends_with_ci(filename, ".zip") && !is_zip_magic) {
        const std::string base = base_filename_only(filename);
        if (base.empty() || base[0] == '.') {
            ++report.skipped;
            return report;
        }
        if (to_lower_copy(base) == "d8000.rom") {
            report.warnings.push_back("Skipped d8000.rom (can hang PC-98 floppy boots)");
            ++report.skipped;
            return report;
        }
        const auto [m, sl] = find_matching_slot(base, scope);
        if (!m || !sl) {
            ++report.skipped;
            return report;
        }
        std::string saved;
        std::string err;
        if (install_single_rom(data_dir, m->id, sl->id, base, data, size, saved, err)) {
            report.placed.push_back(std::string(m->name) + ": " + saved);
        } else {
            report.warnings.push_back(base + ": " + err);
        }
        return report;
    }

    // Locate ZIP End of Central Directory (0x06054b50)
    if (!data || size < 22) {
        report.warnings.push_back("Invalid ZIP archive");
        return report;
    }
    const size_t min_off = (size > 65558u) ? (size - 65558u) : 0u;
    size_t eocd_off = size;
    for (size_t off = size - 22u;; --off) {
        if (read_le32(data + off) == 0x06054b50u) {
            eocd_off = off;
            break;
        }
        if (off == min_off) break;
    }
    if (eocd_off == size) {
        report.warnings.push_back("ZIP End-Of-Central-Directory not found");
        return report;
    }

    const uint16_t entry_count = read_le16(data + eocd_off + 10);
    const uint32_t cd_size = read_le32(data + eocd_off + 12);
    const uint32_t cd_offset = read_le32(data + eocd_off + 16);
    if (static_cast<size_t>(cd_offset) + cd_size > size) {
        report.warnings.push_back("Corrupt ZIP central directory");
        return report;
    }

    size_t pos = cd_offset;
    for (uint16_t i = 0; i < entry_count && pos + 46u <= size; ++i) {
        if (read_le32(data + pos) != 0x02014b50u) break;
        const uint16_t method = read_le16(data + pos + 10);
        const uint32_t comp_size = read_le32(data + pos + 20);
        const uint32_t uncomp_size = read_le32(data + pos + 24);
        const uint16_t name_len = read_le16(data + pos + 28);
        const uint16_t extra_len = read_le16(data + pos + 30);
        const uint16_t comment_len = read_le16(data + pos + 32);
        const uint32_t local_off = read_le32(data + pos + 42);
        if (pos + 46u + name_len > size) break;

        const std::string entry_name(reinterpret_cast<const char*>(data + pos + 46), name_len);
        pos += 46u + name_len + extra_len + comment_len;

        if (entry_name.empty() || entry_name.back() == '/' || entry_name.rfind("__MACOSX/", 0) == 0) {
            continue;
        }
        const std::string base = base_filename_only(entry_name);
        if (base.empty() || base[0] == '.') {
            continue;
        }
        if (to_lower_copy(base) == "d8000.rom") {
            report.warnings.push_back("Skipped d8000.rom (can hang PC-98 floppy boots)");
            ++report.skipped;
            continue;
        }
        const auto [m, sl] = find_matching_slot(base, scope);
        if (!m || !sl) {
            ++report.skipped;
            continue;
        }
        if (uncomp_size > kMaxEntryBytes || comp_size > kMaxEntryBytes) {
            report.warnings.push_back(base + ": file exceeds 32MB limit");
            ++report.skipped;
            continue;
        }
        if (static_cast<size_t>(local_off) + 30u > size || read_le32(data + local_off) != 0x04034b50u) {
            report.warnings.push_back(base + ": corrupt local ZIP header");
            ++report.skipped;
            continue;
        }
        const uint16_t l_name = read_le16(data + local_off + 26);
        const uint16_t l_extra = read_le16(data + local_off + 28);
        const size_t payload_off = static_cast<size_t>(local_off) + 30u + l_name + l_extra;
        if (payload_off + comp_size > size) {
            report.warnings.push_back(base + ": truncated ZIP payload");
            ++report.skipped;
            continue;
        }

        const uint8_t* payload = data + payload_off;
        std::vector<uint8_t> inflated;
        const uint8_t* out_ptr = nullptr;
        size_t out_len = 0;

        if (method == 0) {
            out_ptr = payload;
            out_len = comp_size;
        } else if (method == 8) {
#ifdef CZX_HAVE_ZLIB
            if (!inflate_raw_deflate(payload, comp_size, uncomp_size, inflated)) {
                report.warnings.push_back(base + ": deflate decompression failed");
                ++report.skipped;
                continue;
            }
            out_ptr = inflated.data();
            out_len = inflated.size();
#else
            report.warnings.push_back(base + ": deflated ZIP requires zlib support");
            ++report.skipped;
            continue;
#endif
        } else {
            report.warnings.push_back(base + ": unsupported ZIP compression method");
            ++report.skipped;
            continue;
        }

        std::string saved;
        std::string err;
        if (install_single_rom(data_dir, m->id, sl->id, base, out_ptr, out_len, saved, err)) {
            report.placed.push_back(std::string(m->name) + ": " + saved);
        } else {
            report.warnings.push_back(base + ": " + err);
        }
    }

    return report;
}

std::vector<MediaFileInfo> list_media_files(const std::string& data_dir, const std::string& machine_id) {
    std::vector<MediaFileInfo> out;
    const MachineDoc* m = find_machine_doc(machine_id);
    if (!m || !m->media_dir || m->media_dir[0] == '\0') return out;
    const fs::path dir = fs::path(data_dir) / m->media_dir;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const uintmax_t sz = entry.file_size(ec);
        out.push_back({entry.path().filename().string(), ec ? 0ull : static_cast<uint64_t>(sz)});
    }
    std::sort(out.begin(), out.end(), [](const MediaFileInfo& a, const MediaFileInfo& b) {
        return to_lower_copy(a.name) < to_lower_copy(b.name);
    });
    return out;
}

bool install_media_file(const std::string& data_dir,
                        const std::string& machine_id,
                        const std::string& filename,
                        const uint8_t* data,
                        size_t size,
                        std::string& saved_name,
                        std::string& err_out) {
    const MachineDoc* m = find_machine_doc(machine_id);
    if (!m || !m->media_dir || m->media_dir[0] == '\0') {
        err_out = "Machine has no media directory";
        return false;
    }
    const std::string base = base_filename_only(filename);
    if (base.empty() || base[0] == '.') {
        err_out = "Invalid filename";
        return false;
    }
    if (size == 0 || size > kMaxEntryBytes * 2u) {
        err_out = "Invalid file size";
        return false;
    }
    const fs::path dst = fs::path(data_dir) / m->media_dir / base;
    if (!write_binary_file(dst, data, size, err_out)) {
        return false;
    }
    saved_name = base;
    return true;
}

bool delete_media_file(const std::string& data_dir,
                       const std::string& machine_id,
                       const std::string& filename) {
    const MachineDoc* m = find_machine_doc(machine_id);
    if (!m || !m->media_dir || m->media_dir[0] == '\0') return false;
    const std::string base = base_filename_only(filename);
    if (base.empty() || base[0] == '.') return false;
    std::error_code ec;
    return fs::remove(fs::path(data_dir) / m->media_dir / base, ec);
}

}  // namespace czx::core
