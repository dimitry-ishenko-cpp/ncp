////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/types.hpp"
#include "smooth.hpp"

#include <atomic>
#include <chrono>
#include <format>
#include <mutex>
#include <print>
#include <string_view>
#include <system_error>

////////////////////////////////////////////////////////////////////////////////
enum print_option { retain, replace };
enum report { current, final };

class printer
{
    print_option state_ = retain;
    static std::mutex mutex;

    std::atomic<int> files_total{0}, files_copied{0};
    std::atomic<io::file_size> bytes_total{0}, bytes_copied{0};
    smooth<double> done{0, .5};

    using clock = std::chrono::steady_clock;
    using seconds = std::chrono::seconds;

    clock::time_point start_time = clock::now();
    clock::time_point tick = start_time;
    io::file_size prev_bytes_copied = 0;
    smooth<double> speed{.1};
    seconds elapse, remain;

    ////////////////////
    template <typename... Args>
    void print(print_option option, std::format_string<Args...> fmt, Args&&... args)
    {
        auto lock = get_print_lock();
        print_locked(option, fmt, std::forward<Args>(args)...);
    }

    void print_message(std::string_view msg) {
        print(retain, "{}\n", msg);
    }
    void print_message(std::string_view type, std::string_view msg) {
        print(retain, "{} {}\n", type, msg);
    }
    void print_message(std::string_view type, std::string_view msg, auto&& file) {
        print(retain, "{} {} '{}'\n", type, msg, file);
    }
    void print_message(std::string_view type, std::string_view msg, auto&& source, auto&& target) {
        print(retain, "{} {} '{}' => '{}'\n", type, msg, source, target);
    }
    void print_message(std::string_view type, std::string_view msg, auto&& file, std::error_code ec) {
        print(retain, "{} {} '{}': {}\n", type, msg, file, ec.message());
    }
    void print_message(std::string_view type, std::string_view msg, auto&& source, auto&& target, std::error_code ec) {
        print(retain, "{} {} '{}' => '{}': {}\n", type, msg, source, target, ec.message());
    }

public:
    ////////////////////
    [[nodiscard]] std::unique_lock<std::mutex> get_print_lock() { return std::unique_lock{mutex}; }

    template <typename... Args>
    void print_locked(print_option option, std::format_string<Args...> fmt, Args&&... args)
    {
        if (state_ == replace) std::print("\033[{}F\033[K", 1);
        state_ = option;

        std::print(fmt, std::forward<Args>(args)...);
        std::fflush(stdout);
    }

    void print_error  (auto&&... args) { print_message("E:", std::forward<decltype (args)>(args)...); }
    void print_info   (auto&&... args) { print_message("I:", std::forward<decltype (args)>(args)...); }
    void print_verbose(auto&&... args) { print_message("V:", std::forward<decltype (args)>(args)...); }
    void print_warn   (auto&&... args) { print_message("W:", std::forward<decltype (args)>(args)...); }

    ////////////////////
    void add_files_total(int n) noexcept { files_total.fetch_add(n, std::memory_order_relaxed); }
    void add_files_copied(int n) noexcept { files_copied.fetch_add(n, std::memory_order_relaxed); }

    void add_bytes_total(io::file_size b) noexcept { bytes_total.fetch_add(b, std::memory_order_relaxed); }
    void add_bytes_copied(io::file_size b) noexcept { bytes_copied.fetch_add(b, std::memory_order_relaxed); }

    void add_files_bytes_total(int n, io::file_size b) noexcept { add_files_total(n); add_bytes_total(b); }
    void add_files_bytes_copied(int n, io::file_size b) noexcept { add_files_copied(n); add_bytes_copied(b); }

    void progress(report = current);
};
