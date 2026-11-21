#include "video/file_browser.hpp"

#include "video/font6x8.hpp"
#include "video/pixel.hpp"
#include "video/scaler.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace czx::video {

namespace {

std::string to_lower(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

}  // namespace

void FileBrowser::open(const std::string& start_dir, const std::string& pipe_extensions) {
    allowed_exts_.clear();
    size_t pos = 0;
    while (pos < pipe_extensions.size()) {
        const size_t bar = pipe_extensions.find('|', pos);
        std::string ext = (bar == std::string::npos)
                              ? pipe_extensions.substr(pos)
                              : pipe_extensions.substr(pos, bar - pos);
        ext = to_lower(ext);
        if (!ext.empty()) {
            if (ext[0] != '.') ext.insert(ext.begin(), '.');
            allowed_exts_.push_back(ext);
        }
        if (bar == std::string::npos) break;
        pos = bar + 1;
    }

    std::error_code ec;
    fs::path p(start_dir);
    if (!fs::is_directory(p, ec)) {
        p = fs::current_path(ec);
    }
    current_dir_ = fs::weakly_canonical(p, ec).string();
    if (current_dir_.empty()) current_dir_ = p.string();
    selected_path_.clear();
    open_ = true;
    refresh();
}

bool FileBrowser::matches_extension(const std::string& filename) const {
    if (allowed_exts_.empty()) return true;
    const std::string ext = to_lower(fs::path(filename).extension().string());
    if (ext == ".zip" || ext == ".m3u") return true;
    return std::find(allowed_exts_.begin(), allowed_exts_.end(), ext) != allowed_exts_.end();
}

void FileBrowser::refresh() {
    entries_.clear();
    cursor_ = 0;
    scroll_ = 0;

    std::error_code ec;
    const fs::path cur(current_dir_);
    if (cur.has_parent_path() && cur != cur.parent_path()) {
        entries_.push_back(Entry{"..", cur.parent_path().string(), true});
    }

    std::vector<Entry> dirs;
    std::vector<Entry> files;
    for (const auto& it : fs::directory_iterator(cur, ec)) {
        const std::string name = it.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (it.is_directory(ec)) {
            dirs.push_back(Entry{name, it.path().string(), true});
        } else if (it.is_regular_file(ec) && matches_extension(name)) {
            files.push_back(Entry{name, it.path().string(), false});
        }
    }

    auto by_name = [](const Entry& a, const Entry& b) {
        return to_lower(a.name) < to_lower(b.name);
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    entries_.insert(entries_.end(), dirs.begin(), dirs.end());
    entries_.insert(entries_.end(), files.begin(), files.end());
}

FileBrowserAction FileBrowser::handle_key(const czx_key_event_t& ev) {
    if (!open_ || !ev.pressed) return FileBrowserAction::None;

    if (ev.key == CZX_KEY_ESC) {
        open_ = false;
        return FileBrowserAction::Cancelled;
    }

    if (ev.key == CZX_KEY_Z || ev.key == CZX_KEY_LEFT || ev.key == CZX_KEY_BACKSPACE) {
        const fs::path cur(current_dir_);
        if (cur.has_parent_path() && cur != cur.parent_path()) {
            current_dir_ = cur.parent_path().string();
            refresh();
        }
        return FileBrowserAction::None;
    }

    if (entries_.empty()) return FileBrowserAction::None;

    if (ev.key == CZX_KEY_UP || ev.key == CZX_KEY_F) {
        cursor_ = (cursor_ + static_cast<int>(entries_.size()) - 1) % static_cast<int>(entries_.size());
    } else if (ev.key == CZX_KEY_DOWN || ev.key == CZX_KEY_X) {
        cursor_ = (cursor_ + 1) % static_cast<int>(entries_.size());
    } else if (ev.key == CZX_KEY_RIGHT || ev.key == CZX_KEY_C ||
               ev.key == CZX_KEY_ENTER || ev.key == CZX_KEY_SPACE) {
        const Entry& e = entries_[static_cast<size_t>(cursor_)];
        if (e.is_dir) {
            current_dir_ = e.full_path;
            refresh();
        } else {
            selected_path_ = e.full_path;
            open_ = false;
            return FileBrowserAction::Selected;
        }
    }

    if (cursor_ < scroll_) scroll_ = cursor_;
    if (cursor_ >= scroll_ + kVisibleRows) scroll_ = cursor_ - kVisibleRows + 1;
    return FileBrowserAction::None;
}

void FileBrowser::render(uint16_t* fb_rgb565) const {
    if (!open_ || !fb_rgb565) return;

    Rgb565Canvas canvas(fb_rgb565, kLcdWidth, kLcdHeight);

    constexpr int kBoxX = 10;
    constexpr int kBoxY = 8;
    constexpr int kBoxW = 300;
    constexpr int kBoxH = 154;

    canvas.fill_rect_alpha(0, 0, kLcdWidth, kLcdHeight, pack_rgb565(0, 0, 0), 150);
    canvas.fill_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(18, 24, 34));
    canvas.draw_rect(kBoxX, kBoxY, kBoxW, kBoxH, pack_rgb565(88, 210, 255));

    // Path header
    canvas.fill_rect(kBoxX + 1, kBoxY + 1, kBoxW - 2, 16, pack_rgb565(26, 44, 68));
    std::string dir_label = current_dir_;
    constexpr size_t kMaxChars = 46;
    if (dir_label.size() > kMaxChars) {
        dir_label = "..." + dir_label.substr(dir_label.size() - (kMaxChars - 3));
    }
    canvas.draw_text(kBoxX + 8, kBoxY + 5, dir_label.c_str(), pack_rgb565(255, 255, 255));

    constexpr int kListY = kBoxY + 21;
    constexpr int kRowH = 14;

    if (entries_.size() <= 1) {
        canvas.draw_text(kBoxX + 12, kListY + 20, "No media files in this folder.",
                         pack_rgb565(200, 210, 225));
    }

    const int end = std::min<int>(static_cast<int>(entries_.size()), scroll_ + kVisibleRows);
    for (int i = scroll_; i < end; ++i) {
        const int row = i - scroll_;
        const int ry = kListY + row * kRowH;
        const bool selected = (i == cursor_);
        if (selected) {
            canvas.fill_rect(kBoxX + 6, ry, kBoxW - 12, kRowH - 1, pack_rgb565(30, 86, 124));
        }

        const Entry& e = entries_[static_cast<size_t>(i)];
        std::string label = e.is_dir ? ("[DIR] " + e.name) : e.name;
        if (label.size() > 44) {
            label = label.substr(0, 41) + "...";
        }

        const uint16_t col = selected
                                 ? pack_rgb565(255, 240, 140)
                                 : (e.is_dir ? pack_rgb565(130, 220, 255) : pack_rgb565(215, 225, 235));
        canvas.draw_char(kBoxX + 8, ry + 3, selected ? '>' : ' ', col);
        canvas.draw_text(kBoxX + 18, ry + 3, label.c_str(), col);
    }

    canvas.draw_hline(kBoxX + 6, kBoxY + kBoxH - 15, kBoxW - 12, pack_rgb565(56, 74, 98));
    canvas.draw_text(kBoxX + 10, kBoxY + kBoxH - 11,
                     "F/X:Move  Z:Up  C/OK:Open  ESC:Back",
                     pack_rgb565(150, 170, 195));
}

}  // namespace czx::video
