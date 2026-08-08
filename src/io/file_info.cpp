////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file_info.hpp"

#include <cerrno>
#include <chrono>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

////////////////////////////////////////////////////////////////////////////////
file_info::file_info(io::path path, bool follow_symlinks, std::error_code& ec) noexcept :
    path_{std::move(path)}
{
    ec.clear();

    struct stat st{};
    auto pfn = follow_symlinks ? ::stat : ::lstat;

    if (pfn(path_.c_str(), &st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
            type_ = file_type::not_found;
        else ec = std::error_code{errno, std::generic_category()};
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

        using namespace std::chrono;
        auto mtime = duration_cast<time_type::duration>(
            seconds{st.st_mtim.tv_sec} + nanoseconds{st.st_mtim.tv_nsec}
        );
        mtime_ = time_type{mtime};
    }
}

expected<file_info> file_info::get(io::path path) noexcept
{
    std::error_code ec;
    file_info info{std::move(path), ec};

    if (ec) return make_unexpected(ec.value(), std::move(info.path_));
    else return info;
}

expected<file_info> file_info::get(io::path path, follow_symlinks_t) noexcept
{
    std::error_code ec;
    file_info info{std::move(path), io::follow_symlinks, ec};

    if (ec) return make_unexpected(ec.value(), std::move(info.path_));
    else return info;
}

expected<io::path> file_info::get_target_path() const
{
    if (!is_symlink()) return make_unexpected(EINVAL, path_);

    for (std::string buf(size_ ? size_ + 1 : 128, '\0'); ;)
    {
        auto len = ::readlink(path_.c_str(), buf.data(), buf.size());
        if (len < 0)
            return make_unexpected(errno, path_);
        else if (len < buf.size())
        {
            buf.resize(len);
            return buf;
        }
        else if (buf.size() == 4096)
            return make_unexpected(ENAMETOOLONG, path_);
        else buf.resize(buf.size() * 2, '\0');
    }
}

}
