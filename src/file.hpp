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
#include <format>
#include <functional>
#include <generator>
#include <optional>
#include <string>
#include <system_error>

#include <sys/types.h> // gid_t, uid_t

////////////////////////////////////////////////////////////////////////////////
namespace io
{

using exception = std::filesystem::filesystem_error;

using std::filesystem::path;
using std::filesystem::file_type;
using file_size = std::uintmax_t;
using mode = std::filesystem::perms;
using time = std::filesystem::file_time_type;

using dev = dev_t;
using gid = gid_t;
using ino = ino_t;
using uid = uid_t;

using hardlink_count = std::uintmax_t;

struct follow_symlinks_t { explicit follow_symlinks_t() = default; };
inline constexpr follow_symlinks_t follow_symlinks{};

struct attrib
{
    std::optional<io::mode> mode;
    std::optional<io::time> time;
    std::optional<io::gid> gid;
    std::optional<io::uid> uid;
};

////////////////////////////////////////////////////////////////////////////////
class file
{
    io::path path_;

    io::file_type type_ = file_type::none;
    io::file_size size_ = 0;
    io::mode mode_ = mode::unknown;
    io::time time_{};
    io::gid gid_ = -1;
    io::uid uid_ = -1;
    io::dev dev_type_ = 0;

    io::dev dev_ = 0;
    io::ino ino_ = 0;
    io::hardlink_count hardlink_count_ = 0;

public:
    file() noexcept = default;
    file(io::path path, std::error_code& ec) noexcept : file{std::move(path), false, ec} { }
    file(io::path path, follow_symlinks_t, std::error_code& ec) noexcept : file{std::move(path), true, ec} { }

    bool empty() const noexcept { return type_ == file_type::none; }
    explicit operator bool() const noexcept { return !empty(); }
    void clear() noexcept { *this = file{}; }

    auto& path() const noexcept { return path_; }

    auto size() const noexcept { return size_; }
    auto type() const noexcept { return type_; }
    auto mode() const noexcept { return mode_; }
    auto time() const noexcept { return time_; }
    auto  gid() const noexcept { return  gid_; }
    auto  uid() const noexcept { return  uid_; }
    auto dev_type() const noexcept { return dev_type_; }
    auto hardlink_count() const noexcept { return hardlink_count_; }

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
        return lhs.exists() && rhs.exists() && lhs.dev_ == rhs.dev_ && lhs.ino_ == rhs.ino_;
    }

private:
    file(io::path, bool follow_symlinks, std::error_code&) noexcept;
};

////////////////////////////////////////////////////////////////////////////////
bool can_read(const path&, std::error_code&) noexcept;

using progress_callback = std::function<bool(file_size copied)>;

void copy_file(const file&, const file&, const attrib&, std::error_code&, const progress_callback& = {});
inline void copy_file(const file& source, const file& target, std::error_code& ec, const progress_callback& cb = {}) {
    io::copy_file(source, target, {}, ec, cb);
}

void create_block_device(const path&, dev, const attrib&, std::error_code&) noexcept;
inline void create_block_device(const path& path, dev type, std::error_code& ec) noexcept {
    io::create_block_device(path, type, {}, ec);
}

void create_char_device(const path&, dev, const attrib&, std::error_code&) noexcept;
inline void create_char_device(const path& path, dev type, std::error_code& ec) noexcept {
    io::create_char_device(path, type, {}, ec);
}

void create_directory(const path&, const attrib&, std::error_code&) noexcept;
inline void create_directory(const path& path, std::error_code& ec) noexcept { io::create_directory(path, {}, ec); }

void create_fifo(const path&, const attrib&, std::error_code&) noexcept;
inline void create_fifo(const path& path, std::error_code& ec) noexcept { io::create_fifo(path, {}, ec); }

void create_socket(const path&, const attrib&, std::error_code&) noexcept;
inline void create_socket(const path& path, std::error_code& ec) noexcept { io::create_socket(path, {}, ec); }

void create_symlink(const path& to, const path& new_link, const attrib&, std::error_code&) noexcept;
inline void create_symlink(const path& to, const path& new_link, std::error_code& ec) noexcept {
    io::create_symlink(to, new_link, {}, ec);
}

std::generator<std::expected<path, std::error_code>> directory_iterator(const path&);

void modify(const path&, const attrib&, std::error_code&) noexcept;

void remove(const path&, std::error_code&) noexcept;
void remove_directory(const path&, std::error_code&) noexcept;

void rename(const path&, const path&, std::error_code&) noexcept;

}

////////////////////////////////////////////////////////////////////////////////
namespace std
{

template <>
struct formatter<io::exception> : formatter<string>
{
    auto format(const io::exception& e, format_context& ctx) const
    {
        string out = e.code().message();
        if (!e.path1().empty()) out += std::format(": {}", e.path1().string());
        if (!e.path2().empty()) out += std::format(" => {}", e.path2().string());

        return formatter<string>::format(out, ctx);
    }
};

}
