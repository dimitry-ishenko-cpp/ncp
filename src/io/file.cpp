////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <cerrno>
#include <cstdint>
#include <format>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

inline auto error_code(int val) noexcept {
    return std::error_code{val, std::generic_category()};
}

inline auto proxy_path(const desc& fd) noexcept {
    return std::format("/proc/self/fd/{}", fd.get());
}

}

////////////////////////////////////////////////////////////////////////////////
file::file(io::path path, bool follow, std::error_code& ec) noexcept :
    path_{std::move(path)}
{
    fd_ = desc{ ::open(path_.c_str(), O_PATH | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW)) };
    if (!fd_)
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            type_ = file_type::not_found;
            ec.clear();
        }
        else ec = error_code(errno);
        return;
    }

    struct stat stat{};
    if (::fstat(fd_.get(), &stat))
    {
        if (errno == ENOENT)
        {
            type_ = file_type::not_found;
            fd_ = desc{};
            ec.clear();
        }
        else ec = error_code(errno);
        return;
    }

    if (S_ISREG(stat.st_mode)) type_ = file_type::regular;
    else if (S_ISDIR (stat.st_mode)) type_ = file_type::directory;
    else if (S_ISLNK (stat.st_mode)) type_ = file_type::symlink;
    else if (S_ISBLK (stat.st_mode)) type_ = file_type::block;
    else if (S_ISCHR (stat.st_mode)) type_ = file_type::character;
    else if (S_ISFIFO(stat.st_mode)) type_ = file_type::fifo;
    else if (S_ISSOCK(stat.st_mode)) type_ = file_type::socket;
    else type_ = file_type::unknown;

    if (type_ == file_type::block)
    {
        if (desc fd{ ::open(proxy_path(fd_).c_str(), O_RDONLY | O_CLOEXEC) })
        {
            std::uint64_t bytes = 0;
            if (0 == ::ioctl(fd.get(), BLKGETSIZE64, &bytes)) size_ = bytes;
        }
    }
    else size_ = stat.st_size;

    mode_ = static_cast<io::mode>(stat.st_mode & 07777);
    gid_  = stat.st_gid;
    uid_  = stat.st_uid;
    rdev_ = stat.st_rdev;
    dev_  = stat.st_dev;
    ino_  = stat.st_ino;
    nlink_= stat.st_nlink;

    using namespace std::chrono;
    auto tp = sys_time<nanoseconds>(
        seconds{stat.st_mtim.tv_sec} + nanoseconds{stat.st_mtim.tv_nsec}
    );
    time_ = time::clock::from_sys(tp);

    ec.clear();
}

path file::get_target_path(std::error_code& ec) const
{
    if (!is_symlink()) { ec = error_code(EINVAL); return {}; }

    std::string tp(size_ ? size_ + 1 : 128, '\0');
    for (;;)
    {
        auto len = ::readlinkat(fd_.get(), "", tp.data(), tp.size());
        if (len < 0)
        {
            ec = error_code(errno);
            return {};
        }
        else if (len < tp.size())
        {
            ec.clear();
            tp.resize(len);
            return tp;
        }
        else if (tp.size() >= 4096)
        {
            ec = error_code(ENAMETOOLONG);
            return {};
        }
        else tp.resize(tp.size() * 2, '\0');
    }
}

file file::follow_symlinks(std::error_code& ec) const
{
    file target;
    if (!is_symlink())
    {
        target = *this;
        ec.clear();
    }
    else
    {
        target = file{proxy_path(fd_), io::follow_symlinks, ec};
        target.path_ = path_;
    }
    return target;
}

}
