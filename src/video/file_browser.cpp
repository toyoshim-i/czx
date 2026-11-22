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

void FileBrowser::open(const std::string& start_dir,
                       const std::string& pipe_extensions,
                       const std::string& title,
                       const std::string& d88_cache_dir,
                       bool allow_auto_dual,
                       const D88PickerHint& d88_hint) {
    title_ = title;
    d88_cache_dir_ = d88_cache_dir;
    allow_auto_dual_ = allow_auto_dual;
    in_d88_picker_ = false;
    d88_direct_mode_ = false;
    d88_drive1_index_ = -1;
    d88_drive2_index_ = -1;
    d88_disks_.clear();
    d88_source_path_.clear();
    d88_source_name_.clear();

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
    if (!d88_hint.source_path.empty()) {
        const fs::path hint_parent = fs::path(d88_hint.source_path).parent_path();
        if (!hint_parent.empty() && fs::is_directory(hint_parent, ec)) {
            p = hint_parent;
        }
    }
    if (!fs::is_directory(p, ec)) {
        p = fs::current_path(ec);
    }
    current_dir_ = fs::weakly_canonical(p, ec).string();
    if (current_dir_.empty()) current_dir_ = p.string();
    selected_path_.clear();
    selected_label_.clear();
    selected_drive2_path_.clear();
    selected_drive2_label_.clear();
    selected_d88_source_path_.clear();
    selected_d88_disk_index_ = -1;
    selected_drive2_d88_disk_index_ = -1;
    open_ = true;
    refresh();

    if (!d88_hint.source_path.empty() && !d88_cache_dir_.empty() &&
        czx::core::is_d88_ext(d88_hint.source_path) &&
        czx::core::inspect_d88(d88_hint.source_path, d88_disks_) &&
        d88_disks_.size() >= 2u) {
        std::string hint_canon = fs::weakly_canonical(fs::path(d88_hint.source_path), ec).string();
        if (hint_canon.empty()) hint_canon = d88_hint.source_path;
        const std::string hint_fname = fs::path(d88_hint.source_path).filename().string();
        int match_idx = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].is_dir) continue;
            std::string entry_canon = fs::weakly_canonical(fs::path(entries_[i].full_path), ec).string();
            if (entry_canon.empty()) entry_canon = entries_[i].full_path;
            if (entry_canon == hint_canon || entries_[i].full_path == d88_hint.source_path) {
                match_idx = static_cast<int>(i);
                break;
            }
            if (entries_[i].name == hint_fname) {
                match_idx = static_cast<int>(i);
            }
        }
        saved_cursor_ = match_idx;
        saved_scroll_ = 0;
        if (saved_cursor_ >= kVisibleRows) {
            saved_scroll_ = saved_cursor_ - kVisibleRows + 1;
        }
        in_d88_picker_ = true;
        d88_direct_mode_ = true;
        d88_source_path_ = d88_hint.source_path;
        d88_source_name_ = hint_fname;
        d88_drive1_index_ = d88_hint.drive1_disk_index;
        d88_drive2_index_ = d88_hint.drive2_disk_index;
        cursor_ = (d88_hint.focus_disk_index >= 0 &&
                   static_cast<size_t>(d88_hint.focus_disk_index) < d88_disks_.size())
                      ? d88_hint.focus_disk_index
                      : 0;
        scroll_ = 0;
        if (cursor_ >= kVisibleRows) {
            scroll_ = cursor_ - kVisibleRows + 1;
        }
    }
}

bool FileBrowser::matches_extension(const std::string& filename) const {
    if (allowed_exts_.empty()) return true;
    const std::string ext = to_lower(fs::path(filename).extension().string());
    if (ext == ".zip" || ext == ".m3u" || ext == ".cmd") return true;
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

FileBrowserAction FileBrowser::handle_d88_key(const czx_key_event_t& ev) {
    if (ev.key == CZX_KEY_ESC) {
        in_d88_picker_ = false;
        d88_direct_mode_ = false;
        open_ = false;
        return FileBrowserAction::Cancelled;
    }

    if (ev.key == CZX_KEY_Z || ev.key == CZX_KEY_LEFT || ev.key == CZX_KEY_BACKSPACE) {
        in_d88_picker_ = false;
        d88_direct_mode_ = false;
        cursor_ = saved_cursor_;
        scroll_ = saved_scroll_;
        return FileBrowserAction::None;
    }

    const bool has_auto_row = !d88_direct_mode_ && allow_auto_dual_ && d88_disks_.size() >= 2u;
    const bool has_browse_row = d88_direct_mode_;
    const int total = static_cast<int>(d88_disks_.size()) + (has_auto_row ? 1 : 0) + (has_browse_row ? 1 : 0);
    if (total <= 0) return FileBrowserAction::None;

    if (ev.key == CZX_KEY_UP || ev.key == CZX_KEY_F) {
        cursor_ = (cursor_ + total - 1) % total;
    } else if (ev.key == CZX_KEY_DOWN || ev.key == CZX_KEY_X) {
        cursor_ = (cursor_ + 1) % total;
    } else if (ev.key == CZX_KEY_RIGHT || ev.key == CZX_KEY_C ||
               ev.key == CZX_KEY_ENTER || ev.key == CZX_KEY_SPACE) {
        if (has_browse_row && cursor_ == total - 1) {
            in_d88_picker_ = false;
            d88_direct_mode_ = false;
            cursor_ = saved_cursor_;
            scroll_ = saved_scroll_;
            return FileBrowserAction::None;
        }
        selected_drive2_path_.clear();
        selected_drive2_label_.clear();
        selected_d88_source_path_.clear();
        selected_d88_disk_index_ = -1;
        selected_drive2_d88_disk_index_ = -1;
        if (has_auto_row && cursor_ == 0 && d88_disks_.size() >= 2u) {
            const std::string p0 = czx::core::extract_d88_subdisk(
                d88_source_path_, d88_disks_[0], d88_cache_dir_);
            const std::string p1 = czx::core::extract_d88_subdisk(
                d88_source_path_, d88_disks_[1], d88_cache_dir_);
            if (!p0.empty() && !p1.empty()) {
                selected_path_ = p0;
                selected_label_ = d88_disks_[0].name_utf8;
                selected_drive2_path_ = p1;
                selected_drive2_label_ = d88_disks_[1].name_utf8;
                selected_d88_source_path_ = d88_source_path_;
                selected_d88_disk_index_ = 0;
                selected_drive2_d88_disk_index_ = 1;
                in_d88_picker_ = false;
                d88_direct_mode_ = false;
                open_ = false;
                return FileBrowserAction::Selected;
            }
        } else {
            const size_t idx = has_auto_row
                                   ? static_cast<size_t>(cursor_ - 1)
                                   : static_cast<size_t>(cursor_);
            if (idx < d88_disks_.size()) {
                const std::string p = czx::core::extract_d88_subdisk(
                    d88_source_path_, d88_disks_[idx], d88_cache_dir_);
                if (!p.empty()) {
                    selected_path_ = p;
                    selected_label_ = d88_disks_[idx].name_utf8;
                    selected_d88_source_path_ = d88_source_path_;
                    selected_d88_disk_index_ = static_cast<int>(idx);
                    in_d88_picker_ = false;
                    d88_direct_mode_ = false;
                    open_ = false;
                    return FileBrowserAction::Selected;
                }
            }
        }
        return FileBrowserAction::None;
    }

    if (cursor_ < scroll_) scroll_ = cursor_;
    if (cursor_ >= scroll_ + kVisibleRows) scroll_ = cursor_ - kVisibleRows + 1;
    return FileBrowserAction::None;
}

FileBrowserAction FileBrowser::handle_key(const czx_key_event_t& ev) {
    if (!open_ || !ev.pressed) return FileBrowserAction::None;

    if (in_d88_picker_) {
        return handle_d88_key(ev);
    }

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
            if (czx::core::is_d88_ext(e.full_path) && !d88_cache_dir_.empty()) {
                d88_disks_.clear();
                if (czx::core::inspect_d88(e.full_path, d88_disks_) && d88_disks_.size() >= 2u) {
                    in_d88_picker_ = true;
                    d88_direct_mode_ = false;
                    d88_drive1_index_ = -1;
                    d88_drive2_index_ = -1;
                    d88_source_path_ = e.full_path;
                    d88_source_name_ = e.name;
                    saved_cursor_ = cursor_;
                    saved_scroll_ = scroll_;
                    cursor_ = 0;
                    scroll_ = 0;
                    return FileBrowserAction::None;
                }
            }
            selected_path_ = e.full_path;
            selected_label_ = e.name;
            selected_drive2_path_.clear();
            selected_drive2_label_.clear();
            selected_d88_source_path_.clear();
            selected_d88_disk_index_ = -1;
            selected_drive2_d88_disk_index_ = -1;
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

    // Header
    canvas.fill_rect(kBoxX + 1, kBoxY + 1, kBoxW - 2, 16, pack_rgb565(26, 44, 68));
    if (in_d88_picker_) {
        const std::string hdr_base =
            (d88_direct_mode_ && !title_.empty()) ? (title_ + ": " + d88_source_name_)
                                                  : ("D88: " + d88_source_name_);
        const std::string hdr = truncate_head_px(hdr_base, kBoxW - 16, 1);
        canvas.draw_text(kBoxX + 8, kBoxY + 5, hdr.c_str(), pack_rgb565(255, 255, 255));
    } else {
        const std::string prefix = title_.empty() ? std::string() : (title_ + ": ");
        const int prefix_w = Rgb565Canvas::text_width(prefix.c_str(), 1);
        const std::string dir_tail = truncate_tail_px(current_dir_, kBoxW - 16 - prefix_w, 1);
        const std::string hdr = prefix + dir_tail;
        canvas.draw_text(kBoxX + 8, kBoxY + 5, hdr.c_str(), pack_rgb565(255, 255, 255));
    }

    constexpr int kListY = kBoxY + 21;
    constexpr int kRowH = 14;

    if (in_d88_picker_) {
        const bool has_auto_row = !d88_direct_mode_ && allow_auto_dual_ && d88_disks_.size() >= 2u;
        const bool has_browse_row = d88_direct_mode_;
        const int total = static_cast<int>(d88_disks_.size()) + (has_auto_row ? 1 : 0) + (has_browse_row ? 1 : 0);
        const int end = std::min(total, scroll_ + kVisibleRows);
        for (int i = scroll_; i < end; ++i) {
            const int row = i - scroll_;
            const int ry = kListY + row * kRowH;
            const bool selected = (i == cursor_);
            if (selected) {
                canvas.fill_rect(kBoxX + 6, ry, kBoxW - 12, kRowH - 1, pack_rgb565(30, 86, 124));
            }
            std::string raw_label;
            bool is_auto = false;
            bool is_browse = false;
            if (has_auto_row && i == 0) {
                raw_label = "[AUTO] D1:#1 + D2:#2";
                is_auto = true;
            } else if (has_browse_row && i == total - 1) {
                raw_label = "[Browse other files...]";
                is_browse = true;
            } else {
                const size_t idx = has_auto_row ? static_cast<size_t>(i - 1) : static_cast<size_t>(i);
                const auto& d = d88_disks_[idx];
                const bool is_d1 = (static_cast<int>(idx) == d88_drive1_index_);
                const bool is_d2 = (static_cast<int>(idx) == d88_drive2_index_);
                raw_label = "[";
                raw_label += std::to_string(idx + 1u);
                if (is_d1 && is_d2) {
                    raw_label += ":D1+2";
                } else if (is_d1) {
                    raw_label += ":D1";
                } else if (is_d2) {
                    raw_label += ":D2";
                }
                raw_label += "] ";
                raw_label += d.name_utf8;
                raw_label += " (";
                raw_label += d.media_type_label();
                raw_label += ')';
            }
            const std::string label = truncate_head_px(raw_label, kBoxW - 28, 1);
            const uint16_t col = selected
                                     ? pack_rgb565(255, 240, 140)
                                     : ((is_auto || is_browse) ? pack_rgb565(130, 220, 255)
                                                               : pack_rgb565(215, 225, 235));
            canvas.draw_char(kBoxX + 8, ry + 3, selected ? '>' : ' ', col);
            canvas.draw_text(kBoxX + 18, ry + 3, label.c_str(), col);
        }

        canvas.draw_hline(kBoxX + 6, kBoxY + kBoxH - 15, kBoxW - 12, pack_rgb565(56, 74, 98));
        canvas.draw_text(kBoxX + 10, kBoxY + kBoxH - 11,
                         d88_direct_mode_ ? "F/X:Move  Z:Files  C/OK:Insert  ESC:Back"
                                          : "F/X:Move  Z:List  C/OK:Boot  ESC:Back",
                         pack_rgb565(150, 170, 195));
        return;
    }

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
        const std::string raw_label = e.is_dir ? ("[DIR] " + e.name) : e.name;
        const std::string label = truncate_head_px(raw_label, kBoxW - 28, 1);

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
