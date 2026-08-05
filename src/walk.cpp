////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "options.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <filesystem>
#include <generator>
#include <ranges>

namespace fs = std::filesystem;

void copy_file(const options&, state&, const fs::path&, const fs::path&);

////////////////////////////////////////////////////////////////////////////////
std::generator<fs::directory_entry> walk(const options& options, state& state, fs::path dir)
{
    std::error_code ec;
    fs::directory_iterator it{dir, ec}, end{};
    if (ec) { state.add_error(ec, dir); co_return; }

    while (it != end)
    {
        co_yield *it;
        if (it->is_directory(ec))
        {
            if (!it->is_symlink(ec) || !options.symlink_other)
                co_yield std::ranges::elements_of( walk(options, state, it->path()) );
        }

        it.increment(ec);
        if (ec) { state.add_error(ec, dir); co_return; }
    }
}

void walk_dir(const options& options, state& state, asio::thread_pool& pool, const fs::path& source, const fs::path& target)
{
    std::error_code ec;
    fs::create_directory(target, ec);
    if (ec) { state.add_error(ec, target); return; }

    for (auto&& entry : walk(options, state, source))
    {
        if (state.quit.load(std::memory_order_relaxed)) break;

        auto target_path = target / entry.path().lexically_relative(source);

        auto is_link = entry.is_symlink(ec);
        if (ec) { state.add_error(ec, entry.path()); continue; }

        auto is_dir = entry.is_directory(ec);
        if (ec) { state.add_error(ec, entry.path()); continue; }

        if (is_link && (is_dir ? options.symlink_other : options.symlink_files))
        {
            auto link_target = fs::read_symlink(entry.path(), ec);
            if (!ec)
            {
                fs::create_symlink(link_target, target_path, ec);
                if (ec) state.add_error(ec, target_path);
            }
            else state.add_error(ec, entry.path());
        }
        else if (is_dir)
        {
            fs::create_directory(target_path, ec);
            if (ec) state.add_error(ec, target_path);
        }
        else
        {
            asio::post(pool, [&state, &options, source = entry.path(), target = target_path]{
                copy_file(options, state, source, target);
            });
        }
    }
}

void walk_one(const options& options, state& state, asio::thread_pool& pool, const fs::path& source, const fs::path& target)
{
    std::error_code ec;

    auto is_link = fs::is_symlink(source, ec);
    if (ec) { state.add_error(ec, source); return; }

    auto is_dir = fs::is_directory(source, ec);
    if (ec) { state.add_error(ec, source); return; }

    if (is_link && (is_dir ? options.symlink_other : options.symlink_files))
    {
        auto link_target = fs::read_symlink(source, ec);
        if (!ec)
        {
            fs::create_symlink(link_target, target, ec);
            if (ec) state.add_error(ec, target);
        }
        else state.add_error(ec, source);
    }
    else if (is_dir)
    {
        if (options.recursive) walk_dir(options, state, pool, source, target);
        else state.add_error("Skipping directory", source);
    }
    else
    {
        asio::post(pool, [&state, &options, source, target]{
            copy_file(options, state, source, target);
        });
    }
}

void walk_all(const options& options, state& state, asio::thread_pool& pool)
{
    std::error_code ec;
    auto is_dir = fs::is_directory(options.target, ec);
    if (ec) { state.add_error(ec, options.target); return; }

    if (is_dir)
    {
        for (auto&& source : options.sources)
        {
            if (state.quit.load(std::memory_order_relaxed)) break;

            auto trailing_slash = !source.has_filename();
            auto target = trailing_slash ? options.target : options.target / source.filename();

            walk_one(options, state, pool, source, target);
        }
    }
    else if (options.sources.size() == 1)
    {
        auto& source = options.sources.front();
        auto& target = options.target;
        walk_one(options, state, pool, source, target);
    }
    else state.add_error(std::errc::not_a_directory, options.target);
}
