////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <atomic>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <system_error>
#include <utility> // std::exchange
#include <vector>

namespace fs = std::filesystem;

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
    }
    void add_error(const std::string& msg, const fs::path& path)
    {
        add_error(std::format("{}: '{}'", msg, path.string()));
    }
    void add_error(std::string msg, const fs::path& p1, const fs::path& p2)
    {
        add_error(std::format("{}: '{}' => '{}'", msg, p1.string(), p2.string()));
    }
    void add_error(std::error_code ec, const fs::path& path) { add_error(ec.message(), path); }
    void add_error(std::error_code ec, const fs::path& p1, const fs::path& p2) { add_error(ec.message(), p1, p2); }

    void add_error(std::errc cond, const fs::path& path) { add_error(std::make_error_code(cond), path); }

    auto drain_errors()
    {
        std::lock_guard guard{mutex};
        return std::exchange(errors, {});
    }

private:
    std::mutex mutex;
    std::vector<std::string> errors;
};
