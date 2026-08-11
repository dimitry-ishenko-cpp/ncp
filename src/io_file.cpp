////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io_file.hpp"

#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <dirent.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

inline auto make_error_code(int val) { return std::error_code{val, std::generic_category()}; }

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

        size_ = static_cast<io::file_size>(st.st_size);
        mode_ = static_cast<io::mode>(st.st_mode & 07777);
        uid_  = static_cast<io::uid>(st.st_uid);
        gid_  = static_cast<io::gid>(st.st_gid);
        hardlink_count_ = static_cast<io::hardlink_count>(st.st_nlink);

        using namespace std::chrono;
        auto mtime = duration_cast<io::time::duration>(
            seconds{st.st_mtim.tv_sec} + nanoseconds{st.st_mtim.tv_nsec}
        );
        time_ = io::time{mtime};

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
void copy_file(const file& source, const path& target_path, std::error_code& ec)
{
    struct auto_close
    {
        int fd = -1;
        ~auto_close() { if (fd != -1) ::close(fd); }
    };
    constexpr file_size chunk_size = 4 * 1024 * 1024;

    auto_close in{ ::open(source.path().c_str(), O_RDONLY | O_CLOEXEC) };
    if (in.fd < 0) { ec = make_error_code(errno); return; }

    auto_close out{ ::open(target_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666) };
    if (out.fd < 0) { ec = make_error_code(errno); return; }

    auto remain = source.size();

    // try copy_file_range
    while (remain)
    {
        auto copied = ::copy_file_range(in.fd, nullptr, out.fd, nullptr, std::min(remain, chunk_size), 0);
        if (copied < 0)
        {
            if (errno == EINTR) continue;

            // not supported
            if (errno == ENOSYS || errno == ENOTSUP || errno == EOPNOTSUPP || errno == EXDEV) break;

            ec = make_error_code(errno);
            return;
        }
        else if (copied > 0) remain -= copied;
        else remain = 0; // file shrunk?
    }

    // try sendfile
    while (remain)
    {
        auto copied = ::sendfile(out.fd, in.fd, nullptr, std::min(remain, chunk_size));
        if (copied < 0)
        {
            if (errno == EINTR) continue;

            // not supported
            if (errno == EINVAL || errno == ENOSYS) break;

            ec = make_error_code(errno);
            return;
        }
        else if (copied > 0) remain -= copied;
        else remain = 0; // file shrunk?
    }

    // try read/write
    if (remain)
    {
        auto buf = std::make_unique_for_overwrite<char[]>(chunk_size);
        do
        {
            auto rn = ::read(in.fd, buf.get(), std::min(remain, chunk_size));
            if (rn < 0)
            {
                if (errno == EINTR) continue;

                ec = make_error_code(errno);
                return;
            }
            else if (rn > 0)
            {
                for (auto p = buf.get(); rn; )
                {
                    auto wn = ::write(out.fd, p, rn);
                    if (wn < 0)
                    {
                        if (errno == EINTR) continue;

                        ec = make_error_code(errno);
                        return;
                    }
                    else rn -= wn, p += wn, remain -= wn;
                }
            }
            else remain = 0; // file shrunk?
        }
        while (remain);
    }

    ec.clear();
}

////////////////////////////////////////////////////////////////////////////////
void create_directory(const path& path, mode mode, std::error_code& ec)
{
    if (::mkdir(path.c_str(), static_cast<::mode_t>(mode)))
    {
        if (errno == EEXIST)
        {
            struct stat st{};
            if (!::lstat(path.c_str(), &st) && S_ISDIR(st.st_mode)) ec.clear();
            else ec = make_error_code(EEXIST);
        }
        else ec = make_error_code(errno);
    }
    else ec.clear();
}

////////////////////////////////////////////////////////////////////////////////
void create_symlink(const path& to, const path& new_link, std::error_code& ec)
{
    if (!::symlink(to.c_str(), new_link.c_str())) ec.clear();
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

}
