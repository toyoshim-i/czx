#include "core/rom_catalog.hpp"
#include "core/setup_web_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef CZX_SOURCE_DIR
#define CZX_SOURCE_DIR "."
#endif

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            std::abort();                                                                     \
        }                                                                                     \
    } while (0)

namespace {

std::vector<uint8_t> read_fixture(const fs::path& rel) {
    const fs::path p = fs::path(CZX_SOURCE_DIR) / rel;
    std::ifstream ifs(p, std::ios::binary);
    CHECK(static_cast<bool>(ifs));
    ifs.seekg(0, std::ios::end);
    const std::streamoff len = ifs.tellg();
    CHECK(len > 0);
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> out(static_cast<size_t>(len));
    ifs.read(reinterpret_cast<char*>(out.data()), len);
    CHECK(static_cast<bool>(ifs));
    return out;
}

void append_le16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void append_le32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

std::vector<uint8_t> raw_deflate(const std::vector<uint8_t>& src) {
    z_stream zs{};
    const int init_rc = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    CHECK(init_rc == Z_OK);
    zs.next_in = const_cast<Bytef*>(src.data());
    zs.avail_in = static_cast<uInt>(src.size());
    std::vector<uint8_t> out(src.size() + 64u);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    CHECK(rc == Z_STREAM_END);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return out;
}

struct ZipEntryInput {
    std::string name;
    std::vector<uint8_t> data;
    bool use_deflate = false;
};

std::vector<uint8_t> build_zip(const std::vector<ZipEntryInput>& entries) {
    std::vector<uint8_t> zip;
    struct CentralRec {
        std::string name;
        uint16_t method;
        uint32_t crc;
        uint32_t comp_size;
        uint32_t uncomp_size;
        uint32_t local_off;
    };
    std::vector<CentralRec> centrals;

    for (const auto& e : entries) {
        const uint16_t method = e.use_deflate ? 8u : 0u;
        const std::vector<uint8_t> payload = e.use_deflate ? raw_deflate(e.data) : e.data;
        const uint32_t crc = static_cast<uint32_t>(
            crc32(0L, e.data.data(), static_cast<uInt>(e.data.size())));
        const uint32_t local_off = static_cast<uint32_t>(zip.size());

        append_le32(zip, 0x04034b50u);
        append_le16(zip, 20u);
        append_le16(zip, 0u);
        append_le16(zip, method);
        append_le16(zip, 0u);
        append_le16(zip, 0u);
        append_le32(zip, crc);
        append_le32(zip, static_cast<uint32_t>(payload.size()));
        append_le32(zip, static_cast<uint32_t>(e.data.size()));
        append_le16(zip, static_cast<uint16_t>(e.name.size()));
        append_le16(zip, 0u);
        zip.insert(zip.end(), e.name.begin(), e.name.end());
        zip.insert(zip.end(), payload.begin(), payload.end());

        centrals.push_back({
            e.name,
            method,
            crc,
            static_cast<uint32_t>(payload.size()),
            static_cast<uint32_t>(e.data.size()),
            local_off,
        });
    }

    const uint32_t cd_off = static_cast<uint32_t>(zip.size());
    for (const auto& c : centrals) {
        append_le32(zip, 0x02014b50u);
        append_le16(zip, 20u);
        append_le16(zip, 20u);
        append_le16(zip, 0u);
        append_le16(zip, c.method);
        append_le16(zip, 0u);
        append_le16(zip, 0u);
        append_le32(zip, c.crc);
        append_le32(zip, c.comp_size);
        append_le32(zip, c.uncomp_size);
        append_le16(zip, static_cast<uint16_t>(c.name.size()));
        append_le16(zip, 0u);
        append_le16(zip, 0u);
        append_le16(zip, 0u);
        append_le16(zip, 0u);
        append_le32(zip, 0u);
        append_le32(zip, c.local_off);
        zip.insert(zip.end(), c.name.begin(), c.name.end());
    }
    const uint32_t cd_size = static_cast<uint32_t>(zip.size()) - cd_off;

    append_le32(zip, 0x06054b50u);
    append_le16(zip, 0u);
    append_le16(zip, 0u);
    append_le16(zip, static_cast<uint16_t>(centrals.size()));
    append_le16(zip, static_cast<uint16_t>(centrals.size()));
    append_le32(zip, cd_size);
    append_le32(zip, cd_off);
    append_le16(zip, 0u);
    return zip;
}

struct HttpResponse {
    int status = 0;
    std::string body;
};

HttpResponse http_request(uint16_t port,
                          const std::string& method,
                          const std::string& target,
                          const uint8_t* body = nullptr,
                          size_t body_len = 0) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = ::connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    CHECK(rc == 0);

    std::string req = method + " " + target + " HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "Content-Length: " + std::to_string(body_len) + "\r\n"
                      "Connection: close\r\n\r\n";

    size_t sent = 0;
    while (sent < req.size()) {
        const ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        CHECK(n > 0);
        sent += static_cast<size_t>(n);
    }
    sent = 0;
    while (sent < body_len) {
        const ssize_t n = ::send(fd, body + sent, body_len - sent, MSG_NOSIGNAL);
        CHECK(n > 0);
        sent += static_cast<size_t>(n);
    }

    std::string raw;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    const size_t hdr_end = raw.find("\r\n\r\n");
    CHECK(hdr_end != std::string::npos);
    HttpResponse res{};
    std::sscanf(raw.c_str(), "HTTP/1.1 %d", &res.status);
    res.body = raw.substr(hdr_end + 4);
    return res;
}

void test_sha1_rfc3174() {
    CHECK(czx::core::sha1_hex(nullptr, 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    const char* abc = "abc";
    CHECK(czx::core::sha1_hex(reinterpret_cast<const uint8_t*>(abc), 3) ==
          "a9993e364706816aba3e25717850c26c9cd0d89d");
}

void test_catalog_and_hash_auto_import(const fs::path& tmp_dir,
                                       const std::vector<uint8_t>& apple2_rom,
                                       const std::vector<uint8_t>& c64_kernal) {
    const auto& docs = czx::core::get_machine_docs();
    CHECK(docs.size() == 11);
    CHECK(czx::core::find_machine_doc("quasi88") != nullptr);
    CHECK(czx::core::find_machine_doc("np2kai") != nullptr);
    CHECK(czx::core::find_machine_doc("does_not_exist") == nullptr);

    // Known real ROM hash lookups (regardless of filename)
    const auto* a2_hit = czx::core::identify_rom_by_hash(apple2_rom.data(), apple2_rom.size());
    CHECK(a2_hit != nullptr);
    CHECK(std::string(a2_hit->machine_id) == "apple2");
    CHECK(std::string(a2_hit->slot_id) == "a2_main");

    // Manual per-slot installation accepts unknown/custom ROM payloads
    std::string saved;
    std::string err;
    const std::vector<uint8_t> custom_bytes = {0x11, 0x22, 0x33, 0x44};
    CHECK(czx::core::install_single_rom(
        tmp_dir.string(), "quasi88", "q88_n88", "custom.rom", custom_bytes.data(), custom_bytes.size(), saved, err));
    CHECK(saved == "roms/quasi88/N88.ROM");
    CHECK(fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));
    CHECK(czx::core::delete_slot_rom(tmp_dir.string(), "quasi88", "q88_n88"));
    CHECK(!fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));

    // Auto-import ZIP MUST identify solely by SHA-1 hash:
    // - Renamed Apple IIe ROM -> roms/apple2/Apple2e_Enhanced.rom
    // - Renamed C64 Kernal ROM (deflated) -> roms/vice/C64/kernal-901227-03.bin
    // - Fake payload named N88.ROM -> MUST BE SKIPPED (not matched by name!)
    // - Fake payload named D8000.ROM -> skipped with warning
    // - Traversal entry ../../etc/passwd -> safely skipped
    const std::vector<uint8_t> fake_n88(128, 0x77);
    const std::vector<uint8_t> bad_bytes = {'X'};
    const auto zip = build_zip({
        {"subdir/random_a2.bin", apple2_rom, false},
        {"renamed_kernal.dat", c64_kernal, true},
        {"N88.ROM", fake_n88, true},
        {"D8000.ROM", bad_bytes, false},
        {"../../etc/passwd", bad_bytes, false},
    });

    const auto rep = czx::core::import_rom_upload(tmp_dir.string(), "auto", "pack.zip", zip.data(), zip.size());
    CHECK(rep.placed.size() == 2);
    CHECK(fs::exists(tmp_dir / "roms/apple2/Apple2e_Enhanced.rom"));
    CHECK(fs::exists(tmp_dir / "roms/vice/C64/kernal-901227-03.bin"));
    CHECK(!fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));
    CHECK(!fs::exists(tmp_dir / "etc/passwd"));
    CHECK(rep.skipped == 3);
    CHECK(!rep.warnings.empty());

    // Scoped single-file auto-import rejects renamed Apple IIe ROM when scoped to c64,
    // and rejects unknown bytes even if named KERNAL.BIN.
    const auto wrong_scope = czx::core::import_rom_upload(
        tmp_dir.string(), "c64", "Apple2e_Enhanced.rom", apple2_rom.data(), apple2_rom.size());
    CHECK(wrong_scope.placed.empty());
    CHECK(wrong_scope.skipped == 1);

    const auto fake_single = czx::core::import_rom_upload(
        tmp_dir.string(), "auto", "N88.ROM", fake_n88.data(), fake_n88.size());
    CHECK(fake_single.placed.empty());
    CHECK(fake_single.skipped == 1);
}

void test_web_server(const fs::path& tmp_dir,
                     const std::vector<uint8_t>& apple2_rom,
                     const std::vector<uint8_t>& c64_kernal) {
    czx::core::SetupWebServer server;
    CHECK(server.start(tmp_dir.string(), 0));
    CHECK(server.is_running());
    const uint16_t port = server.port();
    CHECK(port != 0);
    CHECK(!server.urls().empty());

    const auto home = http_request(port, "GET", "/");
    CHECK(home.status == 200);
    CHECK(home.body.find("CZX Setup") != std::string::npos);

    // Manual per-slot upload of custom/unknown bytes MUST succeed and report verified:false
    const std::vector<uint8_t> custom_n88 = {0xAA, 0xBB, 0xCC, 0xDD};
    const auto post_custom = http_request(
        port, "POST", "/api/rom?machine=quasi88&slot=q88_n88&filename=my_n88.rom",
        custom_n88.data(), custom_n88.size());
    CHECK(post_custom.status == 200);
    CHECK(post_custom.body.find("\"ok\":true") != std::string::npos);
    CHECK(post_custom.body.find("\"verified\":false") != std::string::npos);
    CHECK(fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));
    CHECK(server.upload_count() == 1);

    // Manual per-slot upload of real Apple IIe ROM MUST succeed and report verified:true
    const auto post_verified = http_request(
        port, "POST", "/api/rom?machine=apple2&slot=a2_main&filename=Apple2e_Enhanced.rom",
        apple2_rom.data(), apple2_rom.size());
    CHECK(post_verified.status == 200);
    CHECK(post_verified.body.find("\"verified\":true") != std::string::npos);
    CHECK(fs::exists(tmp_dir / "roms/apple2/Apple2e_Enhanced.rom"));

    // Auto-import ZIP over HTTP: renamed real C64 Kernal matches, fake FONT.ROM skipped
    const std::vector<uint8_t> fake_font(32, 0x33);
    const auto zip = build_zip({
        {"any_name_here.bin", c64_kernal, true},
        {"FONT.ROM", fake_font, true},
    });
    const auto post_zip = http_request(
        port, "POST", "/api/upload?machine=auto&filename=bundle.zip", zip.data(), zip.size());
    CHECK(post_zip.status == 200);
    CHECK(fs::exists(tmp_dir / "roms/vice/C64/kernal-901227-03.bin"));
    CHECK(!fs::exists(tmp_dir / "roms/quasi88/FONT.ROM"));
    CHECK(!fs::exists(tmp_dir / "roms/np2kai/font.rom"));

    // /api/status should reflect verified:true on real Apple IIe ROM and verified:false on custom N88.ROM
    const auto status = http_request(port, "GET", "/api/status");
    CHECK(status.status == 200);
    CHECK(status.body.find("\"id\":\"apple2\"") != std::string::npos);
    CHECK(status.body.find("\"verified\":true") != std::string::npos);
    CHECK(status.body.find("\"verified\":false") != std::string::npos);

    const std::vector<uint8_t> disk_data = {0x01, 0x02, 0x03};
    const auto post_media = http_request(
        port, "POST", "/api/media?machine=quasi88&filename=../game.d88", disk_data.data(), disk_data.size());
    CHECK(post_media.status == 200);
    CHECK(fs::exists(tmp_dir / "media/pc88/game.d88"));

    const auto del_rom = http_request(port, "DELETE", "/api/rom?machine=quasi88&slot=q88_n88");
    CHECK(del_rom.status == 200);
    CHECK(!fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));

    const auto del_media = http_request(port, "DELETE", "/api/media?machine=quasi88&filename=game.d88");
    CHECK(del_media.status == 200);
    CHECK(!fs::exists(tmp_dir / "media/pc88/game.d88"));

    const auto bad_route = http_request(port, "GET", "/nonexistent");
    CHECK(bad_route.status == 404);

    server.stop();
    CHECK(!server.is_running());
}

}  // namespace

int main() {
    test_sha1_rfc3174();

    const auto apple2_rom = read_fixture("third_party/AppleWin/resource/Apple2e_Enhanced.rom");
    const auto c64_kernal = read_fixture("third_party/vice-libretro/vice/data/C64/kernal-901227-03.bin");

    const fs::path tmp_dir = fs::temp_directory_path() / ("czx_test_setup_web_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    test_catalog_and_hash_auto_import(tmp_dir / "cat", apple2_rom, c64_kernal);
    test_web_server(tmp_dir / "web", apple2_rom, c64_kernal);

    fs::remove_all(tmp_dir, ec);
    std::printf("test_setup_web: all assertions passed.\n");
    return 0;
}
