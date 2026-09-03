////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <filesystem>

#include <sys/types.h> // dev_t, gid_t, ino_t, uid_t

////////////////////////////////////////////////////////////////////////////////
namespace io
{

using exception = std::filesystem::filesystem_error;

using path = std::filesystem::path;

using file_type = std::filesystem::file_type;
using file_size = std::uintmax_t;
using mode = std::filesystem::perms;
using time = std::filesystem::file_time_type;

using group_id  = gid_t;
using user_id = uid_t;

using device = dev_t;
using index_node = ino_t;
using hardlink_count = std::uintmax_t;

}
