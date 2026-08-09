////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io_file.hpp"
#include "options.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <filesystem>
#include <generator>
#include <ranges>

////////////////////////////////////////////////////////////////////////////////
void post_copy_file(const options& options, state& state, asio::thread_pool& pool, const io::file& source, const io::path& target_path)
{
    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

    asio::post(pool, [&options, &state, source, target_path]
    {
        if (state.quit.load(std::memory_order_relaxed)) return;

        std::error_code ec;
        auto res = copy_file(source.path(), target_path, ec);
        if (!res) { state.add_error(ec, source.path(), target_path); return; }

        state.files_copied.fetch_add(1, std::memory_order_relaxed);
        state.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
    });
}

std::generator<io::file> walk_dir(const options& options, state& state, io::path dir)
{
    for (auto&& expected_path : io::directory_iterator(dir))
        if (expected_path)
        {
            std::error_code ec;
            io::file child{ *expected_path, ec };
            if (ec) { state.add_error(ec, *expected_path); continue; }

            co_yield child;

            if (child.is_symlink() && !options.keep_links)
            {
                std::error_code ec;
                child = child.follow_symlinks(ec);
                if (ec) { state.add_error(ec, child.path()); continue; }
            }

            if (child.is_directory())
                co_yield std::ranges::elements_of( walk_dir(options, state, child.path()) );
        }
        else state.add_error(expected_path.error());
}

void walk_one(const options& options, state& state, asio::thread_pool& pool, const io::path& source_path, const io::path& target_path)
{
    std::error_code ec;
    io::file source{ source_path, ec };
    if (ec) { state.add_error(ec, source_path); return; }

    if (source.is_symlink() && options.keep_links)
    {
        std::error_code ec;
        auto link_target = source.get_target_path(ec);
        if (!ec)
        {
            io::create_symlink(link_target, target_path, ec);
            if (ec) state.add_error(ec, link_target, target_path);
        }
        else state.add_error(ec, source.path());
    }
    else
    {
        if (source.is_symlink())
        {
            std::error_code ec;
            source = source.follow_symlinks(ec);
            if (ec) { state.add_error(ec, source.path()); return; }
        }

        if (source.is_directory())
        {
            if (!options.recursive)
            {
                state.add_error("Skipping directory", source_path);
                return;
            }

            std::error_code ec;
            io::create_directory(target_path, ec);
            if (ec) { state.add_error(ec, target_path); return; }

            for (auto&& source_child : walk_dir(options, state, source_path))
            {
                if (state.quit.load(std::memory_order_relaxed)) break;

                auto target_child_path = target_path / source_child.path().lexically_relative(source_path);

                if (source_child.is_symlink() && options.keep_links)
                {
                    std::error_code ec;
                    auto link_target = source_child.get_target_path(ec);
                    if (!ec)
                    {
                        io::create_symlink(link_target, target_child_path, ec);
                        if (ec) state.add_error(ec, link_target, target_child_path);
                    }
                    else state.add_error(ec, source_child.path());
                }
                else
                {
                    if (source_child.is_symlink())
                    {
                        std::error_code ec;
                        source_child = source_child.follow_symlinks(ec);
                        if (ec) { state.add_error(ec, source_child.path()); continue; }
                    }

                    if (source_child.is_directory())
                    {
                        std::error_code ec;
                        io::create_directory(target_child_path, ec);
                        if (ec) state.add_error(ec, target_child_path);
                    }
                    else post_copy_file(options, state, pool, source_child, target_child_path);
                }
            }
        }
        else post_copy_file(options, state, pool, source, target_path);
    }
}

void walk_all(const options& options, state& state, asio::thread_pool& pool)
{
    std::error_code ec;
    io::file target{ options.target_path, ec };
    if (ec) { state.add_error(ec, options.target_path); return; }

    if (target.is_directory())
    {
        for (auto&& source_path : options.source_paths)
        {
            if (state.quit.load(std::memory_order_relaxed)) break;

            auto trailing_slash = !source_path.has_filename();
            auto target_path = trailing_slash ? options.target_path : options.target_path / source_path.filename();

            walk_one(options, state, pool, source_path, target_path);
        }
    }
    else if (options.source_paths.size() == 1)
    {
        walk_one(options, state, pool, options.source_paths.front(), options.target_path);
    }
    else state.add_error(std::errc::not_a_directory, options.target_path);
}
