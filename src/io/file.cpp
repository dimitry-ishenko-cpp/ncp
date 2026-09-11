////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

inline auto error_code(int val) noexcept {
    return std::error_code{val, std::generic_category()};
}

inline auto fd_or_cwd(const file& parent) { return parent.empty() ? AT_FDCWD : parent.fd().get(); }

inline auto proxy_path(const desc& fd) noexcept {
    return std::format("/proc/self/fd/{}", fd.get());
}

}

////////////////////////////////////////////////////////////////////////////////
file::file(const file& parent, const io::path& path, bool follow, std::error_code& ec) noexcept :
    path_{ parent.empty() ? path : parent.path() / path }
{
    fd_ = desc{ ::openat(fd_or_cwd(parent), path.c_str(), O_PATH | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW)) };
    if (!fd_)
    {
        if (errno == ENOENT || errno == ENOTDIR) { type_ = file_type::not_found; ec.clear(); }
        else ec = error_code(errno);
        return;
    }

    struct stat stat{};
    if (0 == ::fstat(fd_.get(), &stat))
    {
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
        uid_  = stat.st_uid;
        gid_  = stat.st_gid;
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
    else if (errno == ENOENT)
    {
        type_ = file_type::not_found;
        fd_ = desc{};
        ec.clear();
    }
    else ec = error_code(errno);
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

void file::mode(io::mode mode, std::error_code& ec) noexcept
{
    if (is_symlink()) { ec.clear(); return; }

    if (0 == ::chmod(proxy_path(fd_).c_str(), static_cast<::mode_t>(mode)))
    {
        mode_ = mode;
        ec.clear();
    }
    else ec = error_code(errno);
}

void file::time(io::time time, std::error_code& ec) noexcept
{
    using namespace std::chrono;
    auto dur = time::clock::to_sys(time).time_since_epoch();
    auto sec = duration_cast<seconds>(dur);
    auto nsec = duration_cast<nanoseconds>(dur - sec);

    timespec times[2] = { {0, UTIME_OMIT}, {sec.count(), nsec.count()} };
    if (0 == ::utimensat(fd_.get(), "", times, AT_EMPTY_PATH))
    {
        time_ = time;
        ec.clear();
    }
    else ec = error_code(errno);
}

void file::owner(io::user_id uid, io::group_id gid, std::error_code& ec) noexcept
{
    if (0 == ::fchownat(fd_.get(), "", uid, gid, AT_EMPTY_PATH))
    {
        if (uid != none) uid_ = uid;
        if (gid != none) gid_ = gid;
        ec.clear();
    }
    else ec = error_code(errno);
}

////////////////////////////////////////////////////////////////////////////////
void copy_file(const file& source, const file& target_parent, const path& target_name,
    std::error_code& ec, const progress_callback& cb)
{
    constexpr file_size chunk_size = 4194304; // 4MiB

    auto tick = [&](file_size copied)
    {
        if (!cb || cb(copied)) return true;

        ec = std::make_error_code(std::errc::operation_canceled);
        return false;
    };

    desc in{ ::open(proxy_path(source.fd()).c_str(), O_RDONLY | O_CLOEXEC) };
    if (!in) { ec = error_code(errno); return; }

    desc out{ ::openat(fd_or_cwd(target_parent), target_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666) };
    if (!out) { ec = error_code(errno); return; }

    bool copying = true;

    while (copying)
    {
        auto copied = ::copy_file_range(in.get(), nullptr, out.get(), nullptr, chunk_size, 0);
        if (copied < 0)
        {
            if (errno == EINTR) continue;
            // not supported
            if (errno == EINVAL || errno == ENOSYS || errno == ENOTSUP || errno == EOPNOTSUPP || errno == EXDEV) break;

            ec = error_code(errno);
            return;
        }
        else if (copied > 0) { if (!tick(copied)) return; }
        else copying = false;
    }

    while (copying)
    {
        auto copied = ::sendfile(out.get(), in.get(), nullptr, chunk_size);
        if (copied < 0)
        {
            if (errno == EINTR) continue;
            // not supported
            if (errno == EINVAL || errno == ENOSYS) break;

            ec = error_code(errno);
            return;
        }
        else if (copied > 0) { if (!tick(copied)) return; }
        else copying = false;
    }

    if (copying)
    {
        auto buf = std::make_unique_for_overwrite<char[]>(chunk_size);
        do
        {
            auto read = ::read(in.get(), buf.get(), chunk_size);
            if (read < 0)
            {
                if (errno == EINTR) continue;

                ec = error_code(errno);
                return;
            }
            else if (read > 0)
            {
                for (auto p = buf.get(); read; )
                {
                    auto written = ::write(out.get(), p, read);
                    if (written < 0)
                    {
                        if (errno == EINTR) continue;

                        ec = error_code(errno);
                        return;
                    }
                    else
                    {
                        read -= written; p += written;
                        if (!tick(written)) return;
                    }
                }
            }
            else copying = false;
        }
        while (copying);
    }

    ec.clear();
}

void create_directory(const file& parent, const path& name, std::error_code& ec) noexcept
{
    if (0 == ::mkdirat(fd_or_cwd(parent), name.c_str(), 0777)) ec.clear();
    else ec = error_code(errno);
}

void create_symlink(const file& parent, const path& name, const path& link_target, std::error_code& ec) noexcept
{
    if (0 == ::symlinkat(link_target.c_str(), fd_or_cwd(parent), name.c_str())) ec.clear();
    else ec = error_code(errno);
}

namespace
{

void create_node(const file& parent, const path& name, mode_t type, device rdev, std::error_code& ec) noexcept
{
    if (0 == ::mknodat(fd_or_cwd(parent), name.c_str(), type | 0666, rdev)) ec.clear();
    else ec = error_code(errno);
}

}

void create_block_device(const file& parent, const path& name, device rdev, std::error_code& ec) noexcept {
    create_node(parent, name, S_IFBLK, rdev, ec);
}
void create_char_device(const file& parent, const path& name, device rdev, std::error_code& ec) noexcept {
    create_node(parent, name, S_IFCHR, rdev, ec);
}
void create_fifo(const file& parent, const path& name, std::error_code& ec) noexcept {
    create_node(parent, name, S_IFIFO, 0, ec);
}
void create_socket(const file& parent, const path& name, std::error_code& ec) noexcept {
    create_node(parent, name, S_IFSOCK, 0, ec);
}

std::generator<std::expected<path, std::error_code>> directory_iterator(const file& dir)
{
    auto dir_close = [](DIR* p) { ::closedir(p); };
    std::unique_ptr<DIR, decltype (dir_close)> dp;

    desc fd{ ::open(proxy_path(dir.fd()).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC) };
    if (fd) dp.reset( ::fdopendir(fd.get()) );

    if (dp)
    {
        fd.release(); // fdopendir owns it now
        for (;;)
        {
            errno = 0;
            if (auto e = ::readdir(dp.get()))
            {
                std::string_view name = e->d_name;
                if (name != "." && name != "..") co_yield name;
            }
            else
            {
                if (errno) co_yield std::unexpected(error_code(errno));
                break;
            }
        }
    }
    else co_yield std::unexpected(error_code(errno));
}

void remove(const file& parent, const path& name, std::error_code& ec) noexcept
{
    if (0 == ::unlinkat(fd_or_cwd(parent), name.c_str(), 0)) ec.clear();
    else ec = error_code(errno);
}

void remove_directory(const file& parent, const path& name, std::error_code& ec) noexcept
{
    if (0 == ::unlinkat(fd_or_cwd(parent), name.c_str(), AT_REMOVEDIR)) ec.clear();
    else ec = error_code(errno);
}

void rename(const file& parent, const path& name,
    const file& new_parent, const path& new_name, std::error_code& ec) noexcept
{
    if (0 == ::renameat(fd_or_cwd(parent), name.c_str(), fd_or_cwd(new_parent), new_name.c_str())) ec.clear();
    else ec = error_code(errno);
}

}
