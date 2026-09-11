////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/file.hpp"

#include <expected>
#include <functional>
#include <generator>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>

#include <sys/acl.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

struct attrib
{
    std::optional<io::mode> mode;
    std::optional<io::time> time;
    std::optional<io::group_id> gid;
    std::optional<io::user_id> uid;

    explicit operator bool() const noexcept { return mode || time || gid || uid; }
};

using progress_callback = std::function<bool(file_size copied)>;

void copy_file(const file&, const file&, const attrib&, std::error_code&, const progress_callback& = {});
inline void copy_file(const file& source, const file& target, std::error_code& ec, const progress_callback& cb = {}) {
    io::copy_file(source, target, {}, ec, cb);
}

void modify(const path&, const attrib&, std::error_code&) noexcept;

////////////////////////////////////////////////////////////////////////////////
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
