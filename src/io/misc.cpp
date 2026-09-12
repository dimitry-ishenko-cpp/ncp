////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "misc.hpp"

#include <csignal>

#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

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

void raise_open_file_limit() noexcept
{
    struct rlimit rl;
    if (0 == ::getrlimit(RLIMIT_NOFILE, &rl))
    {
        rl.rlim_cur = rl.rlim_max;
        ::setrlimit(RLIMIT_NOFILE, &rl);
    }
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
