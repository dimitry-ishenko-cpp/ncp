////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io_file.hpp"

#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>

#include <dirent.h>
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

        ec.clear();
    }
}

path file::get_target_path(std::error_code& ec) const
{
    if (!is_symlink()) { ec = make_error_code(EINVAL); return {}; }

    for (std::string buf(size_ ? size_ + 1 : 128, '\0'); ;)
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
        else if (buf.size() == 4096)
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
void create_directory(const path& path, perms perms, std::error_code& ec)
{
    auto res = ::mkdir(path.c_str(), static_cast<::mode_t>(perms));
    if (res && errno != EEXIST) ec = make_error_code(errno);
    else ec.clear();
}

////////////////////////////////////////////////////////////////////////////////
void create_symlink(const path& to, const path& new_link, std::error_code& ec)
{
    auto res = ::symlink(to.c_str(), new_link.c_str());
    if (res) ec = make_error_code(errno);
    else ec.clear();
}

////////////////////////////////////////////////////////////////////////////////
std::generator<std::expected<path, std::error_code>> directory_iterator(const path& path)
{
    if (auto dirp = ::opendir(path.c_str()))
    {
        for (;;)
        {
            errno = 0;
            if (auto e = readdir(dirp))
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
        ::closedir(dirp);
    }
    else co_yield std::unexpected(make_error_code(errno));
}

}
