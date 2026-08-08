////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <cerrno>
#include <memory>
#include <string_view>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

unexpected make_unexpected(int val, io::path path, io::path path_to)
{
    return unexpected(error_info
    {
        std::error_code{val, std::generic_category()},
        std::move(path), std::move(path_to)
    });
}

////////////////////////////////////////////////////////////////////////////////
expected<void> create_directory(const path& path, perms perms)
{
    auto code = ::mkdir(path.c_str(), static_cast<::mode_t>(perms));
    if (code && errno != EEXIST) return make_unexpected(errno, path);
    else return {};
}

////////////////////////////////////////////////////////////////////////////////
expected<void> create_symlink(const path& to, const path& new_link)
{
    auto code = ::symlink(to.c_str(), new_link.c_str());
    if (code) return make_unexpected(errno, to, new_link);
    else return {};
}

////////////////////////////////////////////////////////////////////////////////
std::generator<expected<path>> directory_iterator(const path& path)
{
    struct dir_close { void operator()(DIR* dirp) { ::closedir(dirp); } };

    std::unique_ptr<DIR, dir_close> dirp{ ::opendir(path.c_str()) };
    if (dirp)
    {
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
                if (errno) co_yield make_unexpected(errno, path);
                break;
            }
        }
    }
    else co_yield make_unexpected(errno, path);
}

}
