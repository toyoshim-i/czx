#include "core/setup_core.hpp"

#include "core/rom_catalog.hpp"
#include "core/setup_web_server.hpp"
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

#if defined(__ANDROID__)
constexpr int kTabRoms = 0;
constexpr int kTabCtrl = 1;
constexpr int kTabFiles = 2;
constexpr int kTabAbout = 3;
constexpr int kTabCount = 4;
constexpr std::array<const char*, kTabCount> kTabs = {"ROMS", "CTRL", "FILES", "ABOUT"};
#else
constexpr int kTabRoms = 0;
constexpr int kTabWeb = 1;
constexpr int kTabCtrl = 2;
constexpr int kTabFiles = 3;
constexpr int kTabAbout = 4;
constexpr int kTabCount = 5;
constexpr std::array<const char*, kTabCount> kTabs = {"ROMS", "WEB", "CTRL", "FILES", "ABOUT"};
#endif

std::string resolve_default_data_dir() {
    if (const char* env = std::getenv("CZX_DATA_DIR"); env && env[0] != '\0') {
        return std::string(env);
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return (fs::path(home) / "czx").string();
    }
    return "czx_data";
}

struct DetailLine {
    std::string text;
    uint16_t color;
};

struct SetupCoreState {
    czx_host_t host{};
    std::string data_dir;
    int tab = 0;
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
#if !defined(__ANDROID__)
    SetupWebServer web_server;

    ~SetupCoreState() {
        web_server.stop();
    }
#endif

    std::string effective_data_dir() {
        if (data_dir.empty()) {
            data_dir = resolve_default_data_dir();
        }
        return data_dir;
    }

    void refresh_statuses() {
        statuses = inspect_machines(effective_data_dir());
    }

    void on_tab_entered() {
        info_scroll = 0;
#if !defined(__ANDROID__)
        if (tab == kTabWeb && !web_server.is_running()) {
            (void)web_server.start(effective_data_dir(), 8088);
        }
#endif
    }

    std::vector<DetailLine> build_detail_lines(size_t idx) const {
        using czx::video::pack_rgb565;
        const auto& machines = get_machine_docs();
        const MachineDoc& m = machines[idx];
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

        if (!m.slots.empty()) {
            lines.push_back({"", cText});
            lines.push_back({"ROM FILES (*=required):", cHead});
            for (size_t s = 0; s < m.slots.size(); ++s) {
                const RomSlotSpec& sl = m.slots[s];
                const bool ok = st.slots[s].installed;
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

    static uint16_t cMissOrOk(bool highlight) {
        return highlight ? czx::video::pack_rgb565(255, 200, 95)
                         : czx::video::pack_rgb565(225, 232, 240);
    }

    std::vector<DetailLine> build_info_lines(int t) const {
        using czx::video::pack_rgb565;
        const uint16_t cHead = pack_rgb565(120, 225, 255);
        const uint16_t cText = pack_rgb565(225, 232, 240);
        const uint16_t cOk = pack_rgb565(96, 230, 145);
        const uint16_t cMuted = pack_rgb565(165, 180, 200);
        std::vector<DetailLine> lines;

#if !defined(__ANDROID__)
        const uint16_t cWarn = pack_rgb565(255, 195, 85);
        if (t == kTabWeb) {
            if (web_server.is_running()) {
                char hbuf[64];
                std::snprintf(hbuf, sizeof(hbuf), "LAN WEB UPLOAD: RUNNING (port %u)",
                              static_cast<unsigned>(web_server.port()));
                lines.push_back({hbuf, cOk});
                lines.push_back({"Open any URL in a browser on the same LAN:", cHead});
                const auto ulist = web_server.urls();
                const size_t show_n = std::min<size_t>(ulist.size(), 4);
                for (size_t i = 0; i < show_n; ++i) {
                    lines.push_back({std::string("  ") + ulist[i], pack_rgb565(255, 255, 255)});
                }
            } else {
                lines.push_back({"LAN WEB UPLOAD: STOPPED", cWarn});
                lines.push_back({"Press Enter or A button to start server.", cText});
            }
            lines.push_back({"", cText});
            char ubuf[96];
            std::snprintf(ubuf, sizeof(ubuf), "Uploads: %d", web_server.upload_count());
            std::string ev = web_server.last_event();
            if (ev.size() > 35) ev.resize(35);
            lines.push_back({std::string(ubuf) + " | " + ev, cMuted});
            lines.push_back({"", cText});
            lines.push_back({"HOW IT WORKS:", cHead});
            lines.push_back({"  Upload .zip archives or single ROM/media", cText});
            lines.push_back({"  files from PC/phone. Files are sorted into", cText});
            lines.push_back({"  roms/<target>/ and media/<target>/ auto.", cText});
            return lines;
        }
#endif

        if (t == kTabCtrl) {
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
        } else if (t == kTabFiles) {
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
        } else if (t == kTabAbout) {
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

    void handle_nav(int dy, int dx, bool ok, bool back, int pg) {
        const auto& machines = get_machine_docs();
        if (tab == kTabRoms && in_detail) {
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
            tab = (tab + kTabCount + (dx > 0 ? 1 : -1)) % kTabCount;
            on_tab_entered();
            return;
        }

        if (back) {
#if !defined(__ANDROID__)
            web_server.stop();
#endif
            if (host.return_to_launcher) {
                host.return_to_launcher(host.user_data);
            }
            return;
        }

#if !defined(__ANDROID__)
        if (tab == kTabWeb) {
            if (ok) {
                if (web_server.is_running()) {
                    web_server.stop();
                } else {
                    (void)web_server.start(effective_data_dir(), 8088);
                }
            }
            return;
        }
#endif

        if (tab == kTabRoms) {
            const int n = static_cast<int>(machines.size());
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
        const auto& machines = get_machine_docs();
        if (statuses.size() != machines.size()) {
            refresh_statuses();
        }
        fb.resize(static_cast<size_t>(kW * kH));
        std::fill(fb.begin(), fb.end(), pack_rgb565(14, 18, 26));
        czx::video::Rgb565Canvas c(fb.data(), kW, kH);

        // Top tab bar
        c.fill_rect(0, 0, kW, 15, pack_rgb565(22, 32, 48));
        c.draw_hline(0, 15, kW, pack_rgb565(68, 160, 210));
        c.draw_text(5, 4, "SETUP", pack_rgb565(115, 225, 255));

        int tx = 50;
        for (int i = 0; i < kTabCount; ++i) {
            const int tw = static_cast<int>(std::strlen(kTabs[static_cast<size_t>(i)])) * 6 + 8;
            if (i == tab) {
                c.fill_rect(tx, 2, tw, 12, pack_rgb565(46, 108, 152));
                c.draw_rect(tx, 2, tw, 12, pack_rgb565(120, 225, 255));
                c.draw_text(tx + 4, 4, kTabs[static_cast<size_t>(i)], pack_rgb565(255, 255, 255));
            } else {
                c.draw_text(tx + 4, 4, kTabs[static_cast<size_t>(i)], pack_rgb565(165, 180, 200));
            }
            tx += tw + 4;
        }

        if (tab == kTabRoms && !in_detail) {
            constexpr int kVisible = 9;
            const int count = static_cast<int>(machines.size());
            if (machine_cursor < list_scroll) list_scroll = machine_cursor;
            if (machine_cursor >= list_scroll + kVisible) list_scroll = machine_cursor - kVisible + 1;
            list_scroll = std::clamp(list_scroll, 0, std::max(0, count - kVisible));

            int y = 20;
            for (int r = 0; r < kVisible; ++r) {
                const int idx = list_scroll + r;
                if (idx >= count) break;
                const MachineDoc& m = machines[static_cast<size_t>(idx)];
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
            if (tab == kTabRoms) {
                lines = build_detail_lines(static_cast<size_t>(machine_cursor));
                const int max_s = std::max(0, static_cast<int>(lines.size()) - 11);
                detail_scroll = std::clamp(detail_scroll, 0, max_s);
                scroll = detail_scroll;
                c.fill_rect(4, 18, kW - 8, 12, pack_rgb565(28, 42, 62));
                c.draw_text(8, 20, machines[static_cast<size_t>(machine_cursor)].name, pack_rgb565(255, 255, 255));
                c.draw_text(124, 20, machines[static_cast<size_t>(machine_cursor)].subtitle, pack_rgb565(120, 220, 255));
                footer = "Up/Dn:Scroll   Left/B/Esc:Back to List";
            } else {
                lines = build_info_lines(tab);
                const int max_s = std::max(0, static_cast<int>(lines.size()) - 11);
                info_scroll = std::clamp(info_scroll, 0, max_s);
                scroll = info_scroll;
#if !defined(__ANDROID__)
                footer = (tab == kTabWeb)
                             ? "Enter/A:Start/Stop   L/R:Tab   Esc:Exit"
                             : "L/R:Switch Tab   Up/Dn:Scroll   Esc:Exit";
#else
                footer = "L/R:Switch Tab   Up/Dn:Scroll   Esc:Exit";
#endif
            }

            int y = (tab == kTabRoms) ? 34 : 22;
            const int max_rows = (tab == kTabRoms) ? 11 : 12;
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
#if !defined(__ANDROID__)
        st->web_server.stop();
#endif
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

    if ((st->frame_num % 30u) == 0u) {
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
        st->tab = (st->tab + 1) % kTabCount;
        st->in_detail = false;
        st->on_tab_entered();
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
    else if ((rising & CZX_JOY_DOWN) != 0u) dx = 1;
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
