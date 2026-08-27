////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "file.hpp"

#include <atomic>
#include <cstddef> // std::size_t
#include <format>
#include <mutex>
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
    unlink unlink_ = unlink::never;
    update update_ = update::all;

    ////////////////////
    std::atomic<bool> quit{ false };
    std::atomic<int> signal{0};

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};

    std::vector< std::tuple<io::path, io::attrib> > dir_attrs;

    bool add_error(std::string msg, const io::path& path1 = {}, const io::path& path2 = {})
    {
        if (!path1.empty()) msg += std::format(": {}", path1.string());
        if (!path2.empty()) msg += std::format(" => {}", path2.string());

        std::lock_guard guard{mutex_};
        errors_.push_back(std::move(msg));

        error_free_ = false;
        return error_free_;
    }
    bool add_error(std::error_code ec, const auto&... path) {
        return add_error(std::format("{}", io::exception{"", path..., ec}));
    }
    bool add_error(std::errc cond, const auto&... path) {
        return add_error(std::make_error_code(cond), path...);
    }

    auto drain_errors()
    {
        std::lock_guard guard{mutex_};
        return std::exchange(errors_, {});
    }

    auto error_free()
    {
        std::lock_guard guard{mutex_};
        return error_free_;
    }

private:
    std::mutex mutex_;
    std::vector<std::string> errors_;
    bool error_free_ = true;
};
