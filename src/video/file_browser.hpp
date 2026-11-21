#pragma once

#include "czx/core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace czx::video {

enum class FileBrowserAction {
    None = 0,
    Cancelled,
    Selected,
};

class FileBrowser {
public:
    bool is_open() const noexcept { return open_; }
    void open(const std::string& start_dir, const std::string& pipe_extensions);
    void close() noexcept { open_ = false; }

    FileBrowserAction handle_key(const czx_key_event_t& ev);
    void render(uint16_t* fb_rgb565) const;

    const std::string& selected_path() const noexcept { return selected_path_; }
    const std::string& current_dir() const noexcept { return current_dir_; }

private:
    struct Entry {
        std::string name;
        std::string full_path;
        bool is_dir = false;
    };

    static constexpr int kVisibleRows = 8;

    bool open_ = false;
    std::string current_dir_;
    std::string selected_path_;
    std::vector<std::string> allowed_exts_;
    std::vector<Entry> entries_;
    int cursor_ = 0;
    int scroll_ = 0;

    void refresh();
    bool matches_extension(const std::string& filename) const;
};

}  // namespace czx::video
