#pragma once

#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include "fs.hpp"

namespace sphaira {

struct Hbini {
    u64 timestamp{}; // timestamp of last launch
    u32 launch_count{}; //
};

struct NroEntry {
    fs::FsPath path;
    s64 size;
    NacpStruct nacp;

    std::vector<u8> icon;
    u64 icon_size;
    u64 icon_offset;

    FsTimeStampRaw timestamp;
    Hbini hbini;

    int image; // nvg image
    int x,y,w,h; // image
    bool is_nacp_valid;

    auto GetName() const -> const char* {
        return nacp.lang[0].name;
    }

    auto GetAuthor() const -> const char* {
        return nacp.lang[0].author;
    }

    auto GetDisplayVersion() const -> const char* {
        return nacp.display_version;
    }
};

auto nro_verify(std::span<const u8> data) -> Result;
auto nro_parse(const fs::FsPath& path, NroEntry& entry) -> Result;

/**
 * Scans a folder for all nro's.
 *
 * \param path path to the folder that is to be scanned.
 * \param nros output of all scanned nros.
 * \param nested if true, it will scan any folders for nros, ie path/folder/game.nro.
 * \param scan_all_dir if true, when a folder is found, it will scan the entire
 *                     folder for all nros, rather than stopping at the first
 *                     nro found.
 *                     this does nothing if nested=false.
 */
auto nro_scan(const fs::FsPath& path, std::vector<NroEntry>& nros, bool hide_spahira, bool nested = true, bool scan_all_dir = true) -> Result;

auto nro_get_icon(const fs::FsPath& path, u64 size, u64 offset) -> std::vector<u8>;
auto nro_get_icon(const fs::FsPath& path) -> std::vector<u8>;
auto nro_get_nacp(const fs::FsPath& path, NacpStruct& nacp) -> Result;

// path is pre-appended to args, such that argv[0] == path
auto nro_launch(std::string path, std::string args = {}) -> Result;

// if the arg contains a space, it will wrap it in quotes
auto nro_add_arg(std::string arg) -> std::string;

// same as above but prefixes "sdmc" to keep compat with hbmenu
auto nro_add_arg_file(std::string arg) -> std::string;

// strips sdmc:
auto nro_normalise_path(const std::string& p) -> std::string;

} // namespace sphaira