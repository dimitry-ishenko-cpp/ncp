////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

#include <array>
#include <cerrno>
#include <chrono>

#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

namespace
{

inline auto make_error_code(int val) noexcept { return std::error_code{val, std::generic_category()}; }

inline auto mtime(time time) noexcept
{
    using namespace std::chrono;
    auto dur = time::clock::to_sys(time).time_since_epoch();
    auto sec = duration_cast<seconds>(dur);
    auto nsec = duration_cast<nanoseconds>(dur - sec);

    return std::array{ timespec{0, UTIME_OMIT}, timespec{sec.count(), nsec.count()} };
}

}

void modify(const path& path, const attrib& attr, std::error_code& ec) noexcept
{
    if (attr.mode && ::chmod(path.c_str(), static_cast<::mode_t>(*attr.mode)))
        ec = make_error_code(errno);
    else if (attr.time && ::utimensat(AT_FDCWD, path.c_str(), mtime(*attr.time).data(), AT_SYMLINK_NOFOLLOW))
        ec = make_error_code(errno);
    else if ((attr.uid || attr.gid) && ::lchown(path.c_str(), attr.uid.value_or(-1), attr.gid.value_or(-1)))
        ec = make_error_code(errno);
    else ec.clear();
}

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
