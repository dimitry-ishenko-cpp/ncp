////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <cerrno>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace { auto make_error_code(int eval) { return error_code{eval, std::generic_category()}; } }

std::expected<void, error_code> create_symlink(const path& to, const path& new_link)
{
    auto code = ::symlink(to.c_str(), new_link.c_str());
    if (code) return std::unexpected(make_error_code(errno));
    else return {};
}

}
