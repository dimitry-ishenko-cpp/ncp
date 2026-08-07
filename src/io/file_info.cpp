////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file_info.hpp"

#include <cerrno>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace { auto make_error_code(int eval) { return error_code{eval, std::generic_category()}; } }

////////////////////////////////////////////////////////////////////////////////
file_info::file_info(io::path path, bool follow_symlinks, error_code& ec) noexcept :
    path_{std::move(path)}
{
    ec.clear();

    struct stat st{};
    auto pfn = follow_symlinks ? ::stat : ::lstat;

    if (pfn(path_.c_str(), &st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
            type_ = file_type::not_found;
        else ec = make_error_code(errno);
    }
    else
    {
        if (S_ISREG(st.st_mode)) type_ = file_type::regular;
        else if (S_ISDIR (st.st_mode)) type_ = file_type::directory;
        else if (S_ISLNK (st.st_mode)) type_ = file_type::symlink;
        else if (S_ISBLK (st.st_mode)) type_ = file_type::block;
        else if (S_ISCHR (st.st_mode)) type_ = file_type::character;
        else if (S_ISFIFO(st.st_mode)) type_ = file_type::fifo;
        else if (S_ISSOCK(st.st_mode)) type_ = file_type::socket;
        else type_ = file_type::unknown;

        size_  = static_cast<io::file_size>(st.st_size);
        perms_ = static_cast<io::perms>(st.st_mode & 07777);
        uid_   = static_cast<io::uid>(st.st_uid);
        gid_   = static_cast<io::gid>(st.st_gid);
        hardlink_count_ = static_cast<io::hardlink_count>(st.st_nlink);
    }
}

std::expected<file_info, error_code> file_info::get(io::path path) noexcept
{
    error_code ec;
    file_info info{std::move(path), ec};

    if (ec) return std::unexpected(ec);
    else return info;
}

std::expected<file_info, error_code> file_info::get(io::path path, follow_symlinks_t) noexcept
{
    error_code ec;
    file_info info{std::move(path), io::follow_symlinks, ec};

    if (ec) return std::unexpected(ec);
    else return info;
}

std::expected<io::path, error_code> file_info::target_path() const
{
    if (!is_symlink()) return std::unexpected(make_error_code(EINVAL));

    std::string buf(size_ ? size_ + 1 : 128, '\0');

    do
    {
        auto len = ::readlink(path_.string().c_str(), buf.data(), buf.size());
        if (len < 0) return std::unexpected(make_error_code(errno));

        if (len < buf.size())
        {
            buf.resize(len);
            return buf;
        }
        else buf.resize(buf.size() * 2, '\0');
    }
    while (true);
}

}
