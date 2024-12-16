#pragma once

#include "ui/menus/menu_base.hpp"
#include "nro.hpp"
#include "fs.hpp"
#include "option.hpp"
// #include <optional>
#include <span>

namespace sphaira::ui::menu::filebrowser {

enum class SelectedType {
    None,
    Copy,
    Cut,
    Delete,
};

enum SortType {
    SortType_Size,
    SortType_Alphabetical,
};

enum OrderType {
    OrderType_Decending,
    OrderType_Ascending,
};

// roughly 1kib in size per entry
struct FileEntry : FsDirectoryEntry {
    std::string extension{}; // if any
    std::string internal_name{}; // if any
    std::string internal_extension{}; // if any
    s64 file_count{-1}; // number of files in a folder, non-recursive
    s64 dir_count{-1}; // number folders in a folder, non-recursive
    FsTimeStampRaw time_stamp{};
    bool checked_extension{}; // did we already search for an ext?
    bool checked_internal_extension{}; // did we already search for an ext?
    bool selected{}; // is this file selected?

    auto IsFile() const -> bool {
        return type == FsDirEntryType_File;
    }

    auto IsDir() const -> bool {
        return !IsFile();
    }

    auto IsHidden() const -> bool {
        return name[0] == '.';
    }

    auto GetName() const -> std::string {
        return name;
    }

    auto GetExtension() const -> std::string {
        return extension;
    }

    auto GetInternalName() const -> std::string {
        if (!internal_name.empty()) {
            return internal_name;
        }
        return GetName();
    }

    auto GetInternalExtension() const -> std::string {
        if (!internal_extension.empty()) {
            return internal_extension;
        }
        return GetExtension();
    }

    auto IsSelected() const -> bool {
        return selected;
    }
};

struct FileAssocEntry {
    fs::FsPath path{}; // ini name
    std::string name; // ini name
    std::vector<std::string> ext; // list of ext
    std::vector<std::string> database; // list of systems
};
struct LastFile {
    fs::FsPath name;
    u64 index;
    u64 offset;
    u64 entries_count;
};

struct Menu final : MenuBase {
    Menu(const std::vector<NroEntry>& nro_entries);
    ~Menu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    static auto GetNewPath(const fs::FsPath& root_path, const fs::FsPath& file_path) -> fs::FsPath {
        return fs::AppendPath(root_path, file_path);
    }

private:
    void SetIndex(std::size_t index);
    void InstallForwarder();
    auto Scan(const fs::FsPath& new_path, bool is_walk_up = false) -> Result;

    void LoadAssocEntriesPath(const fs::FsPath& path);
    void LoadAssocEntries();
    auto FindFileAssocFor() -> std::vector<FileAssocEntry>;
    void OnIndexChange();

    auto GetNewPath(const FileEntry& entry) const -> fs::FsPath {
        return GetNewPath(m_path, entry.name);
    };

    auto GetNewPath(u64 index) const -> fs::FsPath {
        return GetNewPath(m_path, GetEntry(index).name);
    };

    auto GetNewPathCurrent() const -> fs::FsPath {
        return GetNewPath(m_index);
    };

    auto GetSelectedEntries() const -> std::vector<FileEntry> {
        if (!m_selected_count) {
            return {};
        }

        std::vector<FileEntry> out;

        for (auto&e : m_entries) {
            if (e.IsSelected()) {
                out.emplace_back(e);
            }
        }

        return out;
    }

    void AddSelectedEntries(SelectedType type) {
        auto entries = GetSelectedEntries();
        if (entries.empty()) {
            // log_write("%s with no selected files\n", __PRETTY_FUNCTION__);
            return;
        }

        m_selected_type = type;
        m_selected_files = entries;
        m_selected_path = m_path;
    }

    void AddCurrentFileToSelection(SelectedType type) {
        m_selected_files.emplace_back(GetEntry());
        m_selected_count++;
        m_selected_type = type;
        m_selected_path = m_path;
    }

    void ResetSelection() {
        m_selected_files.clear();
        m_selected_count = 0;
        m_selected_type = SelectedType::None;
        m_selected_path = {};
    }

    auto HasTypeInSelectedEntries(FsDirEntryType type) const -> bool {
        if (!m_selected_count) {
            return GetEntry().type == type;
        } else {
            for (auto&p : m_selected_files) {
                if (p.type == type) {
                    return true;
                }
            }

            return false;
        }
    }

    auto GetEntry(u32 index) -> FileEntry& {
        return m_entries[m_entries_current[index]];
    }

    auto GetEntry(u32 index) const -> const FileEntry& {
        return m_entries[m_entries_current[index]];
    }

    auto GetEntry() -> FileEntry& {
        return GetEntry(m_index);
    }

    auto GetEntry() const -> const FileEntry& {
        return GetEntry(m_index);
    }

    void Sort();
    void SortAndFindLastFile();
    void SetIndexFromLastFile(const LastFile& last_file);
    void UpdateSubheading();

    void OnDeleteCallback();
    void OnPasteCallback();
    void OnRenameCallback();

private:
    static constexpr inline const char* INI_SECTION = "filebrowser";

    const std::vector<NroEntry>& m_nro_entries;
    fs::FsPath m_path;
    std::vector<FileEntry> m_entries;
    std::vector<u32> m_entries_index; // files not including hidden
    std::vector<u32> m_entries_index_hidden; // includes hidden files
    std::vector<u32> m_entries_index_search; // files found via search
    std::span<u32> m_entries_current;

    // search options
    // show files [X]
    // show folders [X]
    // recursive (slow) [ ]

    std::vector<FileAssocEntry> m_assoc_entries;
    std::vector<FileEntry> m_selected_files;

    // this keeps track of the highlighted file before opening a folder
    // if the user presses B to go back to the previous dir
    // this vector is popped, then, that entry is checked if it still exists
    // if it does, the index becomes that file.
    std::vector<LastFile> m_previous_highlighted_file;
    fs::FsPath m_selected_path;
    std::size_t m_index{};
    std::size_t m_index_offset{};
    std::size_t m_selected_count{};
    SelectedType m_selected_type{SelectedType::None};

    option::OptionLong m_sort{INI_SECTION, "sort", SortType::SortType_Alphabetical};
    option::OptionLong m_order{INI_SECTION, "order", OrderType::OrderType_Decending};
    option::OptionBool m_show_hidden{INI_SECTION, "show_hidden", false};
    option::OptionBool m_folders_first{INI_SECTION, "folders_first", true};
    option::OptionBool m_hidden_last{INI_SECTION, "hidden_last", false};

    option::OptionBool m_search_show_files{INI_SECTION, "search_show_files", true};
    option::OptionBool m_search_show_folders{INI_SECTION, "search_show_folders", true};
    option::OptionBool m_search_recursive{INI_SECTION, "search_recursive", false};

    bool m_loaded_assoc_entries{};
    bool m_is_update_folder{};
};

} // namespace sphaira::ui::menu::filebrowser