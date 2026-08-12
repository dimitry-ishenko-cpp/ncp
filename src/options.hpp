////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

struct options
{
    bool keep_group = false, keep_user = false;
    bool keep_mode = false;

    bool recursive = false;
    bool keep_links = false;
};
