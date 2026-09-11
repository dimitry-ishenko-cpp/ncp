////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "message.hpp"

std::unique_lock<std::mutex> get_print_lock()
{
    static std::mutex mutex;
    return std::unique_lock{mutex};
}

print_option state = retain;
