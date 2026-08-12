////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
struct options
{
    std::vector<fs::path> source_paths;
    fs::path target_path;

    bool recursive = false;
    bool keep_links = false;
};
