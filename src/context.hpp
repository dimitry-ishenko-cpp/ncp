////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/types.hpp"

#include <atomic>
#include <chrono>
#include <cstddef> // std::size_t

////////////////////////////////////////////////////////////////////////////////
enum class unlink { never, always, auto_ };
enum class update { none, all, older, changed, size, };

struct context
{
    std::size_t jobs = 1;

    bool can_chown = false;
    io::user_id uid = -1;

    bool interactive = false;

    bool keep_mode = false;
    bool keep_time = false;
    bool keep_user = false, keep_group = false;

    bool keep_devices = false;
    bool keep_links = false;
    bool keep_special = false;

    bool move = false;
    bool progress = false;
    bool recursive = false;
    enum unlink unlink_ = unlink::auto_;
    enum update update_ = update::all;
    bool verbose = false;

    ////////////////////
    std::atomic<int> exit_signal{0};
    std::atomic<bool> quit{ false };

    std::atomic<bool> failed{ false }, attr_failed{ false };
    bool copy_all = false, skip_all = false;

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};
    double percent_copied = 0;

    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_time = start_time;
    long last_bytes = 0;
    double speed = 0;
};
