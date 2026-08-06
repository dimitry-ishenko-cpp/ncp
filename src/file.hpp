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
