////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"

#include <expected>
#include <utility> // std::move

////////////////////////////////////////////////////////////////////////////////
namespace io
{

struct follow_symlinks_t { explicit follow_symlinks_t() = default; };
inline constexpr follow_symlinks_t follow_symlinks{};

////////////////////////////////////////////////////////////////////////////////
class file_info
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
    file_info() noexcept = default;
    file_info(io::path path, error_code& ec) noexcept : file_info{std::move(path), false, ec} { }
    file_info(io::path path, follow_symlinks_t, error_code& ec) noexcept : file_info{std::move(path), true, ec} { }

    static std::expected<file_info, error_code> get(io::path) noexcept;
    static std::expected<file_info, error_code> get(io::path, follow_symlinks_t) noexcept;

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
    std::expected<file_info, error_code> follow_symlinks() const
    {
        if (!is_symlink()) return *this;
        else return file_info::get(path_, io::follow_symlinks);
    }

    std::expected<io::path, error_code> target_path() const;

private:
    file_info(io::path, bool follow_symlinks, error_code&) noexcept;
};

}
