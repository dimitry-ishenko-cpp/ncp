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

using std::error_code;

////////////////////////////////////////////////////////////////////////////////
std::expected<void, error_code> create_symlink(const path& to, const path& new_link);

}
