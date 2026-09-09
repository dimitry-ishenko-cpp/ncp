////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "misc.hpp"

#include <cerrno>

#include <sys/resource.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

inline auto error_code(int val) noexcept {
    return std::error_code{val, std::generic_category()};
}

}

void raise_open_file_limit(std::error_code& ec) noexcept
{
    struct rlimit rl;
    if (0 == ::getrlimit(RLIMIT_NOFILE, &rl))
    {
        rl.rlim_cur = rl.rlim_max;
        if (0 == ::setrlimit(RLIMIT_NOFILE, &rl)) ec.clear();
        else ec = error_code(errno);
    }
    else ec = error_code(errno);
}

}
