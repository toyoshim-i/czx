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

uint32_t rotl32(uint32_t x, unsigned n) noexcept {
    return (x << n) | (x >> (32u - n));
}

void append_sha1_block(const uint8_t block[64], uint32_t state[5]) noexcept {
    uint32_t w[80]{};
    for (size_t i = 0; i < 16; ++i) {
        const size_t j = i * 4u;
        w[i] = (static_cast<uint32_t>(block[j]) << 24) |
               (static_cast<uint32_t>(block[j + 1u]) << 16) |
               (static_cast<uint32_t>(block[j + 2u]) << 8) |
               static_cast<uint32_t>(block[j + 3u]);
    }
    for (size_t i = 16; i < 80; ++i) {
        w[i] = rotl32(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1u);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (size_t i = 0; i < 80; ++i) {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        const uint32_t temp = rotl32(a, 5u) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30u);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
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

std::string sha1_hex(const uint8_t* data, size_t size) {
    uint32_t state[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    uint8_t block[64]{};
    size_t offset = 0;

    while (size - offset >= 64u) {
        append_sha1_block(data + offset, state);
        offset += 64u;
    }

    const size_t rem = size - offset;
    if (rem > 0u && data) {
        std::memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80u;
    if (rem >= 56u) {
        append_sha1_block(block, state);
        std::memset(block, 0, sizeof(block));
    }

    const uint64_t bit_len = static_cast<uint64_t>(size) * 8ull;
    for (size_t i = 0; i < 8u; ++i) {
        block[56u + i] = static_cast<uint8_t>((bit_len >> ((7u - i) * 8u)) & 0xffull);
    }
    append_sha1_block(block, state);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(40, '0');
    for (size_t i = 0; i < 5u; ++i) {
        for (size_t nib = 0; nib < 8u; ++nib) {
            const unsigned shift = static_cast<unsigned>((7u - nib) * 4u);
            out[i * 8u + nib] = kHex[(state[i] >> shift) & 0xfu];
        }
    }
    return out;
}

const std::vector<KnownRomEntry>& get_known_rom_hashes() {
    static const std::vector<KnownRomEntry> kHashes = {
        // Apple II (AppleWin)
        {"b8ea90abe135a0031065e01697c4a3a20d51198b", 16384u, "apple2", "a2_main", "Apple2e_Enhanced.rom", "Apple IIe Enhanced System ROM"},
        {"b2b5d87f52693817fc747df087a4aa1ddcdb1f10", 4096u, "apple2", "a2_vid", "Apple2e_Enhanced_Video.rom", "Apple IIe Enhanced Video ROM"},
        {"d4181c9f046aafc3fb326b381baac809d9e38d16", 256u, "apple2", "a2_disk", "DISK2.rom", "Apple II Disk II 16-sector ROM"},

        // Commodore 64 (VICE)
        {"1d503e56df85a62fee696e7618dc5b4e781df1bb", 8192u, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.3 (901227-03)"},
        {"0e2e4ee3f2d41f00bed72f9ab588b83e306fdb13", 8192u, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.2 (901227-02)"},
        {"87cc04d61fc748b82df09856847bb5c2754a2033", 8192u, "c64", "c64_kernal", "kernal-901227-03.bin", "C64 Kernal Rev.1 (901227-01)"},
        {"79015323128650c742a3694c9429aa91f355905e", 8192u, "c64", "c64_basic", "basic-901226-01.bin", "C64 BASIC V2 (901226-01)"},
        {"adc7c31e18c7c7413d54802ef2f4193da14711aa", 4096u, "c64", "c64_chargen", "chargen-901225-01.bin", "C64 Chargen (901225-01)"},
        {"d3b78c3dbac55f5199f33f3fe0036439811f7fb3", 16384u, "c64", "c64_1541", "dos1541ii-251968-03.bin", "Commodore 1541-II DOS (251968-03)"},
        {"ab16f56989b27d89babe5f89c5a8cb3da71a82f0", 16384u, "c64", "c64_1541", "dos1541ii-251968-03.bin", "Commodore 1541 DOS (325302-01+901229-05)"},

        // PC-8801 (QUASI88)
        {"3518193b8207bdebf22c1380c2db8c554baff329", 32768u, "quasi88", "q88_n88", "N88.ROM", "PC-8801 N88-BASIC ROM (MAME)"},
        {"3b31fc68fa7f47b21c1a1cb027b86b9e87afbfff", 32768u, "quasi88", "q88_n88", "N88.ROM", "PC-8801mkIISR N88-BASIC ROM"},
        {"d1ae642aed4f0584eeb81ff50180db694e5101d4", 8192u, "quasi88", "q88_n88_0", "N88_0.ROM", "PC-8801 4th ROM Bank 0 (MAME)"},
        {"d239c26ad7ac5efac6e947b0e9549b1534aa970d", 8192u, "quasi88", "q88_n88_0", "N88_0.ROM", "PC-8801mkIISR 4th ROM Bank 0"},
        {"4ae4d37409ff99411a623da9f6a44192170a854e", 8192u, "quasi88", "q88_n88_1", "N88_1.ROM", "PC-8801 4th ROM Bank 1 (Libretro)"},
        {"8528eef7946edf6501a6ccb1f416b60c64efac7c", 8192u, "quasi88", "q88_n88_1", "N88_1.ROM", "PC-8801mkIISR 4th ROM Bank 1"},
        {"e94278682ef9e9bbb82201f72c50382748dcea2a", 8192u, "quasi88", "q88_n88_2", "N88_2.ROM", "PC-8801 4th ROM Bank 2 (Libretro)"},
        {"b7c8bcea219b77d9cc3ee0efafe343cc307425d1", 8192u, "quasi88", "q88_n88_2", "N88_2.ROM", "PC-8801mkIISR 4th ROM Bank 2"},
        {"af452aed79b072c4d17985830b7c5dca64d4b412", 8192u, "quasi88", "q88_n88_3", "N88_3.ROM", "PC-8801 4th ROM Bank 3 (Libretro)"},
        {"efce0b51cab9f0da6cf68507757f1245a2867a72", 8192u, "quasi88", "q88_n88_3", "N88_3.ROM", "PC-8801mkIISR 4th ROM Bank 3"},
        {"063609dd518c124a4fc9ba35d1bae35771666a34", 32768u, "quasi88", "q88_n88n", "N88N.ROM", "PC-8801 N-BASIC ROM (MAME)"},
        {"5b922ed9de07d2a729bdf1da7b57c50ddf08809a", 32768u, "quasi88", "q88_n88n", "N88N.ROM", "PC-8801mkIISR N-BASIC ROM"},
        {"bb7103a0818850a039c67ff666a31ce49a8d516f", 2048u, "quasi88", "q88_disk", "DISK.ROM", "PC-8801 Floppy Subsystem ROM"},
        {"84b90f69671d4b72e8f219e1fe7cd667e976cf7f", 2048u, "quasi88", "q88_font", "FONT.ROM", "PC-8801 Font ROM"},
        {"82e11a177af6a5091dd67f50a2f4bafda84d6556", 131072u, "quasi88", "q88_kanji1", "KANJI1.ROM", "PC-8801 Kanji ROM Level 1"},
        {"b86b932cb4ce34472e0fe0f01a4a4350e15dcb53", 131072u, "quasi88", "q88_kanji2", "KANJI2.ROM", "PC-8801 Kanji ROM Level 2"},

        // PC-9801 (NP2kai)
        {"76a7360502fa0296ea93b4c537174610a834d367", 98304u, "np2kai", "np2_bios", "bios.rom", "PC-9801 System BIOS"},
        {"a2fb11c000ed7c976520622cfb7940ed6ddc904e", 32768u, "np2kai", "np2_itf", "itf.rom", "PC-9801 ITF ROM"},
        {"d5dbc4fea3b8367024d363f5351baecd6adcd8ef", 16384u, "np2kai", "np2_sound", "sound.rom", "PC-9801-26K/86 Sound BIOS"},
        {"78ba9960f135372825ab7244b5e4e73a810002ff", 288768u, "np2kai", "np2_font", "font.rom", "PC-9801 Font ROM"},
        {"b4f14e58030ed40fff2dc312b58ea4440bdf8cc5", 524350u, "np2kai", "np2_font", "font.bmp", "PC-9801 Font BMP"},

        // MSX / MSX2+ (blueMSX Shared Roms)
        {"e998f0c441f4f1800ef44e42cd1659150206cf79", 32768u, "bluemsx", "msx_1", "MSX.rom", "MSX1 BIOS"},
        {"6103b39f1e38d1aa2d84b1c3219c44f1abb5436e", 32768u, "bluemsx", "msx_2", "MSX2.rom", "MSX2 Main BIOS"},
        {"5c1f9c7fb655e43d38e5dd1fcc6b942b2ff68b02", 16384u, "bluemsx", "msx_2ext", "MSX2EXT.rom", "MSX2 Sub/ExtROM"},
        {"e2fbd56e42da637609d23ae9df9efd1b4241b18a", 32768u, "bluemsx", "msx_2p", "MSX2P.rom", "MSX2+ Main BIOS (Libretro)"},
        {"e90f80a61d94c617850c415e12ad70ac41e66bb7", 32768u, "bluemsx", "msx_2p", "MSX2P.rom", "MSX2+ Main BIOS (blueMSX)"},
        {"fe0254cbfc11405b79e7c86c7769bd6322b04995", 16384u, "bluemsx", "msx_2pext", "MSX2PEXT.rom", "MSX2+ Sub/ExtROM"},
        {"7ed7c55e0359737ac5e68d38cb6903f9e5d7c2b6", 16384u, "bluemsx", "msx_pdisk", "PANASONICDISK.rom", "Panasonic MSX Disk BIOS"},
        {"c3efedda7ab947a06d9345f7b8261076fa7ceeef", 16384u, "bluemsx", "msx_phdisk", "PHILIPSDISK.rom", "Philips MSX Disk BIOS"},
        {"6354ccc5c100b1c558c9395fa8c00784d2e9b0a3", 16384u, "bluemsx", "msx_music", "MSX2PMUS.rom", "MSX-MUSIC FM-PAC BIOS"},
        {"47370bec7ca1f0615a54eda548b07fbc0c7ef398", 16384u, "bluemsx", "msx_xbasic", "XBASIC2.rom", "MSX2+ XBASIC2 ROM"},
        {"32760893ce06dbe3930627755ba065cc3d8ec6ca", 2097152u, "bluemsx", "msx_moon", "MOONSOUND.rom", "OPL4 MoonSound Wave ROM (YRW801)"},

        // SHARP MZ-700 (EmuZ-700)
        {"7b10d7965c541393e33a265bcf71a00314d2db7a", 4096u, "emuz700", "mz_ipl", "1Z-009B.ROM", "MZ-700 Monitor 1Z-009B (JP)"},
        {"bd2cc750f2d2f63e50a59786668509e81a276e32", 4096u, "emuz700", "mz_font", "MZ700FON.JP", "MZ-700 CGROM (JP)"},
        {"5eb1d42d273b1fd2cab120486279ab8ff6c85dc7", 4096u, "emuz700", "mz_font", "MZ700FON.JP", "MZ-700 CGROM (JP alt)"},
        {"5128ad179a702f8e0bd9910a58bad8fbe4c20167", 4096u, "emuz700", "mz_font", "MZ700FON.DAT", "MZ-700 CGROM (INT)"},

        // FM-7 / FM77AV / FM77AV40
        {"50b7283db6fe1342c6063fc94046283f4feddc1c", 10240u, "fm7", "fm_subc", "SUBSYS_C.ROM", "FM-7/77AV Subsys C ROM"},
        {"8d5f0cfe7e0d39bf2ab7e4c798a13004769c28b2", 31744u, "fm7", "fm_fbas", "FBASIC30.ROM", "FM-7/77AV F-BASIC V3.0 ROM"},
        {"53b63a301cba7e3030e79c59a4d4291eab6e64b0", 512u, "fm7", "fm_boot", "BOOT_BAS.ROM", "FM-7 BASIC Boot ROM"},
        {"037e5881bd3fed472a210ee894a6446965a8d2ef", 512u, "fm7", "fm_boot", "BOOT_DOS.ROM", "FM-NEW7 DOS Boot ROM"},
        {"616c17155f84fb0e3731a31ef0eb0cbb664a5600", 512u, "fm7", "fm_boot", "BOOT_DOS.ROM", "FM-7 DOS Boot ROM"},
        {"b65987e98a9564a82c85eadb86f0204eee5a5c93", 8192u, "fm7", "fm_init", "INITIATE.ROM", "FM77AV Initiate ROM"},
        {"8ab466b1546d023ba54987790a79e9815d2b7bb2", 8192u, "fm7", "fm_cg", "SUBSYSCG.ROM", "FM77AV Subsys CG ROM"},
        {"bf52d22b119d54410dad4949b0687bb0edf3e143", 131072u, "fm7", "fm_kanji", "KANJI.ROM", "FM-7/77AV Kanji ROM Level 1"},
        {"038cb0b42aee9e933b20fccd6f19942e2f476c83", 8192u, "fm7", "fm_suba", "SUBSYS_A.ROM", "FM77AV Subsys A ROM"},
        {"0305bdd44e7d9b7b6a17675aff0a3330a08d21a8", 8192u, "fm7", "fm_suba", "SUBSYS_B.ROM", "FM77AV Subsys B ROM"},
        {"a1304457eeb400b4edd3c20af948d66a04df255e", 49152u, "fm7", "fm_suba", "EXTSUB.ROM", "FM77AV40 ExtSub ROM"},
        {"fe9f92a8a2750bcba0a1d2895e75e83858e4f97f", 262144u, "fm7", "fm_suba", "DICROM.ROM", "FM77AV40 Dictionary ROM"},

        // SHARP X68000 (PX68k / msx68k)
        {"0ed038ed2133b9f78c6e37256807424e0d927560", 131072u, "msx68k", "x68_ipl", "IPLROM.DAT", "X68000 IPL-ROM v1.0"},
        {"239e9124568c862c31d9ec0605e32373ea74b86a", 131072u, "msx68k", "x68_ipl", "IPLROM30.DAT", "X68030 IPL-ROM v1.3"},
        {"e33cdcdb69cd257b0b211ef46e7a8b144637db57", 131072u, "msx68k", "x68_scsi", "IPLROMXV.DAT", "X68000 XVI IPL-ROM v1.1"},
        {"77511fc58798404701f66b6bbc9cbde06596eba7", 131072u, "msx68k", "x68_scsi", "IPLROMCO.DAT", "X68000 Compact IPL-ROM v1.2"},
        {"8d72c5b4d63bb14c5dbdac495244d659aa1498b6", 786432u, "msx68k", "x68_cg", "CGROM.DAT", "X68000 CGROM"},
        {"3f063d4231cdf53da6adc4db96533725e260076a", 8192u, "msx68k", "x68_scsi", "SCSIINROM.DAT", "X68000 Internal SCSI ROM"},

        // Roland MT-32 / CM-32L (munt)
        {"73683d585cd6948cc19547942ca0e14a0319456d", 65536u, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32L/LAPC-I Control v1.00"},
        {"a439fbb390da38cada95a7cbb1d6ca199cd66ef8", 65536u, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32L/LAPC-I Control v1.02"},
        {"dc1c5b1b90a4646d00f7daf3679733c7badc7077", 65536u, "mt32", "mt_ctrl", "CM32L_CONTROL.ROM", "CM-32LN/CM-500 Control v1.00"},
        {"5a5cb5a77d7d55ee69657c2f870416daed52dea7", 65536u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.04"},
        {"e17a3a6d265bf1fa150312061134293d2b58288c", 65536u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.05"},
        {"a553481f4e2794c10cfe597fef154eef0d8257de", 65536u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.06"},
        {"b083518fffb7f66b03c23b7eb4f868e62dc5a987", 65536u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v1.07"},
        {"7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92", 65536u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control BlueRidge"},
        {"5837064c9df4741a55f7c4d8787ac158dff2d3ce", 131072u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.03"},
        {"2c16432b6c73dd2a3947cba950a0f4c19d6180eb", 131072u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.04"},
        {"2869cf4c235d671668cfcb62415e2ce8323ad4ed", 131072u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.06"},
        {"47b52adefedaec475c925e54340e37673c11707c", 131072u, "mt32", "mt_ctrl", "MT32_CONTROL.ROM", "MT-32 Control v2.07"},
        {"289cc298ad532b702461bfc738009d9ebe8025ea", 1048576u, "mt32", "mt_pcm", "CM32L_PCM.ROM", "CM-32L/CM-64/LAPC-I PCM ROM"},
        {"f6b1eebc4b2d200ec6d3d21d51325d5b48c60252", 524288u, "mt32", "mt_pcm", "MT32_PCM.ROM", "MT-32 PCM ROM"},
    };
    return kHashes;
}

const KnownRomEntry* identify_rom_by_hash(const uint8_t* data,
                                          size_t size,
                                          const std::string& scope_machine_id) {
    if (!data || size == 0) return nullptr;
    const bool filter_machine = !scope_machine_id.empty() &&
                                scope_machine_id != "all" &&
                                scope_machine_id != "auto";
    const auto& hashes = get_known_rom_hashes();
    bool any_size_match = false;
    for (const auto& e : hashes) {
        if (e.size_bytes == size && (!filter_machine || scope_machine_id == e.machine_id)) {
            any_size_match = true;
            break;
        }
    }
    if (!any_size_match) return nullptr;

    const std::string digest = sha1_hex(data, size);
    for (const auto& e : hashes) {
        if (e.size_bytes == size &&
            (!filter_machine || scope_machine_id == e.machine_id) &&
            digest == e.sha1) {
            return &e;
        }
    }
    return nullptr;
}

const KnownRomEntry* identify_rom_file_by_hash(const std::string& file_path,
                                               const std::string& scope_machine_id) {
    std::error_code ec;
    const fs::path p(file_path);
    if (!fs::is_regular_file(p, ec) || ec) return nullptr;
    const uintmax_t sz = fs::file_size(p, ec);
    if (ec || sz == 0 || sz > kMaxEntryBytes) return nullptr;

    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return nullptr;
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!ifs) return nullptr;
    return identify_rom_by_hash(buf.data(), buf.size(), scope_machine_id);
}

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
    const bool all_machines = scope_machine_id.empty() || scope_machine_id == "all" || scope_machine_id == "auto";
    if (!all_machines && !find_machine_doc(scope_machine_id)) {
        report.warnings.push_back("Unknown machine scope: " + scope_machine_id);
        return report;
    }

    auto place_matched = [&](const KnownRomEntry& hit, const std::string& raw_name,
                             const uint8_t* payload, size_t payload_len) {
        const MachineDoc* m = find_machine_doc(hit.machine_id);
        if (!m) {
            ++report.skipped;
            return;
        }
        std::string saved;
        std::string err;
        if (install_single_rom(data_dir, hit.machine_id, hit.slot_id, hit.save_name,
                               payload, payload_len, saved, err)) {
            report.placed.push_back(std::string(m->name) + ": " + saved + " [" + hit.label + "]");
        } else {
            report.warnings.push_back(raw_name + ": " + err);
        }
    };

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
        if (!data || size == 0 || size > kMaxEntryBytes) {
            report.warnings.push_back(base + ": invalid file size");
            ++report.skipped;
            return report;
        }
        const KnownRomEntry* hit = identify_rom_by_hash(data, size, scope_machine_id);
        if (!hit) {
            const std::string digest = sha1_hex(data, size);
            report.warnings.push_back(
                base + ": unknown SHA-1 (" + digest.substr(0, 8) +
                "...) - use per-slot manual upload for unlisted dumps");
            ++report.skipped;
            return report;
        }
        place_matched(*hit, base, data, size);
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
    size_t unknown_warnings = 0;
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
            ++report.skipped;
            continue;
        }
        if (to_lower_copy(base) == "d8000.rom") {
            report.warnings.push_back("Skipped d8000.rom (can hang PC-98 floppy boots)");
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

        const KnownRomEntry* hit = identify_rom_by_hash(out_ptr, out_len, scope_machine_id);
        if (!hit) {
            ++report.skipped;
            if (unknown_warnings < 8u) {
                const std::string digest = sha1_hex(out_ptr, out_len);
                report.warnings.push_back(
                    base + ": unknown SHA-1 (" + digest.substr(0, 8) + "...)");
            } else if (unknown_warnings == 8u) {
                report.warnings.push_back(
                    "More unlisted ROMs omitted from warnings; use per-slot manual upload if needed");
            }
            ++unknown_warnings;
            continue;
        }

        place_matched(*hit, base, out_ptr, out_len);
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
