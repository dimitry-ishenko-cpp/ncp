////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
std::expected<void, error> copy_file(const path& from, const path& to, const options& options)
{
    std::error_code ec;
    fs::copy(from, to, ec);
    if (ec) return std::unexpected(error{ec, from, to});
    else return {};
}

std::expected<void, error> create_directory(const path& p, const options& options)
{
    std::error_code ec;
    fs::create_directory(p, ec);
    if (ec) return std::unexpected(error{ec, p});
    else return {};
}

std::expected<void, error> create_symlink(const path& to, const path& new_link, const options&)
{
    std::error_code ec;
    fs::create_symlink(to, new_link, ec);
    if (ec) return std::unexpected(error{ec, new_link});
    else return {};
}

std::expected<std::uintmax_t, error> file_size(const path& p)
{
    std::error_code ec;
    auto size = fs::file_size(p, ec);
    if (ec) return std::unexpected(error{ec, p});
    else return size;
}
