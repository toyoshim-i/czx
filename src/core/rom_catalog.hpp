#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace czx::core {

enum class RomTier {
    RequiredMissing = 0,
    RequiredOk,
    BuiltinNoRoms,
    BuiltinPlusRoms,
    OptionalEmpty,
    OptionalOk,
};

struct RomSlotSpec {
    const char* id;
    const char* canonical;
    const char* rel_dir;  // relative to roms_sub_dir (empty = directly inside roms_sub_dir)
    bool required;
    bool case_insensitive;
    const char* note;
    std::vector<const char*> aliases;
};

struct MachineDoc {
    const char* id;
    const char* name;
    const char* subtitle;
    const char* roms_sub_dir;
    const char* case_rule;
    const char* builtin_tag;  // nullptr if external ROMs are required
    const char* media_dir;
    const char* media_exts;
    const char* tip1;
    const char* tip2;
    std::vector<RomSlotSpec> slots;
};

struct RomSlotStatus {
    bool installed = false;
    std::string filename;
    uint64_t size_bytes = 0;
};

struct MachineStatus {
    RomTier tier = RomTier::BuiltinNoRoms;
    int req_found = 0;
    int req_total = 0;
    int opt_found = 0;
    int opt_total = 0;
    std::vector<RomSlotStatus> slots;
};

struct ImportReport {
    std::vector<std::string> placed;
    std::vector<std::string> warnings;
    int skipped = 0;
};

struct MediaFileInfo {
    std::string name;
    uint64_t size_bytes = 0;
};

const std::vector<MachineDoc>& get_machine_docs();
const MachineDoc* find_machine_doc(const std::string& id);

std::vector<MachineStatus> inspect_machines(const std::string& data_dir);

bool install_single_rom(const std::string& data_dir,
                        const std::string& machine_id,
                        const std::string& slot_id,
                        const std::string& orig_filename,
                        const uint8_t* data,
                        size_t size,
                        std::string& saved_rel_path,
                        std::string& err_out);

bool delete_slot_rom(const std::string& data_dir,
                     const std::string& machine_id,
                     const std::string& slot_id);

ImportReport import_rom_upload(const std::string& data_dir,
                               const std::string& scope_machine_id,
                               const std::string& filename,
                               const uint8_t* data,
                               size_t size);

std::vector<MediaFileInfo> list_media_files(const std::string& data_dir, const std::string& machine_id);

bool install_media_file(const std::string& data_dir,
                          const std::string& machine_id,
                          const std::string& filename,
                          const uint8_t* data,
                          size_t size,
                          std::string& saved_name,
                          std::string& err_out);

bool delete_media_file(const std::string& data_dir,
                       const std::string& machine_id,
                       const std::string& filename);

}  // namespace czx::core
