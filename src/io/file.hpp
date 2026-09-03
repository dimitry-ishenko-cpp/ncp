////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "desc.hpp"
#include "types.hpp"

#include <system_error> // std::error_code
#include <utility>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

struct follow_symlinks_t { explicit follow_symlinks_t() = default; };
inline constexpr follow_symlinks_t follow_symlinks{};

class file
{
    io::path path_;
    io::desc fd_;

    io::file_type type_ = file_type::none;
    io::file_size size_ = 0;
    io::mode mode_ = mode::unknown;
    io::time time_{};

    io::group_id gid_ = -1;
    io::user_id uid_ = -1;

    io::device dev_ = 0;
    io::index_node ino_ = 0;
    io::hardlink_count nlink_ = 0;

    io::device rdev_ = 0;

public:
    ////////////////////
    file() noexcept = default;
    file(io::path path, std::error_code& ec) noexcept : file{std::move(path), false, ec} { }
    file(io::path path, follow_symlinks_t, std::error_code& ec) noexcept : file{std::move(path), true, ec} { }

    bool empty() const noexcept { return type_ == file_type::none; }
    explicit operator bool() const noexcept { return !empty(); }

    const auto& path() const noexcept { return path_; }

    ////////////////////
    auto type() const noexcept { return type_; }
    auto size() const noexcept { return size_; }
    auto mode() const noexcept { return mode_; }
    auto time() const noexcept { return time_; }

    auto group_id() const noexcept { return gid_; }
    auto user_id() const noexcept { return uid_; }

    auto device() const noexcept { return dev_; }
    auto index_node() const noexcept { return ino_; }
    auto hardlink_count() const noexcept { return nlink_; }

    auto device_type() const noexcept { return rdev_; }

    ////////////////////
    bool not_found() const noexcept { return type_ == file_type::not_found; }
    bool exists() const noexcept { return !empty() && !not_found(); }

    bool is_regular_file() const noexcept { return type_ == file_type::regular;   }
    bool is_directory   () const noexcept { return type_ == file_type::directory; }
    bool is_symlink     () const noexcept { return type_ == file_type::symlink;   }
    bool is_block_device() const noexcept { return type_ == file_type::block;     }
    bool is_char_device () const noexcept { return type_ == file_type::character; }
    bool is_fifo        () const noexcept { return type_ == file_type::fifo;      }
    bool is_socket      () const noexcept { return type_ == file_type::socket;    }

    bool is_standard    () const noexcept { return is_regular_file() || is_directory() || is_symlink(); }
    bool is_device      () const noexcept { return is_block_device() || is_char_device(); }
    bool is_special     () const noexcept { return is_fifo() || is_socket(); }

    ////////////////////
    file follow_symlinks(std::error_code&) const;
    io::path get_target_path(std::error_code&) const;

    friend bool operator==(const file& lhs, const file& rhs) noexcept {
        return lhs.exists() && rhs.exists() && lhs.device() == rhs.device() && lhs.index_node() == rhs.index_node();
    }

private:
    file(io::path, bool follow, std::error_code&) noexcept;
};

}
