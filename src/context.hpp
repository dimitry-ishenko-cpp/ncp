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
#include <cstdio>
#include <format>
#include <mutex>
#include <print>
#include <string>
#include <system_error>
#include <tuple>
#include <utility> // std::exchange
#include <vector>

////////////////////////////////////////////////////////////////////////////////
enum class unlink { never, always, auto_ };
enum class update { none, all, older, changed, size, };

struct context
{
    std::size_t jobs = 1;

    bool follow_dest_links = false;

    bool keep_group = false, keep_user = false;
    bool keep_mode = false;
    bool keep_time = false;

    bool keep_devices = false;
    bool keep_links = false;
    bool keep_special = false;

    bool recursive = false;
    unlink unlink_ = unlink::auto_;
    update update_ = update::all;

    ////////////////////
    std::atomic<bool> quit{ false };
    std::atomic<int> signal{0};

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};

    void add_error(std::string msg, const io::path& path1 = {}, const io::path& path2 = {})
    {
        if (!path1.empty()) msg += std::format(": {}", path1.string());
        if (!path2.empty()) msg += std::format(" => {}", path2.string());

        std::lock_guard guard{mutex_};
        errors_.push_back(std::move(msg));
        error_free_ = false;
    }
    void add_error(std::error_code ec, const auto&... path) {
        add_error(std::format("{}", io::exception{"", path..., ec}));
    }
    void add_error(std::errc cond, const auto&... path) {
        add_error(std::make_error_code(cond), path...);
    }

    auto error_free()
    {
        std::lock_guard guard{mutex_};
        return error_free_;
    }

    void add_dir_attr(io::path path, io::attrib attr) {
        dir_attrs_.emplace_back(std::move(path), std::move(attr));
    }

    auto& dir_attrs() const { return dir_attrs_; }

    void print_status()
    {
        std::lock_guard lock{mutex_};
        if (status_repl_) std::print("\033[{}F\033[K", 1);
        else status_repl_ = true;

        if (auto s = signal.exchange(0)) std::print("Received signal {}, exiting...\n", s);

        for (auto&& e : errors_) std::print("{}\n", e);
        errors_.clear();

        auto ft = files_total.load(std::memory_order_relaxed);
        auto fc = files_copied.load(std::memory_order_relaxed);
        auto bt = bytes_total.load(std::memory_order_relaxed);
        auto bc = bytes_copied.load(std::memory_order_relaxed);
        auto pc = bt ? (100.0 * bc / bt) : 100.0;

        if (quit.load(std::memory_order_relaxed)) smooth_done_ = pc;
        else smooth_done_ += (pc - smooth_done_) * 0.3;

        auto done = static_cast<long>(smooth_done_);
        constexpr auto bar_width = 40;
        auto bar_fill = done * bar_width / 100;

        std::string bar;
        for (auto n = 0; n < bar_fill; ++n) bar += "█";
        for (auto n = bar_fill; n < bar_width; ++n) bar += "░";

        std::print(" {:>3}% {} {}/{} ● {}/{}\n", done, bar, fc, ft, human(bc), human(bt));
        std::fflush(stdout);
    }

private:
    std::mutex mutex_;
    std::vector<std::string> errors_;
    bool error_free_ = true;

    bool status_repl_ = false;
    double smooth_done_ = 0;

    static std::string human(long bytes)
    {
        constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};

        auto n = 0;
        auto dbl_bytes = static_cast<double>(bytes);
        for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

        return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
    }

    std::vector< std::tuple<io::path, io::attrib> > dir_attrs_;
};
