////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <exception>
#include <filesystem>
#include <print>

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
inline void print_error(const std::exception& e)
{
    std::print("{}\n", e.what());
}

inline void print_error(const fs::filesystem_error& e)
{
    if (e.path1().empty()) std::print("{}\n", e.code().message());
    else std::print("{}: '{}'\n", e.code().message(), e.path1().string());
}
