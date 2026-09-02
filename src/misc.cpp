////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "misc.hpp"

#include <sys/capability.h>
#include <sys/ioctl.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

int term_width() noexcept
{
    struct winsize w;
    return (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) ? 80 : w.ws_col;
}

uid get_effective_uid() noexcept { return geteuid(); }

bool have_cap_chown() noexcept
{
    bool have_caps = false;
    if (auto caps = cap_get_proc())
    {
        cap_flag_value_t val;
        if (0 == cap_get_flag(caps, CAP_CHOWN, CAP_EFFECTIVE, &val)) have_caps = (val == CAP_SET);
        cap_free(caps);
    }
    return have_caps;
}

}
