#pragma once

#include "czx/core.h"
#include "czx/libretro.h"

#include <memory>
#include <string>

namespace czx::core {

class LibretroCoreLoader {
public:
    LibretroCoreLoader();
    ~LibretroCoreLoader();

    LibretroCoreLoader(const LibretroCoreLoader&) = delete;
    LibretroCoreLoader& operator=(const LibretroCoreLoader&) = delete;

    bool load(const std::string& so_path);
    const czx_core_api_t* api() const noexcept { return loaded_ ? &api_ : nullptr; }
    const czx_core_desc_t* desc() const noexcept { return &desc_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& path() const noexcept { return so_path_; }
    const std::string& extensions() const noexcept { return extensions_; }

    struct Symbols;
    const Symbols& symbols() const noexcept { return *syms_; }
    bool need_fullpath() const noexcept { return need_fullpath_; }
    bool open_library();
    void close_library() noexcept;
    bool is_library_open() const noexcept { return dl_ != nullptr && syms_ != nullptr; }
    void update_av_timing(double fps, double sample_rate);
    void set_last_error(std::string msg) { last_error_ = std::move(msg); }
    const char* last_error() const noexcept { return last_error_.empty() ? nullptr : last_error_.c_str(); }

private:
    std::string so_path_;
    std::string id_;
    std::string display_name_;
    std::string extensions_;
    std::string last_error_;
    void* dl_ = nullptr;
    bool loaded_ = false;
    bool need_fullpath_ = false;
    std::unique_ptr<Symbols> syms_;
    czx_core_desc_t desc_{};
    czx_core_api_t api_{};
};

}  // namespace czx::core
