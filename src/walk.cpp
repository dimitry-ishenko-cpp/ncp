////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "file.hpp"
#include "options.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <filesystem>
#include <generator>
#include <ranges>

////////////////////////////////////////////////////////////////////////////////
void create_directory(options& options, state& state, io::file& source, io::file& target, std::error_code& ec)
{
    io::attrib attr;
    if (options.keep_group) attr.gid = source.gid();
    if (options.keep_mode ) attr.mode= source.mode();
    if (options.keep_user ) attr.uid = source.uid();

    io::create_directory(target.path(), attr, ec);
}

void create_symlink(options& options, state& state, io::path& to, io::file& source, io::file& target, std::error_code& ec)
{
    io::attrib attr;
    if (options.keep_group) attr.gid = source.gid();
    if (options.keep_user ) attr.uid = source.uid();

    io::create_symlink(to, target.path(), attr, ec);
}

void post_copy_file(options& options, state& state, asio::thread_pool& pool, io::file& source, io::file& target)
{
    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

    asio::post(pool, [&options, &state, source = std::move(source), target = std::move(target)]
    {
        if (state.quit.load(std::memory_order_relaxed)) return;

        std::error_code ec;
        io::copy_file(source, target.path(), ec);
        if (ec) { state.add_error(ec, source.path(), target.path()); return; }

        state.files_copied.fetch_add(1, std::memory_order_relaxed);
        state.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
    });
}

////////////////////////////////////////////////////////////////////////////////
std::generator<io::file> walk_dir(options& options, state& state, io::file& dir)
{
    for (auto&& expected_path : io::directory_iterator(dir.path()))
        if (expected_path)
        {
            std::error_code ec;
            io::file child{*expected_path, ec};
            if (ec) { state.add_error(ec, *expected_path); continue; }

            co_yield child;

            if (child.is_symlink() && !options.keep_links)
            {
                child = child.follow_symlinks(ec);
                if (ec) { state.add_error(ec, child.path()); continue; }
            }

            if (child.is_directory())
                co_yield std::ranges::elements_of( walk_dir(options, state, child) );
        }
        else state.add_error(expected_path.error());
}

void walk_one(options& options, state& state, asio::thread_pool& pool, io::file& source, io::file& target)
{
    std::error_code ec;

    if (source.is_symlink() && options.keep_links)
    {
        auto link_target = source.get_target_path(ec);
        if (!ec)
        {
            create_symlink(options, state, link_target, source, target, ec);
            if (ec) state.add_error(ec, link_target, target.path());
        }
        else state.add_error(ec, source.path());
    }
    else
    {
        if (source.is_symlink())
        {
            source = source.follow_symlinks(ec);
            if (ec) { state.add_error(ec, source.path()); return; }
        }

        if (source.is_directory())
        {
            if (!options.recursive)
            {
                state.add_error("Skipping directory", source.path());
                return;
            }

            create_directory(options, state, source, target, ec);
            if (ec) { state.add_error(ec, target.path()); return; }

            for (auto&& source_child : walk_dir(options, state, source))
            {
                if (state.quit.load(std::memory_order_relaxed)) break;

                auto name = source_child.path().lexically_relative(source.path());
                io::file target_child{ target.path() / name, ec };
                if (ec) { state.add_error(ec, target_child.path()); continue; }

                if (source_child.is_symlink() && options.keep_links)
                {
                    auto link_target = source_child.get_target_path(ec);
                    if (!ec)
                    {
                        create_symlink(options, state, link_target, source_child, target_child, ec);
                        if (ec) state.add_error(ec, link_target, target_child.path());
                    }
                    else state.add_error(ec, source_child.path());
                }
                else
                {
                    if (source_child.is_symlink())
                    {
                        source_child = source_child.follow_symlinks(ec);
                        if (ec) { state.add_error(ec, source_child.path()); continue; }
                    }

                    if (source_child.is_directory())
                    {
                        create_directory(options, state, source_child, target_child, ec);
                        if (ec) state.add_error(ec, target_child.path());
                    }
                    else post_copy_file(options, state, pool, source_child, target_child);
                }
            }
        }
        else post_copy_file(options, state, pool, source, target);
    }
}

void walk_all(options& options, state& state, asio::thread_pool& pool, std::vector<io::file>& sources, io::file& target)
{
    if (target.is_directory())
    {
        for (auto&& source : sources)
        {
            if (state.quit.load(std::memory_order_relaxed)) break;

            if (source.path().has_filename()) // trailing_slash
            {
                std::error_code ec;
                target = io::file{target.path() / source.path().filename(), io::follow_symlinks, ec};
                if (ec) throw io::exception{"walk_all", target.path(), ec};
            }

            walk_one(options, state, pool, source, target);
        }
    }
    else if (sources.size() == 1)
    {
        walk_one(options, state, pool, sources.front(), target);
    }
    else throw io::exception{"main",
        target.path(), std::make_error_code(std::errc::not_a_directory)
    };
}
