////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "misc.hpp"

#include <cerrno>
#include <csignal>

#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

inline auto error_code(int val) noexcept {
    return std::error_code{val, std::generic_category()};
}

}

user_id effective_user_id() noexcept { return ::geteuid(); }

bool have_cap_chown() noexcept
{
    bool have_caps = false;
    if (auto caps = ::cap_get_proc())
    {
        cap_flag_value_t val;
        if (0 == ::cap_get_flag(caps, CAP_CHOWN, CAP_EFFECTIVE, &val)) have_caps = (val == CAP_SET);
        ::cap_free(caps);
    }
    return have_caps;
}

std::uint64_t max_open_file_limit(std::error_code& ec) noexcept
{
    struct rlimit rl;
    if (0 == ::getrlimit(RLIMIT_NOFILE, &rl)) ec.clear();
    else ec = error_code(errno);
    return rl.rlim_max;
}

void set_open_file_limit(std::uint64_t nofile, std::error_code& ec) noexcept
{
    struct rlimit rl{ .rlim_cur = nofile, .rlim_max = nofile };
    if (0 == ::setrlimit(RLIMIT_NOFILE, &rl)) ec.clear();
    else ec = error_code(errno);
}

void set_signal_callback(void (*cb)(int signal))
{
    std::signal(SIGINT, cb);
    std::signal(SIGTERM, cb);
}

int term_width() noexcept
{
    struct winsize w;
    return (0 == ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)) ? w.ws_col : 80;
}

}
