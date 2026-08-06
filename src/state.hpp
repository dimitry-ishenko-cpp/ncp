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
    void add_error(const error& e)
    {
        if (!e.path2.empty())
            add_error(std::format("{}: '{}' => '{}'", e.code.message(), e.path1.string(), e.path2.string()));
        else if (!e.path1.empty())
            add_error(std::format("{}: '{}'", e.code.message(), e.path1.string()));
        else add_error(e.code.message());
    }
    void add_error(const std::string& msg, const path& path)
    {
        add_error(std::format("{}: '{}'", msg, path.string()));
    }
    void add_error(std::string msg, const path& p1, const path& p2)
    {
        add_error(std::format("{}: '{}' => '{}'", msg, p1.string(), p2.string()));
    }
    void add_error(std::error_code ec, const path& path) { add_error(ec.message(), path); }
    void add_error(std::error_code ec, const path& p1, const path& p2) { add_error(ec.message(), p1, p2); }

    void add_error(std::errc cond, const path& path) { add_error(std::make_error_code(cond), path); }

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
