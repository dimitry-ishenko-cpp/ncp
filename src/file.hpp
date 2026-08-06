////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "options.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <system_error>

using std::filesystem::directory_entry;
using std::filesystem::file_status;
using std::filesystem::path;

////////////////////////////////////////////////////////////////////////////////
struct error
{
    std::error_code code;
    path path1, path2;
};

std::expected<void, error> copy_file(const path&, const path&, const options&);
std::expected<void, error> create_directory(const path&, const options&);
std::expected<void, error> create_symlink(const path&, const path&, const options&);

std::expected<std::uintmax_t, error> file_size(const path&);

std::expected<bool, error> is_directory(const directory_entry&);
inline auto is_directory(file_status s) { return std::filesystem::is_directory(s); }
std::expected<bool, error> is_directory(const path&);

std::expected<bool, error> is_symlink(const directory_entry&);
inline auto is_symlink(file_status s) { return std::filesystem::is_symlink(s); }
std::expected<bool, error> is_symlink(const path&);

std::expected<path, error> read_symlink(const path&);

std::expected<file_status, error> symlink_status(const path&);
