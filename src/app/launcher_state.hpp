#pragma once

#include "core/core_registry.hpp"
#include "video/scaler.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace czx::app {

struct TargetVariant {
    std::string id;        // "fast", "accurate", "fm7", "fm77av", "fm77av40"
    std::string label;     // "Fast", "CycleAccurate", "FM-7", "FM77AV", "FM77AV40"
    std::string subtitle;  // "VICE x64", "eFM77AV40"
    std::string menu_name; // "C64 (VICE x64)", "FM77AV40 (eFM77AV40)"
    int core_index = -1;
};

struct MachineTarget {
    std::string id;
    std::string title;
    std::string icon_key;
    std::string variant_option_label;  // e.g. "Accuracy" (c64), "Model" (fm7)
    std::vector<TargetVariant> variants;
    int default_variant_index = 0;

    bool has_variants() const noexcept {
        return variants.size() > 1u;
    }

    int variant_count() const noexcept {
        return static_cast<int>(variants.size());
    }

    int clamp_variant_index(int variant_idx) const noexcept;
    int find_variant_index(std::string_view variant_id) const noexcept;
    int resolve_core_index(int variant_idx) const noexcept;
    const std::string& variant_id(int variant_idx) const noexcept;
    const std::string& variant_label(int variant_idx) const noexcept;
    const std::string& subtitle(int variant_idx) const noexcept;
    std::string menu_core_name(int variant_idx) const;
};

struct TargetSavedState {
    std::string media1;
    std::string media2;
    std::string media1_label;
    std::string media2_label;
    std::string d88_source1;
    int d88_index1 = -1;
    std::string d88_source2;
    int d88_index2 = -1;
    std::string last_media_dir;
    uint32_t preset_index = 0;
    std::string variant_id;
    bool has_display_prefs = false;
    czx::video::DisplayMode display_mode = czx::video::DisplayMode::OverflowFit;
    bool desktop_native = false;
    int desktop_scale = 2;
    czx::video::AspectPolicy aspect_policy = czx::video::AspectPolicy::ParAware;
    bool show_borders = false;
    czx::video::MarginFill margin_fill = czx::video::MarginFill::Auto;
    int input_mode = 0;
};

struct LauncherState {
    std::string last_target_id;
    std::map<std::string, TargetSavedState> targets;
};

std::vector<MachineTarget> build_machine_targets(const czx::core::CoreRegistry& registry);

int find_target_index_by_id(const std::vector<MachineTarget>& targets, std::string_view id) noexcept;

int find_target_index_for_core_index(const std::vector<MachineTarget>& targets,
                                     int core_index,
                                     int* out_variant_index = nullptr) noexcept;

LauncherState load_launcher_state(const std::string& path);

bool save_launcher_state(const std::string& path, const LauncherState& state);

}  // namespace czx::app
