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
void post_copy_file(const options& options, state& state, asio::thread_pool& pool, const fs::path& source, const fs::path& target)
{
    std::error_code ec;
    auto size = fs::file_size(source, ec);
    if (ec) { state.add_error(ec, source); return; }

    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(size, std::memory_order_relaxed);

    asio::post(pool, [&options, &state, source, target]{
        copy_file(options, state, source, target);
    });
}

std::generator<fs::directory_entry> walk_dir(const options& options, state& state, fs::path dir)
{
    std::error_code ec;
    fs::directory_iterator it{dir, ec}, end{};
    if (ec) { state.add_error(ec, dir); co_return; }

    while (it != end)
    {
        co_yield *it;

        std::error_code ec;
        if (it->is_directory(ec))
        {
            if (!it->is_symlink(ec) || !options.keep_links)
                co_yield std::ranges::elements_of( walk_dir(options, state, it->path()) );
        }
        // ignore ec above -- it will be dealt with by the yield recepient

        it.increment(ec);
        if (ec) { state.add_error(ec, dir); co_return; }
    }
}

void walk_one(const options& options, state& state, asio::thread_pool& pool, const fs::path& source, const fs::path& target)
{
    std::error_code ec;
    auto status = fs::symlink_status(source, ec);
    if (ec) { state.add_error(ec, source); return; }

    auto is_link = fs::is_symlink(status);
    if (is_link && options.keep_links)
    {
        auto link_target = fs::read_symlink(source, ec);
        if (!ec)
        {
            fs::create_symlink(link_target, target, ec);
            if (ec) state.add_error(ec, target);
        }
        else state.add_error(ec, source);
    }
    else
    {
        auto is_dir = fs::is_directory(status);
        if (is_link)
        {
            is_dir = fs::is_directory(source, ec);
            if (ec) { state.add_error(ec, source); return; }
        }

        if (is_dir)
        {
            if (!options.recursive)
            {
                state.add_error("Skipping directory", source);
                return;
            }

            fs::create_directory(target, ec);
            if (ec) { state.add_error(ec, target); return; }

            for (auto&& child : walk_dir(options, state, source))
            {
                if (state.quit.load(std::memory_order_relaxed)) break;

                auto child_target = target / child.path().lexically_relative(source);

                std::error_code ec;
                auto is_link = child.is_symlink(ec);
                if (ec) { state.add_error(ec, child.path()); continue; }

                if (is_link && options.keep_links)
                {
                    auto link_target = fs::read_symlink(child.path(), ec);
                    if (!ec)
                    {
                        fs::create_symlink(link_target, child_target, ec);
                        if (ec) state.add_error(ec, child_target);
                    }
                    else state.add_error(ec, child.path());
                }
                else
                {
                    auto is_dir = child.is_directory(ec);
                    if (ec) { state.add_error(ec, child.path()); continue; }

                    if (is_dir)
                    {
                        fs::create_directory(child_target, ec);
                        if (ec) state.add_error(ec, child_target);
                    }
                    else post_copy_file(options, state, pool, child.path(), child_target);
                }
            }
        }
        else post_copy_file(options, state, pool, source, target);
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
