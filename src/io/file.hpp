////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "desc.hpp"
#include "types.hpp"

#include <expected>
#include <generator>
#include <system_error> // std::error_code

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

    static constexpr auto none = -1;
    io::user_id uid_ = none;
    io::group_id gid_ = none;

    io::device dev_ = 0;
    io::index_node ino_ = 0;
    io::hardlink_count nlink_ = 0;

    io::device rdev_ = 0;

    ////////////////////
    file(const file& parent, const io::path&, bool follow, std::error_code&) noexcept;

public:
    ////////////////////
    file() noexcept = default;

    file(const file& parent, const io::path& name, std::error_code& ec) noexcept :
        file{parent, name, false, ec}
    { }
    file(const file& parent, const io::path& name, follow_symlinks_t, std::error_code& ec) noexcept :
        file{parent, name, true, ec}
    { }

    file(const io::path& path, std::error_code& ec) noexcept : file{{}, path, false, ec} { }
    file(const io::path& path, follow_symlinks_t, std::error_code& ec) noexcept : file{{}, path, true, ec} { }

    const auto& path() const noexcept { return path_; }
    const auto& fd() const noexcept { return fd_; }

    ////////////////////
    auto type() const noexcept { return type_; }
    auto size() const noexcept { return size_; }
    auto mode() const noexcept { return mode_; }
    auto time() const noexcept { return time_; }

    auto user_id() const noexcept { return uid_; }
    auto group_id() const noexcept { return gid_; }

    auto device() const noexcept { return dev_; }
    auto index_node() const noexcept { return ino_; }
    auto hardlink_count() const noexcept { return nlink_; }

    auto device_type() const noexcept { return rdev_; }

    ////////////////////
    bool empty() const noexcept { return type_ == file_type::none; }
    explicit operator bool() const noexcept { return !!fd_; }

    bool not_found      () const noexcept { return type_ == file_type::not_found; }
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
    io::path get_target_path(std::error_code&) const;

    friend bool operator==(const file& lhs, const file& rhs) noexcept {
        return lhs && rhs && lhs.device() == rhs.device() && lhs.index_node() == rhs.index_node();
    }

    ////////////////////
    void mode(io::mode, std::error_code&) noexcept;
    void time(io::time, std::error_code&) noexcept;

    void user_id(io::user_id uid, std::error_code& ec) noexcept { owner(uid, none, ec); }
    void group_id(io::group_id gid, std::error_code& ec) noexcept { owner(none, gid, ec); }
    void owner(io::user_id, io::group_id, std::error_code&) noexcept;
};

////////////////////////////////////////////////////////////////////////////////
void create_directory(const file& parent, const path& name, std::error_code&) noexcept;
inline void create_directory(const path& path, std::error_code& ec) noexcept { io::create_directory({}, path, ec); }

void create_symlink(const file& parent, const path& name, const path& link_target, std::error_code&) noexcept;
inline void create_symlink(const io::path& path, const io::path& link_target, std::error_code& ec) noexcept {
    io::create_symlink({}, path, link_target, ec);
}

void create_block_device(const file& parent, const path& name, device, std::error_code&) noexcept;
inline void create_block_device(const path& path, device rdev, std::error_code& ec) noexcept {
    io::create_block_device({}, path, rdev, ec);
}

void create_char_device(const file& parent, const path& name, device, std::error_code&) noexcept;
inline void create_char_device(const path& path, device rdev, std::error_code& ec) noexcept {
    io::create_char_device({}, path, rdev, ec);
}

void create_fifo(const file& parent, const path& name, std::error_code&) noexcept;
inline void create_fifo(const path& path, std::error_code& ec) noexcept { io::create_fifo({}, path, ec); }

void create_socket(const file& parent, const path& name, std::error_code&) noexcept;
inline void create_socket(const path& path, std::error_code& ec) noexcept { io::create_socket({}, path, ec); }

std::generator<std::expected<path, std::error_code>> directory_iterator(const file&);

void remove(const file& parent, const path& name, std::error_code&) noexcept;
inline void remove(const path& path, std::error_code& ec) noexcept { remove({}, path, ec); }

void remove_directory(const file& parent, const path& name, std::error_code&) noexcept;
inline void remove_directory(const path& path, std::error_code& ec) noexcept { remove_directory({}, path, ec); }

}
