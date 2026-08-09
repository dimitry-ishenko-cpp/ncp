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

struct follow_symlinks_t { explicit follow_symlinks_t() = default; };
inline constexpr follow_symlinks_t follow_symlinks{};

////////////////////////////////////////////////////////////////////////////////
class file
{
    io::path path_;

    io::file_type type_ = file_type::none;
    io::file_size size_ = 0;
    io::perms perms_ = perms::unknown;
    io::uid uid_ = 0;
    io::gid gid_ = 0;
    io::time_type mtime_{};
    io::hardlink_count hardlink_count_ = 0;

public:
    file() noexcept = default;
    file(io::path path, std::error_code& ec) noexcept : file{std::move(path), false, ec} { }
    file(io::path path, follow_symlinks_t, std::error_code& ec) noexcept : file{std::move(path), true, ec} { }

    auto& path() const noexcept { return  path_; }

    auto  type() const noexcept { return  type_; }
    auto  size() const noexcept { return  size_; }
    auto perms() const noexcept { return perms_; }
    auto   uid() const noexcept { return   uid_; }
    auto   gid() const noexcept { return   gid_; }
    auto mtime() const noexcept { return mtime_; }
    auto hardlink_count() const noexcept { return hardlink_count_; }

    bool exists() const noexcept { return type_ != file_type::none && type_ != file_type::not_found; }
    explicit operator bool() const noexcept { return exists(); }

    bool is_regular_file() const noexcept { return type_ == file_type::regular;   }
    bool is_directory   () const noexcept { return type_ == file_type::directory; }
    bool is_symlink     () const noexcept { return type_ == file_type::symlink;   }
    bool is_block_device() const noexcept { return type_ == file_type::block;     }
    bool is_char_device () const noexcept { return type_ == file_type::character; }
    bool is_fifo        () const noexcept { return type_ == file_type::fifo;      }
    bool is_socket      () const noexcept { return type_ == file_type::socket;    }

    bool is_standard    () const noexcept { return is_regular_file() || is_directory()   || is_symlink(); }
    bool is_special     () const noexcept { return is_block_device() || is_char_device() || is_fifo() || is_socket(); }

    ////////////////////
    file follow_symlinks(std::error_code&) const;
    io::path get_target_path(std::error_code&) const;

private:
    file(io::path, bool follow_symlinks, std::error_code&) noexcept;
};

////////////////////////////////////////////////////////////////////////////////
void copy_file(const file&, const path&, std::error_code&);

void create_directory(const path&, perms, std::error_code&);
inline void create_directory(const path& path, std::error_code& ec) { create_directory(path, perms::all, ec); }

void create_symlink(const path& to, const path& new_link, std::error_code&);

std::generator<std::expected<path, std::error_code>> directory_iterator(const path&);

}
