////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <utility>

#include <fcntl.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace io
{

class desc
{
    static constexpr int none = -1;
    int fd_ = none;

public:
    constexpr desc() noexcept = default;
    constexpr explicit desc(int fd) noexcept : fd_{fd} { }
    ~desc() { if (fd_ != -1) ::close(fd_); }

    desc(const desc& rhs) noexcept :
        fd_{rhs.fd_ != none ? ::fcntl(rhs.fd_, F_DUPFD_CLOEXEC, 0) : none}
    { }
    constexpr desc(desc&& rhs) noexcept : fd_{std::exchange(rhs.fd_, none)} { }
    constexpr auto& operator=(desc rhs) noexcept { std::swap(fd_, rhs.fd_); return *this; }

    constexpr explicit operator bool() const noexcept { return fd_ != none; }
    constexpr auto get() const noexcept { return fd_; }
};

}
