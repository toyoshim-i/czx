#include "app/launcher_state.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace czx::app {

namespace {

std::string to_lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

std::string to_upper_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return s;
}

std::string sanitize_ini_value(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (ch != '\r' && ch != '\n' && ch != '\0') {
            out.push_back(ch);
        }
    }
    return out;
}

std::string trim_whitespace(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

bool parse_bool_val(std::string_view v, bool fallback) noexcept {
    const std::string low = to_lower_copy(trim_whitespace(v));
    if (low == "1" || low == "true" || low == "on" || low == "yes") return true;
    if (low == "0" || low == "false" || low == "off" || low == "no") return false;
    return fallback;
}

int parse_int_val(std::string_view v, int fallback) noexcept {
    try {
        const std::string s = trim_whitespace(v);
        if (s.empty()) return fallback;
        size_t pos = 0;
        const int parsed = std::stoi(s, &pos, 10);
        return (pos == s.size()) ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

uint32_t parse_u32_val(std::string_view v, uint32_t fallback) noexcept {
    try {
        const std::string s = trim_whitespace(v);
        if (s.empty() || s[0] == '-') return fallback;
        size_t pos = 0;
        const unsigned long parsed = std::stoul(s, &pos, 10);
        return (pos == s.size()) ? static_cast<uint32_t>(parsed) : fallback;
    } catch (...) {
        return fallback;
    }
}

const char* display_mode_to_ini(czx::video::DisplayMode m) noexcept {
    switch (m) {
        case czx::video::DisplayMode::NativePan: return "native_pan";
        case czx::video::DisplayMode::OverflowFit: return "overflow_fit";
        case czx::video::DisplayMode::AspectFit: return "aspect_fit";
    }
    return "overflow_fit";
}

czx::video::DisplayMode parse_display_mode(std::string_view v, czx::video::DisplayMode fallback) noexcept {
    const std::string low = to_lower_copy(trim_whitespace(v));
    if (low == "native_pan" || low == "nativepan" || low == "1:1" || low == "0") {
        return czx::video::DisplayMode::NativePan;
    }
    if (low == "overflow_fit" || low == "overflowfit" || low == "fit" || low == "1") {
        return czx::video::DisplayMode::OverflowFit;
    }
    if (low == "aspect_fit" || low == "aspectfit" || low == "asp" || low == "2") {
        return czx::video::DisplayMode::AspectFit;
    }
    return fallback;
}

const char* aspect_policy_to_ini(czx::video::AspectPolicy p) noexcept {
    return (p == czx::video::AspectPolicy::Crt4x3) ? "crt_4x3" : "par_aware";
}

czx::video::AspectPolicy parse_aspect_policy(std::string_view v, czx::video::AspectPolicy fallback) noexcept {
    const std::string low = to_lower_copy(trim_whitespace(v));
    if (low == "crt_4x3" || low == "crt4x3" || low == "4:3" || low == "1") {
        return czx::video::AspectPolicy::Crt4x3;
    }
    if (low == "par_aware" || low == "paraware" || low == "par" || low == "0") {
        return czx::video::AspectPolicy::ParAware;
    }
    return fallback;
}

const char* margin_fill_to_ini(czx::video::MarginFill f) noexcept {
    return (f == czx::video::MarginFill::Black) ? "black" : "auto";
}

czx::video::MarginFill parse_margin_fill(std::string_view v, czx::video::MarginFill fallback) noexcept {
    const std::string low = to_lower_copy(trim_whitespace(v));
    if (low == "black" || low == "1") return czx::video::MarginFill::Black;
    if (low == "auto" || low == "0") return czx::video::MarginFill::Auto;
    return fallback;
}

struct TargetDescriptor {
    std::string id;
    std::string title;
    std::string icon_key;
    std::string variant_option_label;
    std::string variant_id;
    std::string variant_label;
    std::string subtitle;
    std::string menu_name;
};

TargetDescriptor describe_core_id(const std::string& raw_id, const char* raw_name) {
    const std::string id = to_lower_copy(raw_id);
    if (id == "applewin") {
        return {"applewin", "APPLE II", "applewin", "", "", "", "AppleWin", "Apple II (AppleWin)"};
    }
    if (id == "vice_x64") {
        return {"c64", "COMMODORE 64", "c64", "Accuracy", "fast", "Fast", "VICE x64", "C64 (VICE x64)"};
    }
    if (id == "vice_x64sc") {
        return {"c64", "COMMODORE 64", "c64", "Accuracy", "accurate", "CycleAccurate", "VICE x64sc", "C64 (VICE x64sc)"};
    }
    if (id == "quasi88") {
        return {"quasi88", "PC-8801", "quasi88", "", "", "", "QUASI88", "PC-8801 (QUASI88)"};
    }
    if (id == "np2kai") {
        return {"np2kai", "PC-9801", "np2kai", "", "", "", "NP2kai", "PC-9801 (NP2kai)"};
    }
    if (id == "bluemsx") {
        return {"bluemsx", "MSX", "bluemsx", "", "", "", "blueMSX", "MSX (blueMSX)"};
    }
    if (id == "emuz700") {
        return {"emuz700", "MZ-700", "emuz700", "", "", "", "EmuZ-700", "MZ-700 (EmuZ-700)"};
    }
    if (id == "efm7") {
        return {"fm7", "FM-7", "fm7", "Model", "fm7", "FM-7", "eFM-7", "FM-7 (eFM-7)"};
    }
    if (id == "efm77av") {
        return {"fm7", "FM-7", "fm7", "Model", "fm77av", "FM77AV", "eFM77AV", "FM77AV (eFM77AV)"};
    }
    if (id == "efm77av40") {
        return {"fm7", "FM-7", "fm7", "Model", "fm77av40", "FM77AV40", "eFM77AV40", "FM77AV40 (eFM77AV40)"};
    }
    if (id == "msx68k") {
        return {"msx68k", "X68000", "msx68k", "", "", "", "msx68k", "X68000 (msx68k)"};
    }
    if (id == "g800") {
        return {"g800", "PC-G850", "g800", "", "", "", "g800", "PC-G850 (g800)"};
    }
    if (id == "test_pattern") {
        return {"test_pattern", "TEST", "test_pattern", "", "", "", "Test Pattern", "Test Pattern"};
    }

    const std::string fallback_id = !id.empty() ? id : "core";
    const std::string title = !id.empty() ? to_upper_copy(id) : std::string("CORE");
    const std::string mname = (raw_name && raw_name[0] != '\0') ? std::string(raw_name) : title;
    return {fallback_id, title, fallback_id, "", "", "", mname, mname};
}

const std::string& empty_string() noexcept {
    static const std::string kEmpty;
    return kEmpty;
}

}  // namespace

int MachineTarget::clamp_variant_index(int variant_idx) const noexcept {
    if (variants.empty()) return 0;
    if (variant_idx >= 0 && static_cast<size_t>(variant_idx) < variants.size()) {
        return variant_idx;
    }
    if (default_variant_index >= 0 &&
        static_cast<size_t>(default_variant_index) < variants.size()) {
        return default_variant_index;
    }
    return 0;
}

int MachineTarget::find_variant_index(std::string_view vid) const noexcept {
    if (vid.empty()) return -1;
    for (size_t i = 0; i < variants.size(); ++i) {
        if (variants[i].id == vid) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MachineTarget::resolve_core_index(int variant_idx) const noexcept {
    if (variants.empty()) return -1;
    return variants[static_cast<size_t>(clamp_variant_index(variant_idx))].core_index;
}

const std::string& MachineTarget::variant_id(int variant_idx) const noexcept {
    if (variants.empty()) return empty_string();
    return variants[static_cast<size_t>(clamp_variant_index(variant_idx))].id;
}

const std::string& MachineTarget::variant_label(int variant_idx) const noexcept {
    if (variants.empty()) return empty_string();
    return variants[static_cast<size_t>(clamp_variant_index(variant_idx))].label;
}

const std::string& MachineTarget::subtitle(int variant_idx) const noexcept {
    if (variants.empty()) return empty_string();
    return variants[static_cast<size_t>(clamp_variant_index(variant_idx))].subtitle;
}

std::string MachineTarget::menu_core_name(int variant_idx) const {
    if (variants.empty()) return title;
    const auto& v = variants[static_cast<size_t>(clamp_variant_index(variant_idx))];
    return !v.menu_name.empty() ? v.menu_name : title;
}

std::vector<MachineTarget> build_machine_targets(const czx::core::CoreRegistry& registry) {
    std::vector<MachineTarget> targets;
    for (size_t i = 0; i < registry.count(); ++i) {
        const czx_core_api_t* api = registry.get(i);
        const czx_core_desc_t* desc = (api && api->get_desc) ? api->get_desc(api) : nullptr;
        const std::string core_id = (desc && desc->id) ? desc->id : "";
        const char* core_name = (desc && desc->name) ? desc->name : nullptr;
        const TargetDescriptor info = describe_core_id(core_id, core_name);

        TargetVariant variant{
            info.variant_id,
            info.variant_label,
            info.subtitle,
            info.menu_name,
            static_cast<int>(i),
        };

        if (!info.variant_option_label.empty()) {
            const int existing = find_target_index_by_id(targets, info.id);
            MachineTarget* t = nullptr;
            if (existing >= 0) {
                t = &targets[static_cast<size_t>(existing)];
            } else {
                targets.push_back(MachineTarget{
                    info.id,
                    info.title,
                    info.icon_key,
                    info.variant_option_label,
                    {},
                    0,
                });
                t = &targets.back();
            }
            t->variants.push_back(std::move(variant));
            continue;
        }

        targets.push_back(MachineTarget{
            info.id,
            info.title,
            info.icon_key,
            "",
            {std::move(variant)},
            0,
        });
    }

    for (auto& t : targets) {
        if (t.id == "c64") {
            const int fast_idx = t.find_variant_index("fast");
            t.default_variant_index = (fast_idx >= 0) ? fast_idx : 0;
        } else if (t.id == "fm7") {
            const int av40_idx = t.find_variant_index("fm77av40");
            const int av_idx = t.find_variant_index("fm77av");
            if (av40_idx >= 0) {
                t.default_variant_index = av40_idx;
            } else if (av_idx >= 0) {
                t.default_variant_index = av_idx;
            } else {
                t.default_variant_index = 0;
            }
        }
    }

    return targets;
}

int find_target_index_by_id(const std::vector<MachineTarget>& targets, std::string_view id) noexcept {
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find_target_index_for_core_index(const std::vector<MachineTarget>& targets,
                                     int core_index,
                                     int* out_variant_index) noexcept {
    if (core_index < 0) return -1;
    for (size_t i = 0; i < targets.size(); ++i) {
        for (size_t v = 0; v < targets[i].variants.size(); ++v) {
            if (targets[i].variants[v].core_index == core_index) {
                if (out_variant_index) *out_variant_index = static_cast<int>(v);
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

LauncherState load_launcher_state(const std::string& path) {
    LauncherState state{};
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return state;
    }

    enum class SectionKind { None, Launcher, Target };
    SectionKind section = SectionKind::None;
    std::string current_target;

    std::string line;
    while (std::getline(ifs, line)) {
        const std::string trimmed = trim_whitespace(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            const std::string sec = trim_whitespace(std::string_view(trimmed).substr(1, trimmed.size() - 2));
            if (sec == "launcher") {
                section = SectionKind::Launcher;
                current_target.clear();
            } else if (sec.rfind("target.", 0) == 0 && sec.size() > 7u) {
                section = SectionKind::Target;
                current_target = sanitize_ini_value(std::string_view(sec).substr(7));
                if (!current_target.empty()) {
                    (void)state.targets[current_target];
                } else {
                    section = SectionKind::None;
                }
            } else {
                section = SectionKind::None;
                current_target.clear();
            }
            continue;
        }

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim_whitespace(std::string_view(trimmed).substr(0, eq));
        const std::string val = sanitize_ini_value(trim_whitespace(std::string_view(trimmed).substr(eq + 1)));

        if (section == SectionKind::Launcher) {
            if (key == "last_target") {
                state.last_target_id = val;
            }
        } else if (section == SectionKind::Target && !current_target.empty()) {
            TargetSavedState& ts = state.targets[current_target];
            if (key == "media1") {
                ts.media1 = val;
            } else if (key == "media2") {
                ts.media2 = val;
            } else if (key == "media1_label") {
                ts.media1_label = val;
            } else if (key == "media2_label") {
                ts.media2_label = val;
            } else if (key == "d88_source1") {
                ts.d88_source1 = val;
            } else if (key == "d88_index1") {
                ts.d88_index1 = parse_int_val(val, ts.d88_index1);
            } else if (key == "d88_source2") {
                ts.d88_source2 = val;
            } else if (key == "d88_index2") {
                ts.d88_index2 = parse_int_val(val, ts.d88_index2);
            } else if (key == "last_media_dir") {
                ts.last_media_dir = val;
            } else if (key == "preset_index") {
                ts.preset_index = parse_u32_val(val, ts.preset_index);
            } else if (key == "variant_id") {
                ts.variant_id = val;
            } else if (key == "cycle_accurate") {
                if (ts.variant_id.empty() && current_target == "c64") {
                    ts.variant_id = parse_bool_val(val, false) ? "accurate" : "fast";
                }
            } else if (key == "has_display_prefs") {
                ts.has_display_prefs = parse_bool_val(val, ts.has_display_prefs);
            } else if (key == "display_mode") {
                ts.display_mode = parse_display_mode(val, ts.display_mode);
            } else if (key == "desktop_native") {
                ts.desktop_native = parse_bool_val(val, ts.desktop_native);
            } else if (key == "desktop_scale") {
                ts.desktop_scale = std::clamp(parse_int_val(val, ts.desktop_scale), 1, 3);
            } else if (key == "aspect_policy") {
                ts.aspect_policy = parse_aspect_policy(val, ts.aspect_policy);
            } else if (key == "show_borders") {
                ts.show_borders = parse_bool_val(val, ts.show_borders);
            } else if (key == "margin_fill") {
                ts.margin_fill = parse_margin_fill(val, ts.margin_fill);
            } else if (key == "input_mode") {
                ts.input_mode = std::clamp(parse_int_val(val, ts.input_mode), 0, 3);
            }
        }
    }

    return state;
}

bool save_launcher_state(const std::string& path, const LauncherState& state) {
    if (path.empty()) return false;
    std::error_code ec;
    const fs::path target_path(path);
    if (target_path.has_parent_path()) {
        fs::create_directories(target_path.parent_path(), ec);
    }

    const fs::path tmp_path(path + ".tmp");
    {
        std::ofstream ofs(tmp_path, std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << "[launcher]\n";
        ofs << "last_target = " << sanitize_ini_value(state.last_target_id) << "\n";

        for (const auto& [tid, ts] : state.targets) {
            const std::string clean_id = sanitize_ini_value(tid);
            if (clean_id.empty()) continue;
            ofs << "\n[target." << clean_id << "]\n";
            ofs << "media1 = " << sanitize_ini_value(ts.media1) << "\n";
            ofs << "media2 = " << sanitize_ini_value(ts.media2) << "\n";
            ofs << "media1_label = " << sanitize_ini_value(ts.media1_label) << "\n";
            ofs << "media2_label = " << sanitize_ini_value(ts.media2_label) << "\n";
            ofs << "d88_source1 = " << sanitize_ini_value(ts.d88_source1) << "\n";
            ofs << "d88_index1 = " << ts.d88_index1 << "\n";
            ofs << "d88_source2 = " << sanitize_ini_value(ts.d88_source2) << "\n";
            ofs << "d88_index2 = " << ts.d88_index2 << "\n";
            ofs << "last_media_dir = " << sanitize_ini_value(ts.last_media_dir) << "\n";
            ofs << "preset_index = " << ts.preset_index << "\n";
            if (!ts.variant_id.empty()) {
                ofs << "variant_id = " << sanitize_ini_value(ts.variant_id) << "\n";
            }
            ofs << "has_display_prefs = " << (ts.has_display_prefs ? "true" : "false") << "\n";
            ofs << "display_mode = " << display_mode_to_ini(ts.display_mode) << "\n";
            ofs << "desktop_native = " << (ts.desktop_native ? "true" : "false") << "\n";
            ofs << "desktop_scale = " << std::clamp(ts.desktop_scale, 1, 3) << "\n";
            ofs << "aspect_policy = " << aspect_policy_to_ini(ts.aspect_policy) << "\n";
            ofs << "show_borders = " << (ts.show_borders ? "true" : "false") << "\n";
            ofs << "margin_fill = " << margin_fill_to_ini(ts.margin_fill) << "\n";
            ofs << "input_mode = " << std::clamp(ts.input_mode, 0, 3) << "\n";
        }

        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            fs::remove(tmp_path, ec);
            return false;
        }
    }

    fs::rename(tmp_path, target_path, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }
    return true;
}

}  // namespace czx::app
