////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"
#include <vector>

////////////////////////////////////////////////////////////////////////////////
struct options
{
    std::vector<io::path> source_paths;
    io::path target_path;

    bool recursive = false;
    bool keep_links = false;
};
