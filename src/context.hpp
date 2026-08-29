////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"

#include <atomic>
#include <chrono>
#include <cstddef> // std::size_t
#include <cstdio> // std::fflush
#include <format>
#include <mutex>
#include <print>
#include <tuple>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
enum class unlink { never, always, auto_ };
enum class update { none, all, older, changed, size, };

enum print_option { retain, replace };

struct context
{
    std::size_t jobs = 1;

    bool follow_dest_links = false;
    bool interactive = false;

    bool keep_group = false, keep_user = false;
    bool keep_mode = false;
    bool keep_time = false;

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

    std::atomic<bool> error_free{ true };
    bool confirm_all = false;

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};
    double percent_copied = 0;

    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_time = start_time;
    long last_bytes = 0;
    double speed = 0;

    ////////////////////
    void add_dir_attr(io::path path, io::attrib attr) {
        dir_attrs_.emplace_back(std::move(path), std::move(attr));
    }
    auto& dir_attrs() const noexcept { return dir_attrs_; }

    void add_rmdir(io::path path) { rmdirs_.push_back(std::move(path)); }
    auto& rmdirs() const noexcept { return rmdirs_; }

    ////////////////////
    [[nodiscard]] auto get_print_lock() { return std::unique_lock{mutex_}; }

    template <typename... Args>
    void print(print_option option, std::format_string<Args...> fmt, Args&&... args)
    {
        std::lock_guard guard{mutex_};
        print_locked(option, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void print_locked(print_option option, std::format_string<Args...> fmt, Args&&... args)
    {
        if (print_ == replace) std::print("\033[{}F\033[K", 1);
        print_ = option;

        std::print(fmt, std::forward<Args>(args)...);
        std::fflush(stdout);
    }

private:
    ////////////////////
    std::mutex mutex_;
    print_option print_ = retain;

    std::vector< std::tuple<io::path, io::attrib> > dir_attrs_;
    std::vector< io::path > rmdirs_;
};
