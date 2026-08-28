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
#include <string_view>
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
    bool interactive = false;

    bool keep_group = false, keep_user = false;
    bool keep_mode = false;
    bool keep_time = false;

    bool keep_devices = false;
    bool keep_links = false;
    bool keep_special = false;

    bool progress = false;
    bool recursive = false;
    unlink unlink_ = unlink::auto_;
    update update_ = update::all;
    bool verbose = false;

    ////////////////////
    std::atomic<bool> quit{ false };
    std::atomic<int> signal{0};

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};

    ////////////////////
    void add_error(std::string msg, const io::path& path1 = {}, const io::path& path2 = {})
    {
        if (!path1.empty()) msg += std::format(": {}", path1.string());
        if (!path2.empty()) msg += std::format(" => {}", path2.string());

        error_free_.store(false, std::memory_order_relaxed);

        std::lock_guard guard{error_mutex_};
        errors_.push_back(std::move(msg));
    }
    void add_error(std::error_code ec, const auto&... path) {
        add_error(std::format("{}", io::exception{"", path..., ec}));
    }
    void add_error(std::errc cond, const auto&... path) {
        add_error(std::make_error_code(cond), path...);
    }

    auto error_free() const noexcept {
        return error_free_.load(std::memory_order_relaxed);
    }

    ////////////////////
    void add_dir_attr(io::path path, io::attrib attr) {
        dir_attrs_.emplace_back(std::move(path), std::move(attr));
    }
    auto& dir_attrs() const noexcept { return dir_attrs_; }

    void add_rmdir(io::path path) { rmdirs_.push_back(std::move(path)); }
    auto& rmdirs() const noexcept { return rmdirs_; }

    ////////////////////
    bool confirm(std::string_view reason, const io::path& path)
    {
        if (confirm_all_) return true;

        std::lock_guard lock{print_mutex_};
        for (;;)
        {
            print_impl(retain, "{} {}? [Y/n/a/q] ", reason, path.string());

            auto c = std::getchar();
            auto reply = c;
            while (c != '\n' && c != EOF) c = std::getchar();

            switch (reply)
            {
                case 'y': case 'Y': case '\n': return true;
                case 'n': case 'N': return false;
                case 'a': case 'A': confirm_all_ = true; return true;
                case EOF: std::print("q\n");
                case 'q': case 'Q': quit = true; return false;
            }
        }
    }

    void print_status()
    {
        decltype (errors_) errors;
        {
            std::lock_guard lock{error_mutex_};
            std::swap(errors, errors_);
        }

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

        std::lock_guard lock{print_mutex_};
        for (auto&& e : errors) print_impl(retain, "{}\n", e);

        if (auto s = signal.exchange(0))
            print_impl(retain, "Received signal {}, exiting...\n", s);

        print_impl(replace, " {:>3}% {} {}/{} ● {}/{}\n", done, bar, fc, ft, human(bc), human(bt));
    }

    void print_action(const io::path& source, const io::path& target)
    {
        std::lock_guard lock{print_mutex_};
        print_impl(retain, "{} => {}\n", source.string(), target.string());
    }

private:
    ////////////////////
    std::mutex error_mutex_;
    std::vector<std::string> errors_;
    std::atomic<bool> error_free_{ true };

    std::mutex print_mutex_;
    enum print { retain, replace } print_ = retain;
    bool confirm_all_ = false;
    double smooth_done_ = 0;

    std::vector< std::tuple<io::path, io::attrib> > dir_attrs_;
    std::vector< io::path > rmdirs_;

    ////////////////////
    template <typename... Args>
    void print_impl(print print, std::format_string<Args...> fmt, Args&&... args)
    {
        if (print_ == replace) std::print("\033[{}F\033[K", 1);
        print_ = print;

        std::print(fmt, std::forward<Args>(args)...);
        std::fflush(stdout);
    }

    static std::string human(long bytes)
    {
        constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};

        auto n = 0;
        auto dbl_bytes = static_cast<double>(bytes);
        for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

        return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
    }
};
