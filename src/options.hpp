////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/types.hpp"

////////////////////////////////////////////////////////////////////////////////
enum class unlink { never, always, force, auto_ };
enum class update { none, all, older, changed, size, };

struct options
{
    bool can_chown = false;
    io::user_id uid = -1;

    bool copy_all = true;
    bool follow_links = true;
    bool keep_acl = false;
    bool keep_devices = false;
    bool keep_group = false;
    bool keep_hardlinks = false;
    bool keep_mode = false;
    bool keep_special = false;
    bool keep_time = false;
    bool keep_user = false;

    constexpr bool keep_attrs() noexcept {
        return keep_acl || keep_group || keep_mode || keep_time || keep_user;
    }

    bool move = false;
    bool progress = false;
    bool recursive = false;
    bool skip_all = false;

    enum unlink unlink = unlink::auto_;
    enum update update = update::all;

    bool verbose = false;
};

