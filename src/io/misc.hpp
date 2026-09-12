////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "types.hpp"

#include <cstdint>
#include <system_error> // std::error_code

////////////////////////////////////////////////////////////////////////////////
namespace io
{

user_id effective_user_id() noexcept;
bool have_cap_chown() noexcept;

std::uint64_t max_open_file_limit(std::error_code& ec) noexcept;
void set_open_file_limit(std::uint64_t, std::error_code& ec) noexcept;

void set_signal_callback(void (*)(int signal));

int term_width() noexcept;

}
