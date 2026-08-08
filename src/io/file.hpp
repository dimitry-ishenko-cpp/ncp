////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <generator>
#include <system_error>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

using std::filesystem::file_type;
using std::filesystem::path;
using std::filesystem::perms;
using time_type = std::filesystem::file_time_type;

using file_size = std::uintmax_t;
using hardlink_count = std::uintmax_t;

using uid = std::uint32_t;
using gid = std::uint32_t;

struct error_info
{
    std::error_code code;
    io::path path, path_to;
};

template <typename T>
using expected = std::expected<T, error_info>;
using unexpected = std::unexpected<error_info>;

unexpected make_unexpected(int val, path = {}, path = {});

////////////////////////////////////////////////////////////////////////////////
expected<void> create_directory(const path&, perms = perms::all);
expected<void> create_symlink(const path& to, const path& new_link);

std::generator<expected<path>> directory_iterator(const path&);

}
