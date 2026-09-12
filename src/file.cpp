////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"
#include <cerrno>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

inline auto make_error_code(int val) noexcept { return std::error_code{val, std::generic_category()}; }

////////////////////////////////////////////////////////////////////////////////
acl get_acl(const path& path, std::error_code& ec)
{
    acl acl;
    acl.access.reset(acl_get_file(path.c_str(), ACL_TYPE_ACCESS));
    if (!acl.access && errno != ENODATA && errno != ENOTSUP) ec = make_error_code(errno);
    else ec.clear();
    return acl;
}

acl get_directory_acl(const path& path, std::error_code& ec)
{
    auto acl = get_acl(path, ec);
    if (ec) return acl;

    acl.default_.reset(acl_get_file(path.c_str(), ACL_TYPE_DEFAULT));
    if (!acl.default_ && errno != ENODATA && errno != ENOTSUP) ec = make_error_code(errno);
    else ec.clear();
    return acl;
}

void set_acl(const path& path, const acl& acl, std::error_code& ec)
{
    ec.clear();
    if (acl.access)
    {
        if (acl_set_file(path.c_str(), ACL_TYPE_ACCESS, acl.access.get()))
        {
            if (errno != ENOTSUP) ec = make_error_code(errno);
            return;
        }
    }
    if (acl.default_)
    {
        if (acl_set_file(path.c_str(), ACL_TYPE_DEFAULT, acl.default_.get()))
        {
            if (errno != ENOTSUP) ec = make_error_code(errno);
            return;
        }
    }
}

}
