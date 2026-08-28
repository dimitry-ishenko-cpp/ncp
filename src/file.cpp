////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

struct auto_close
{
    int fd = -1;
    ~auto_close() { if (fd != -1) ::close(fd); }
};

inline bool chmod(const path& path, const attrib& attr) noexcept {
    return !attr.mode || 0 == ::chmod(path.c_str(), static_cast<::mode_t>(*attr.mode));
}

inline bool chown(const path& path, const attrib& attr) noexcept {
    return !(attr.uid || attr.gid) || 0 == ::lchown(path.c_str(), attr.uid.value_or(-1), attr.gid.value_or(-1));
}

inline auto make_error_code(int val) noexcept { return std::error_code{val, std::generic_category()}; }

inline auto mtime(time time) noexcept
{
    using namespace std::chrono;
    auto dur = time::clock::to_sys(time).time_since_epoch();
    auto sec = duration_cast<seconds>(dur);
    auto nsec = duration_cast<nanoseconds>(dur - sec);

    return std::array{ timespec{0, UTIME_OMIT}, timespec{sec.count(), nsec.count()} };
}

inline bool utime(const path& path, const attrib& attr) noexcept {
    return !attr.time || 0 == ::utimensat(AT_FDCWD, path.c_str(), mtime(*attr.time).data(), AT_SYMLINK_NOFOLLOW);
}

}

////////////////////////////////////////////////////////////////////////////////
file::file(io::path path, bool follow_symlinks, std::error_code& ec) noexcept :
    path_{std::move(path)}
{
    struct stat st{};
    auto pfn = follow_symlinks ? ::stat : ::lstat;

    if (pfn(path_.c_str(), &st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            type_ = file_type::not_found;
            ec.clear();
        }
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

        if (type_ == file_type::block)
        {
            auto_close dev{ ::open(path_.c_str(), O_RDONLY | O_CLOEXEC) };
            if (dev.fd >= 0)
            {
                uint64_t bytes = 0;
                if (0 == ::ioctl(dev.fd, BLKGETSIZE64, &bytes)) size_ = bytes;
            }
        }
        else size_ = st.st_size;
        mode_ = static_cast<io::mode>(st.st_mode & 07777);
        gid_  = st.st_gid;
        uid_  = st.st_uid;
        dev_type_ = st.st_rdev;
        dev_  = st.st_dev;
        ino_  = st.st_ino;
        hardlink_count_ = st.st_nlink;

        using namespace std::chrono;
        auto tp = sys_time<nanoseconds>(
            seconds{st.st_mtim.tv_sec} + nanoseconds{st.st_mtim.tv_nsec}
        );
        time_ = time::clock::from_sys(tp);

        ec.clear();
    }
}

path file::get_target_path(std::error_code& ec) const
{
    if (!is_symlink()) { ec = make_error_code(EINVAL); return {}; }

    std::string buf(size_ ? size_ + 1 : 128, '\0');
    for (;;)
    {
        auto len = ::readlink(path_.c_str(), buf.data(), buf.size());
        if (len < 0)
        {
            ec = make_error_code(errno);
            return {};
        }
        else if (len < buf.size())
        {
            ec.clear();
            buf.resize(len);
            return buf;
        }
        else if (buf.size() >= 4096)
        {
            ec = make_error_code(ENAMETOOLONG);
            return {};
        }
        else buf.resize(buf.size() * 2, '\0');
    }
}

file file::follow_symlinks(std::error_code& ec) const
{
    if (!is_symlink())
    {
        ec.clear();
        return *this;
    }
    else return file{path_, io::follow_symlinks, ec};
}

////////////////////////////////////////////////////////////////////////////////
bool can_read(const path& path, std::error_code& ec) noexcept
{
    if (0 == ::access(path.c_str(), R_OK)) ec.clear();
    else ec = make_error_code(errno);
    return !ec;
}

////////////////////////////////////////////////////////////////////////////////
void copy_file(const file& source, const file& target, const attrib& attr, std::error_code& ec, const progress_callback& cb)
{
    constexpr file_size chunk_size = 4 * 1024 * 1024;

    auto tick = [&](file_size copied) {
        if (!cb || cb(copied)) return true;
        ec = std::make_error_code(std::errc::operation_canceled);
        return false;
    };

    auto_close in { ::open(source.path().c_str(), O_RDONLY | O_CLOEXEC) };
    if (in.fd  < 0) { ec = make_error_code(errno); return; }

    auto_close out{ ::open(target.path().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666) };
    if (out.fd < 0) { ec = make_error_code(errno); return; }

    bool copying = true;

    // try copy_file_range
    while (copying)
    {
        auto copied = ::copy_file_range(in.fd, nullptr, out.fd, nullptr, chunk_size, 0);
        if (copied < 0)
        {
            if (errno == EINTR) continue;
            // not supported
            if (errno == EINVAL || errno == ENOSYS || errno == ENOTSUP || errno == EOPNOTSUPP || errno == EXDEV) break;

            ec = make_error_code(errno);
            return;
        }
        else if (copied > 0) { if (!tick(copied)) return; }
        else copying = false;
    }

    // try sendfile
    while (copying)
    {
        auto copied = ::sendfile(out.fd, in.fd, nullptr, chunk_size);
        if (copied < 0)
        {
            if (errno == EINTR) continue;
            // not supported
            if (errno == EINVAL || errno == ENOSYS) break;

            ec = make_error_code(errno);
            return;
        }
        else if (copied > 0) { if (!tick(copied)) return; }
        else copying = false;
    }

    // try read/write
    if (copying)
    {
        auto buf = std::make_unique_for_overwrite<char[]>(chunk_size);
        do
        {
            auto read = ::read(in.fd, buf.get(), chunk_size);
            if (read < 0)
            {
                if (errno == EINTR) continue;

                ec = make_error_code(errno);
                return;
            }
            else if (read > 0)
            {
                for (auto p = buf.get(); read; )
                {
                    auto wrtn = ::write(out.fd, p, read);
                    if (wrtn < 0)
                    {
                        if (errno == EINTR) continue;

                        ec = make_error_code(errno);
                        return;
                    }
                    else
                    {
                        read -= wrtn; p += wrtn;
                        if (!tick(wrtn)) return;
                    }
                }
            }
            else copying = false;
        }
        while (copying);
    }

    if (attr.mode && ::fchmod(out.fd, static_cast<::mode_t>(*attr.mode)))
        ec = make_error_code(errno);
    else if (attr.time && ::futimens(out.fd, mtime(*attr.time).data()))
        ec = make_error_code(errno);
    else if ((attr.uid || attr.gid) && ::fchown(out.fd, attr.uid.value_or(-1), attr.gid.value_or(-1)))
        ec = make_error_code(errno);
    else ec.clear();
}

////////////////////////////////////////////////////////////////////////////////
void create_node(const path& path, mode_t mode, dev rdev, const attrib& attr, std::error_code& ec) noexcept
{
    if (0 == ::mknod(path.c_str(), mode | 0666, rdev)) modify(path, attr, ec);
    else ec = make_error_code(errno);
}

void create_block_device(const path& path, dev type, const attrib& attr, std::error_code& ec) noexcept {
    create_node(path, S_IFBLK, type, attr, ec);
}
void create_char_device(const path& path, dev type, const attrib& attr, std::error_code& ec) noexcept {
    create_node(path, S_IFCHR, type, attr, ec);
}
void create_fifo(const path& path, const attrib& attr, std::error_code& ec) noexcept {
    create_node(path, S_IFIFO, 0, attr, ec);
}
void create_socket(const path& path, const attrib& attr, std::error_code& ec) noexcept {
    create_node(path, S_IFSOCK, 0, attr, ec);
}

////////////////////////////////////////////////////////////////////////////////
void create_directory(const path& path, const attrib& attr, std::error_code& ec) noexcept
{
    if (0 == ::mkdir(path.c_str(), 0777)) modify(path, attr, ec);
    else if (errno == EEXIST)
    {
        struct stat st{};
        if (0 == ::lstat(path.c_str(), &st) && S_ISDIR(st.st_mode)) modify(path, attr, ec);
        else ec = make_error_code(EEXIST);
    }
    else ec = make_error_code(errno);
}

////////////////////////////////////////////////////////////////////////////////
void create_symlink(const path& to, const path& new_link, const attrib& attr, std::error_code& ec) noexcept
{
    if (0 == ::symlink(to.c_str(), new_link.c_str())) modify(new_link, attr, ec);
    else ec = make_error_code(errno);
}

////////////////////////////////////////////////////////////////////////////////
std::generator<std::expected<path, std::error_code>> directory_iterator(const path& path)
{
    auto dir_close = [](DIR* p) { ::closedir(p); };
    std::unique_ptr<DIR, decltype (dir_close)> dirp{ ::opendir(path.c_str()) };

    if (dirp)
        for (;;)
        {
            errno = 0;
            if (auto e = readdir(dirp.get()))
            {
                std::string_view name = e->d_name;
                if (name != "." && name != "..") co_yield path / name;
            }
            else
            {
                if (errno) co_yield std::unexpected(make_error_code(errno));
                break;
            }
        }
    else co_yield std::unexpected(make_error_code(errno));
}

void modify(const path& path, const attrib& attr, std::error_code& ec) noexcept
{
    if (chmod(path, attr) && utime(path, attr) && chown(path, attr)) ec.clear();
    else ec = make_error_code(errno);
}

void remove(const path& path, std::error_code& ec) noexcept
{
    if (0 == ::unlink(path.c_str())) ec.clear();
    else ec = make_error_code(errno);
}

void rename(const path& from, const path& to, std::error_code& ec) noexcept
{
    if (0 == ::rename(from.c_str(), to.c_str())) ec.clear();
    else ec = make_error_code(errno);
}

}
