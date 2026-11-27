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
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::fprintf(stderr, "CHECK FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            std::abort();                                                                     \
        }                                                                                     \
    } while (0)

namespace {

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

void test_catalog_and_zip(const fs::path& tmp_dir) {
    const auto& docs = czx::core::get_machine_docs();
    CHECK(docs.size() == 11);
    CHECK(czx::core::find_machine_doc("quasi88") != nullptr);
    CHECK(czx::core::find_machine_doc("np2kai") != nullptr);
    CHECK(czx::core::find_machine_doc("does_not_exist") == nullptr);

    std::string saved;
    std::string err;
    const std::vector<uint8_t> rom_bytes = {0x11, 0x22, 0x33, 0x44};
    CHECK(czx::core::install_single_rom(
        tmp_dir.string(), "quasi88", "q88_n88", "n88.rom", rom_bytes.data(), rom_bytes.size(), saved, err));
    CHECK(saved == "roms/quasi88/N88.ROM");
    CHECK(fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));

    // Apple II required check: index 0 has req_total == 3.
    CHECK(czx::core::install_single_rom(
        tmp_dir.string(), "apple2", "a2_main", "Apple2e_Enhanced.rom", rom_bytes.data(), rom_bytes.size(), saved, err));
    auto statuses = czx::core::inspect_machines(tmp_dir.string());
    CHECK(statuses[0].req_found == 1);
    CHECK(statuses[2].opt_found == 1);
    CHECK(czx::core::delete_slot_rom(tmp_dir.string(), "quasi88", "q88_n88"));
    CHECK(!fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));

    // ZIP with Stored (IPLROM.DAT -> x68000), Deflate (N88_0.ROM -> quasi88),
    // D8000.ROM warning, and Zip Slip traversal entry safely sanitized.
    const std::vector<uint8_t> x68_bytes(64, 0x55);
    const std::vector<uint8_t> n88_0_bytes(128, 0x77);
    const std::vector<uint8_t> bad_bytes = {'X'};
    const auto zip = build_zip({
        {"subdir/IPLROM.DAT", x68_bytes, false},
        {"N88_0.ROM", n88_0_bytes, true},
        {"D8000.ROM", bad_bytes, false},
        {"../../etc/passwd", bad_bytes, false},
    });

    const auto rep = czx::core::import_rom_upload(tmp_dir.string(), "auto", "pack.zip", zip.data(), zip.size());
    CHECK(rep.placed.size() == 2);
    CHECK(fs::exists(tmp_dir / "roms/x68000/IPLROM.DAT"));
    CHECK(fs::exists(tmp_dir / "roms/quasi88/N88_0.ROM"));
    CHECK(!fs::exists(tmp_dir / "etc/passwd"));
    CHECK(rep.skipped == 2);
    CHECK(!rep.warnings.empty());
}

void test_web_server(const fs::path& tmp_dir) {
    czx::core::SetupWebServer server;
    CHECK(server.start(tmp_dir.string(), 0));
    CHECK(server.is_running());
    const uint16_t port = server.port();
    CHECK(port != 0);
    CHECK(!server.urls().empty());

    const auto home = http_request(port, "GET", "/");
    CHECK(home.status == 200);
    CHECK(home.body.find("CZX Setup") != std::string::npos);

    const auto status = http_request(port, "GET", "/api/status");
    CHECK(status.status == 200);
    CHECK(status.body.find("\"id\":\"quasi88\"") != std::string::npos);

    const std::vector<uint8_t> n88_data = {0xAA, 0xBB, 0xCC, 0xDD};
    const auto post_rom = http_request(
        port, "POST", "/api/rom?machine=quasi88&slot=q88_n88&filename=n88.rom", n88_data.data(), n88_data.size());
    CHECK(post_rom.status == 200);
    CHECK(post_rom.body.find("\"ok\":true") != std::string::npos);
    CHECK(fs::exists(tmp_dir / "roms/quasi88/N88.ROM"));
    CHECK(server.upload_count() == 1);

    const std::vector<uint8_t> disk_data = {0x01, 0x02, 0x03};
    const auto post_media = http_request(
        port, "POST", "/api/media?machine=quasi88&filename=../game.d88", disk_data.data(), disk_data.size());
    CHECK(post_media.status == 200);
    CHECK(fs::exists(tmp_dir / "media/pc88/game.d88"));

    const auto zip = build_zip({{"FONT.ROM", std::vector<uint8_t>(32, 0x33), true}});
    const auto post_zip = http_request(
        port, "POST", "/api/upload?machine=np2kai&filename=pc98_roms.zip", zip.data(), zip.size());
    CHECK(post_zip.status == 200);
    CHECK(fs::exists(tmp_dir / "roms/np2kai/font.rom"));

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
    const fs::path tmp_dir = fs::temp_directory_path() / ("czx_test_setup_web_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    test_catalog_and_zip(tmp_dir / "cat");
    test_web_server(tmp_dir / "web");

    fs::remove_all(tmp_dir, ec);
    std::printf("test_setup_web: all assertions passed.\n");
    return 0;
}
