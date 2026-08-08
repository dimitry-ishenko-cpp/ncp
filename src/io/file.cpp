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

namespace { auto make_error_code(int eval) { return error_code{eval, std::generic_category()}; } }

////////////////////////////////////////////////////////////////////////////////
std::expected<void, error_code> create_directory(const path& path, perms perms)
{
    auto code = ::mkdir(path.c_str(), static_cast<::mode_t>(perms));
    if (code && errno != EEXIST) return std::unexpected(make_error_code(errno));
    else return {};
}

////////////////////////////////////////////////////////////////////////////////
std::expected<void, error_code> create_symlink(const path& to, const path& new_link)
{
    auto code = ::symlink(to.c_str(), new_link.c_str());
    if (code) return std::unexpected(make_error_code(errno));
    else return {};
}

////////////////////////////////////////////////////////////////////////////////
std::generator<std::expected<path, error_code>> directory_iterator(const path& path)
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
                if (errno) co_yield std::unexpected(make_error_code(errno));
                break;
            }
        }
    }
    else co_yield std::unexpected(make_error_code(errno));
}

}
