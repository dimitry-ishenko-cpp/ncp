////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"

#include <array>
#include <atomic>
#include <cstddef> // std::size_t
#include <cstdio> // std::fflush
#include <format>
#include <mutex>
#include <print>
#include <string>
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
    unlink unlink_ = unlink::auto_;
    update update_ = update::all;
    bool verbose = false;

    ////////////////////
    std::atomic<bool> error_free{ true };
    std::atomic<int> exit_signal{0};
    std::atomic<bool> quit{ false };

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};

    std::atomic<bool> confirm_all{ false };

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

    void show_progress()
    {
        auto ft = files_total.load(std::memory_order_relaxed);
        auto fc = files_copied.load(std::memory_order_relaxed);
        auto bt = bytes_total.load(std::memory_order_relaxed);
        auto bc = bytes_copied.load(std::memory_order_relaxed);
        auto pc = bt ? (100.0 * bc / bt) : 100.0;

        if (quit.load(std::memory_order_relaxed)) percent_ = pc;
        else percent_ += (pc - percent_) * 0.3;

        auto percent = static_cast<long>(percent_);
        constexpr auto bar_width = 40;
        auto bar_fill = percent * bar_width / 100;

        std::string bar;
        for (auto n = 0; n < bar_fill; ++n) bar += "█";
        for (auto n = bar_fill; n < bar_width; ++n) bar += "░";

        std::lock_guard lock{mutex_};
        print_locked(replace, " {:>3}% {} {}/{} ● {}/{}\n", percent, bar, fc, ft, human(bc), human(bt));
    }

private:
    ////////////////////
    std::mutex mutex_;
    print_option print_ = retain;

    double percent_ = 0;

    std::vector< std::tuple<io::path, io::attrib> > dir_attrs_;
    std::vector< io::path > rmdirs_;

    ////////////////////
    static std::string human(long bytes)
    {
        constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};

        auto n = 0;
        auto dbl_bytes = static_cast<double>(bytes);
        for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

        return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
    }
};
