#include "core/d88_image.hpp"
#include "core/libretro_core.hpp"
#include "video/file_browser.hpp"
#include "video/scaler.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> make_dummy_d88_disk(const std::string& sjis_name, uint8_t media_type, uint32_t size) {
    CHECK(size >= 0x2B0u);
    std::vector<uint8_t> buf(size, 0);
    const size_t n = std::min<size_t>(16u, sjis_name.size());
    std::memcpy(buf.data(), sjis_name.data(), n);
    buf[0x1B] = media_type;
    buf[0x1C] = static_cast<uint8_t>(size & 0xFFu);
    buf[0x1D] = static_cast<uint8_t>((size >> 8) & 0xFFu);
    buf[0x1E] = static_cast<uint8_t>((size >> 16) & 0xFFu);
    buf[0x1F] = static_cast<uint8_t>((size >> 24) & 0xFFu);
    buf[0x20] = 0xB0;  // track 0 offset
    buf[0x21] = 0x02;
    return buf;
}

czx_key_event_t key_press(czx_key_t k) {
    czx_key_event_t ev{};
    ev.key = k;
    ev.pressed = true;
    return ev;
}

void write_d88_file(const fs::path& path, const std::vector<std::vector<uint8_t>>& imgs) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    CHECK(ofs.is_open());
    for (const auto& img : imgs) {
        ofs.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    }
    CHECK(ofs.good());
}

void test_file_browser_d88_first() {
    const fs::path tmp_dir = fs::temp_directory_path() / "czx_test_browser_d88";
    const fs::path media_dir = tmp_dir / "media";
    const fs::path cache_dir = tmp_dir / "cache";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(media_dir, ec);
    fs::create_directories(cache_dir, ec);

    const fs::path alpha3 = media_dir / "alpha3.d88";
    const fs::path beta2 = media_dir / "beta2.d88";
    const fs::path zeta1 = media_dir / "zeta1.d88";

    write_d88_file(alpha3, {
        make_dummy_d88_disk("Disk 1", 0x00, 0x300u),
        make_dummy_d88_disk("Disk 2", 0x00, 0x300u),
        make_dummy_d88_disk("Disk 3", 0x00, 0x300u),
    });
    write_d88_file(beta2, {
        make_dummy_d88_disk("Beta 1", 0x00, 0x300u),
        make_dummy_d88_disk("Beta 2", 0x00, 0x300u),
    });
    write_d88_file(zeta1, {
        make_dummy_d88_disk("Single", 0x00, 0x300u),
    });

    czx::video::FileBrowser browser;
    czx::video::D88PickerHint hint{alpha3.string(), 0, 1, 1};

    // Opens directly into D88 picker with focus on Disk 2 (index 1); DOWN -> Disk 3 -> ENTER selects it.
    browser.open(media_dir.string(), "d88|m3u", "Drive 2", cache_dir.string(), false, hint);
    CHECK(browser.is_open());

    std::vector<uint16_t> fb(static_cast<size_t>(czx::video::kLcdWidth * czx::video::kLcdHeight), 0);
    browser.render(fb.data());
    int nonzero = 0;
    for (uint16_t px : fb) {
        if (px != 0u) ++nonzero;
    }
    CHECK(nonzero > 1000);

    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_ENTER)) == czx::video::FileBrowserAction::Selected);
    CHECK(!browser.is_open());
    CHECK(browser.selected_d88_source_path() == alpha3.string());
    CHECK(browser.selected_d88_disk_index() == 2);
    CHECK(browser.selected_label() == "Disk 3");
    CHECK(!browser.selected_path().empty());

    // Navigating to final item [Browse other files...] and pressing ENTER opens directory list.
    browser.open(media_dir.string(), "d88|m3u", "Drive 2", cache_dir.string(), true, hint);
    CHECK(browser.is_open());
    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_ENTER)) == czx::video::FileBrowserAction::None);
    CHECK(browser.is_open());
    // In media_dir: [0]="..", [1]="alpha3.d88" (saved cursor), [2]="beta2.d88".
    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_ENTER)) == czx::video::FileBrowserAction::None);
    CHECK(browser.is_open());
    // Now in beta2.d88 picker: [0]=[AUTO], [1]=Beta 1.
    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_ENTER)) == czx::video::FileBrowserAction::Selected);
    CHECK(!browser.is_open());
    CHECK(browser.selected_d88_source_path() == beta2.string());
    CHECK(browser.selected_d88_disk_index() == 0);
    CHECK(browser.selected_label() == "Beta 1");

    // Z switches from direct D88 picker back to file list; ESC cancels.
    browser.open(media_dir.string(), "d88|m3u", "Drive 2", cache_dir.string(), false, hint);
    CHECK(browser.handle_key(key_press(CZX_KEY_Z)) == czx::video::FileBrowserAction::None);
    CHECK(browser.is_open());
    CHECK(browser.handle_key(key_press(CZX_KEY_ESC)) == czx::video::FileBrowserAction::Cancelled);
    CHECK(!browser.is_open());

    // 1-disk .d88 hint goes straight to the normal file list (cursor 0="..", DOWN -> alpha3.d88 -> ENTER opens D88 picker).
    czx::video::D88PickerHint single_hint{zeta1.string(), 0, -1, 0};
    browser.open(media_dir.string(), "d88|m3u", "Drive 1", cache_dir.string(), true, single_hint);
    CHECK(browser.is_open());
    CHECK(browser.handle_key(key_press(CZX_KEY_DOWN)) == czx::video::FileBrowserAction::None);
    CHECK(browser.handle_key(key_press(CZX_KEY_ENTER)) == czx::video::FileBrowserAction::None);
    CHECK(browser.is_open());
    browser.close();

    fs::remove_all(tmp_dir, ec);
}

void test_quasi88_hot_swap() {
    std::string so_path;
    for (const char* cand : {"build/cores/quasi88_libretro.so",
                             "cores/quasi88_libretro.so",
                             "../build/cores/quasi88_libretro.so"}) {
        std::error_code ec;
        if (fs::is_regular_file(cand, ec)) {
            so_path = fs::weakly_canonical(cand, ec).string();
            if (so_path.empty()) so_path = cand;
            break;
        }
    }
    if (so_path.empty()) {
        std::printf("test_d88: skipping live QUASI88 hot-swap check (quasi88_libretro.so not built)\n");
        return;
    }

    const fs::path tmp_dir = fs::temp_directory_path() / "czx_test_q88_hotswap";
    const fs::path data_dir = tmp_dir / "data";
    const fs::path cache_dir = tmp_dir / "cache";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(data_dir, ec);
    fs::create_directories(cache_dir, ec);

    const fs::path d88_path = tmp_dir / "multi3.d88";
    write_d88_file(d88_path, {
        make_dummy_d88_disk("Disk 1", 0x00, 0x300u),
        make_dummy_d88_disk("Disk 2", 0x00, 0x300u),
        make_dummy_d88_disk("Disk 3", 0x00, 0x300u),
    });

    std::vector<czx::core::D88SubDisk> disks;
    CHECK(czx::core::inspect_d88(d88_path.string(), disks) && disks.size() == 3u);
    const std::string ext2 = czx::core::extract_d88_subdisk(d88_path.string(), disks[1], cache_dir.string());
    const std::string ext3 = czx::core::extract_d88_subdisk(d88_path.string(), disks[2], cache_dir.string());
    CHECK(!ext2.empty() && !ext3.empty());

    const char* prev_dd_env = std::getenv("CZX_DATA_DIR");
    const std::string prev_dd = prev_dd_env ? prev_dd_env : "";
    setenv("CZX_DATA_DIR", data_dir.c_str(), 1);

    czx::core::LibretroCoreLoader loader;
    CHECK(loader.load(so_path));
    const czx_core_api_t* api = loader.api();
    CHECK(api != nullptr);

    std::vector<std::string> logs;
    auto on_video = [](void*, const czx_frame_t*) {};
    auto on_audio = [](void*, const int16_t*, size_t) {};
    auto on_log = [](void* ud, int, const char* msg) {
        if (ud && msg) {
            static_cast<std::vector<std::string>*>(ud)->emplace_back(msg);
        }
    };

    czx_host_t host{};
    host.user_data = &logs;
    host.video_frame = on_video;
    host.audio_samples = on_audio;
    host.log = on_log;
    host.initial_media = d88_path.c_str();

    czx_core_instance_t* inst = api->create(api, &host);
    CHECK(inst != nullptr);
    if (api->set_data_dir) {
        api->set_data_dir(inst, data_dir.c_str());
    }
    for (int i = 0; i < 15; ++i) {
        api->run_frame(inst);
    }

    const size_t logs_after_boot = logs.size();
    CHECK(api->load_media_in_drive(inst, 1u, ext3.c_str(), "Disk 3"));
    CHECK(std::string(api->get_drive_media_label(inst, 1u)) == "Disk 3");
    CHECK(logs.size() == logs_after_boot);

    for (int i = 0; i < 5; ++i) {
        api->run_frame(inst);
    }
    CHECK(api->eject_drive(inst, 1u));
    CHECK(std::string(api->get_drive_media_label(inst, 1u)) == "Empty");
    CHECK(logs.size() == logs_after_boot);

    for (int i = 0; i < 5; ++i) {
        api->run_frame(inst);
    }
    CHECK(api->load_media_in_drive(inst, 0u, ext2.c_str(), "Disk 2"));
    CHECK(std::string(api->get_drive_media_label(inst, 0u)) == "Disk 2");
    CHECK(logs.size() == logs_after_boot);

    for (int i = 0; i < 5; ++i) {
        api->run_frame(inst);
    }
    api->destroy(inst);

    if (!prev_dd.empty()) {
        setenv("CZX_DATA_DIR", prev_dd.c_str(), 1);
    } else {
        unsetenv("CZX_DATA_DIR");
    }
    fs::remove_all(tmp_dir, ec);
}

}  // namespace

int main() {
    CHECK(czx::core::is_d88_ext("foo/ゲーム.D88"));
    CHECK(czx::core::is_d88_ext("bar.88d"));
    CHECK(czx::core::is_d88_ext("foo/ゲーム.D77"));
    CHECK(czx::core::is_d88_ext("bar.77d"));
    CHECK(!czx::core::is_d88_ext("bar.d64"));

    const fs::path tmp_dir = fs::temp_directory_path() / "czx_test_d88";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    const fs::path d88_path = tmp_dir / "テスト.d88";
    auto d1 = make_dummy_d88_disk("\x83\x56\x83\x58\x83\x65\x83\x80", 0x00, 0x300u);  // "システム" (2D)
    auto d2 = make_dummy_d88_disk("\x83\x66\x81\x5B\x83\x5E", 0x20, 0x340u);          // "データ" (2HD)
    d2[0x1A] = 0x10;
    {
        std::ofstream ofs(d88_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(d1.data()), static_cast<std::streamsize>(d1.size()));
        ofs.write(reinterpret_cast<const char*>(d2.data()), static_cast<std::streamsize>(d2.size()));
    }

    std::vector<czx::core::D88SubDisk> disks;
    CHECK(czx::core::inspect_d88(d88_path.string(), disks));
    CHECK(disks.size() == 2u);
    CHECK(disks[0].name_utf8 == "システム");
    CHECK(std::strcmp(disks[0].media_type_label(), "2D") == 0);
    CHECK(!disks[0].write_protected);
    CHECK(disks[1].name_utf8 == "データ");
    CHECK(std::strcmp(disks[1].media_type_label(), "2HD") == 0);
    CHECK(disks[1].write_protected);

    const fs::path cache_dir = tmp_dir / "cache";
    const std::string ext1 = czx::core::extract_d88_subdisk(d88_path.string(), disks[0], cache_dir.string());
    const std::string ext2 = czx::core::extract_d88_subdisk(d88_path.string(), disks[1], cache_dir.string());
    CHECK(!ext1.empty() && !ext2.empty() && ext1 != ext2);
    CHECK(fs::file_size(ext1) == 0x300u);
    CHECK(fs::file_size(ext2) == 0x340u);

    // Modifying cached sub-disk must be preserved (re-extract must not clobber).
    {
        std::ofstream ofs(ext2, std::ios::binary | std::ios::in | std::ios::out);
        ofs.seekp(0x2B0);
        const char marker[4] = {'S', 'A', 'V', 'E'};
        ofs.write(marker, 4);
    }
    const std::string ext2_again = czx::core::extract_d88_subdisk(d88_path.string(), disks[1], cache_dir.string());
    CHECK(ext2_again == ext2);
    {
        std::ifstream ifs(ext2_again, std::ios::binary);
        ifs.seekg(0x2B0);
        char marker[4]{};
        ifs.read(marker, 4);
        CHECK(std::memcmp(marker, "SAVE", 4) == 0);
    }

    // Truncated file fails cleanly.
    const fs::path bad_path = tmp_dir / "bad.d88";
    {
        std::ofstream ofs(bad_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(d1.data()), 0x2D0);
    }
    CHECK(!czx::core::inspect_d88(bad_path.string(), disks));

    fs::remove_all(tmp_dir, ec);

    test_file_browser_d88_first();
    test_quasi88_hot_swap();
    return 0;
}
