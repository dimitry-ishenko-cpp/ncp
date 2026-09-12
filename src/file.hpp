////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/types.hpp"

#include <memory>
#include <system_error>
#include <type_traits>

#include <sys/acl.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

struct acl
{
    struct acl_delete { void operator()(acl_t p) { acl_free(p); } };
    using acl_ptr = std::unique_ptr<std::remove_pointer_t<acl_t>, acl_delete>;

    acl_ptr access, default_;
};

acl get_acl(const path&, std::error_code&);
acl get_directory_acl(const path&, std::error_code&);
void set_acl(const path&, const io::acl&, std::error_code&);

}
