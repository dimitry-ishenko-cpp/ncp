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

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
void post_copy_file(const options& options, state& state, asio::thread_pool& pool, const path& source, const path& target)
{
    auto size = ::file_size(source);
    if (!size.has_value()) { state.add_error(size.error()); return; }

    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(size.value(), std::memory_order_relaxed);

    asio::post(pool, [&options, &state, source, target]
    {
        if (state.quit.load(std::memory_order_relaxed)) return;

        auto res = copy_file(source, target, options);
        if (!res) { state.add_error(res.error()); return; }

        auto size = ::file_size(source);
        if (!size.has_value()) { state.add_error(size.error()); return; }

        state.files_copied.fetch_add(1, std::memory_order_relaxed);
        state.bytes_copied.fetch_add(size.value(), std::memory_order_relaxed);
    });
}

std::generator<fs::directory_entry> walk_dir(const options& options, state& state, path dir)
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

void walk_one(const options& options, state& state, asio::thread_pool& pool, const path& source, const path& target)
{
    auto stat = ::symlink_status(source);
    if (!stat.has_value()) { state.add_error(stat.error()); return; }

    auto is_link = ::is_symlink(stat.value());
    if (is_link && options.keep_links)
    {
        auto res = ::read_symlink(source)
            .and_then([&](auto&& link_target) {
                return create_symlink(link_target, target, options);
            });
        if (!res) state.add_error(res.error());
    }
    else
    {
        auto is_dir = ::is_directory(*stat);
        if (is_link)
        {
            auto res = ::is_directory(source);
            if (!res) { state.add_error(res.error()); return; }
            is_dir = res.value();
        }

        if (is_dir)
        {
            if (!options.recursive)
            {
                state.add_error("Skipping directory", source);
                return;
            }

            auto res = create_directory(target, options);
            if (!res) { state.add_error(res.error()); return; }

            for (auto&& child : walk_dir(options, state, source))
            {
                if (state.quit.load(std::memory_order_relaxed)) break;

                auto child_target = target / child.path().lexically_relative(source);

                auto is_link = is_symlink(child);
                if (!is_link.has_value()) { state.add_error(is_link.error()); continue; }

                if (is_link.value() && options.keep_links)
                {
                    auto res = ::read_symlink(child.path())
                        .and_then([&](auto&& link_target) {
                            return create_symlink(link_target, child_target, options);
                        });
                    if (!res) state.add_error(res.error());
                }
                else
                {
                    auto is_dir = is_directory(child);
                    if (!is_dir.has_value()) { state.add_error(is_dir.error()); continue; }

                    if (is_dir.value())
                    {
                        auto res = create_directory(child_target, options);
                        if (!res) state.add_error(res.error());
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
    auto is_dir = ::is_directory(options.target);
    if (!is_dir.has_value()) { state.add_error(is_dir.error()); return; }

    if (is_dir.value())
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
