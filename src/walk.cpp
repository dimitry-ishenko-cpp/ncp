////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io/file.hpp"
#include "io/file_info.hpp"
#include "options.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <filesystem>
#include <generator>
#include <ranges>

////////////////////////////////////////////////////////////////////////////////
void post_copy_file(const options& options, state& state, asio::thread_pool& pool, const io::file_info& source, const io::path& target)
{
    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

    asio::post(pool, [&options, &state, source, target]
    {
        if (state.quit.load(std::memory_order_relaxed)) return;

        std::error_code ec;
        auto res = copy_file(source.path(), target, ec);
        if (!res) { state.add_error(ec, source.path(), target); return; }

        state.files_copied.fetch_add(1, std::memory_order_relaxed);
        state.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
    });
}

std::generator<io::file_info> walk_dir(const options& options, state& state, io::path dir)
{
    for (auto&& e_path : io::directory_iterator(dir))
        if (e_path)
        {
            auto e_path_info = io::file_info::get(*e_path);
            if (!e_path_info) { state.add_error(e_path_info.error()); continue; }

            co_yield *e_path_info;

            if (e_path_info->is_symlink() && !options.keep_links)
                if (!(e_path_info = e_path_info->follow_symlinks()))
                {
                    state.add_error(e_path_info.error());
                    continue;
                }

            if (e_path_info->is_directory())
                co_yield std::ranges::elements_of( walk_dir(options, state, e_path_info->path()) );
        }
        else state.add_error(e_path.error());
}

void walk_one(const options& options, state& state, asio::thread_pool& pool, const io::path& source, const io::path& target)
{
    auto e_source_info = io::file_info::get(source);
    if (!e_source_info) { state.add_error(e_source_info.error()); return; }

    if (e_source_info->is_symlink() && options.keep_links)
    {
        auto res = e_source_info->get_target_path()
            .and_then([&](auto&& link_target) {
                return io::create_symlink(link_target, target);
            });
        if (!res) state.add_error(res.error());
    }
    else
    {
        if (e_source_info->is_symlink())
        {
            e_source_info = e_source_info->follow_symlinks();
            if (!e_source_info) { state.add_error(e_source_info.error()); return; }
        }

        if (e_source_info->is_directory())
        {
            if (!options.recursive)
            {
                state.add_error("Skipping directory", source);
                return;
            }

            auto res = io::create_directory(target);
            if (!res) { state.add_error(res.error()); return; }

            for (auto&& source_child_info : walk_dir(options, state, source))
            {
                if (state.quit.load(std::memory_order_relaxed)) break;

                auto target_child = target / source_child_info.path().lexically_relative(source);

                if (source_child_info.is_symlink() && options.keep_links)
                {
                    auto res = source_child_info.get_target_path()
                        .and_then([&](auto&& link_target) {
                            return io::create_symlink(link_target, target_child);
                        });
                    if (!res) state.add_error(res.error());
                }
                else
                {
                    if (source_child_info.is_symlink())
                    {
                        auto res = source_child_info.follow_symlinks();
                        if (!res) { state.add_error(res.error()); continue; }
                        source_child_info = *std::move(res);
                    }

                    if (source_child_info.is_directory())
                    {
                        auto res = io::create_directory(target_child);
                        if (!res) state.add_error(res.error());
                    }
                    else post_copy_file(options, state, pool, source_child_info, target_child);
                }
            }
        }
        else post_copy_file(options, state, pool, *e_source_info, target);
    }
}

void walk_all(const options& options, state& state, asio::thread_pool& pool)
{
    auto e_target_info = io::file_info::get(options.target);
    if (!e_target_info) { state.add_error(e_target_info.error()); return; }

    if (e_target_info->is_directory())
    {
        for (auto&& source : options.sources)
        {
            if (state.quit.load(std::memory_order_relaxed)) break;

            auto trailing_slash = !source.has_filename();
            auto target = trailing_slash ? options.target : options.target / source.filename();

            walk_one(options, state, pool, source, target);
        }
    }
    else
    {
        if (options.sources.size() == 1)
        {
            auto& source = options.sources.front();
            auto& target = options.target;
            walk_one(options, state, pool, source, target);
        }
        else state.add_error(std::errc::not_a_directory, options.target);
    }
}
