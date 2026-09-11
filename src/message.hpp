////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/file.hpp"

#include <cstdio> // std::fflush
#include <format>
#include <mutex>
#include <print>
#include <string_view>
#include <system_error> // std::error_code

////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] std::unique_lock<std::mutex> get_print_lock();

enum print_option { retain, replace };
extern print_option state;

template <typename... Args>
void print_locked(print_option option, std::format_string<Args...> fmt, Args&&... args)
{
    if (state == replace) std::print("\033[{}F\033[K", 1);
    state = option;

    std::print(fmt, std::forward<Args>(args)...);
    std::fflush(stdout);
}

template <typename... Args>
void print(print_option option, std::format_string<Args...> fmt, Args&&... args)
{
    auto lock = get_print_lock();
    print_locked(option, fmt, std::forward<Args>(args)...);
}

////////////////////////////////////////////////////////////////////////////////
inline auto s(const io::file& f) { return f.path().string(); }
inline auto s(const io::path& p) { return p.string(); }

inline void message(std::string_view msg) {
    print(retain, "{}\n", msg);
}
inline void message(std::string_view type, std::string_view msg) {
    print(retain, "{} {}\n", type, msg);
}
inline void message(std::string_view type, std::string_view msg, auto&& file) {
    print(retain, "{} {} '{}'\n", type, msg, s(file));
}
inline void message(std::string_view type, std::string_view msg, auto&& source, auto&& target) {
    print(retain, "{} {} '{}' => '{}'\n", type, msg, s(source), s(target));
}
inline void message(std::string_view type, std::string_view msg, auto&& file, std::error_code ec) {
    print(retain, "{} {} '{}': {}\n", type, msg, s(file), ec.message());
}
inline void message(std::string_view type, std::string_view msg, auto&& source, auto&& target, std::error_code ec) {
    print(retain, "{} {} '{}' => '{}': {}\n", type, msg, s(source), s(target), ec.message());
}
