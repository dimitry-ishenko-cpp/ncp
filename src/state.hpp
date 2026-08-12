////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"

#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <system_error>
#include <utility> // std::exchange
#include <vector>

////////////////////////////////////////////////////////////////////////////////
struct state
{
    std::atomic<bool> quit{ false };

    std::atomic<long> files_total, files_copied;
    std::atomic<long> bytes_total, bytes_copied;

    void add_error(std::string msg)
    {
        std::lock_guard guard{mutex};
        errors.push_back(std::move(msg));
        ++error_count;
    }
    void add_error(const std::string& msg, const io::path& path) { add_error(std::format("{}: '{}'", msg, path.string())); }
    void add_error(std::error_code ec, const auto&... path) { add_error(std::format("{}", io::exception{"", path..., ec})); }
    void add_error(std::errc cond, const auto&... path) { add_error(std::make_error_code(cond), path...); }

    auto drain_errors()
    {
        std::lock_guard guard{mutex};
        return std::exchange(errors, {});
    }

    auto get_error_count()
    {
        std::lock_guard guard{mutex};
        return error_count;
    }

private:
    std::mutex mutex;
    std::vector<std::string> errors;
    long error_count = 0;
};
