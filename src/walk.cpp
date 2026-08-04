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

void copy_task(state& state, const options& options, const fs::path&, const fs::path&);

////////////////////////////////////////////////////////////////////////////////
inline void throw_filesystem_error(std::errc cond)
{
    throw fs::filesystem_error{"walk", std::make_error_code(cond)};
}
inline void throw_filesystem_error(std::errc cond, const fs::path& path)
{
    throw fs::filesystem_error{"walk", path, std::make_error_code(cond)};
}

std::generator<fs::directory_entry> walk(const fs::path& dir)
{
    for (auto& entry : fs::directory_iterator(dir))
    {
        co_yield entry;
        if (entry.is_directory()) co_yield std::ranges::elements_of( walk(entry.path()) );
    }
}

void post_copy_task(state& state, const options& options, asio::thread_pool& pool, fs::path source, fs::path target)
{
    asio::post(pool, [&state, &options, source = std::move(source), target = std::move(target)]{
        copy_task(state, options, source, target);
    });
}

////////////////////////////////////////////////////////////////////////////////
void walk_dir(state& state, const options& options, asio::thread_pool& pool, const fs::path& source, const fs::path& target)
{
    fs::create_directory(target);

    for (auto&& entry : walk(source))
    {
        if (state.quit.load(std::memory_order_relaxed)) break;
        auto target_path = target / fs::relative(entry.path(), source);

        if (entry.is_directory()) fs::create_directory(target_path);
        else post_copy_task(state, options, pool, entry.path(), target_path);
    }
}

void walk_task(state& state, const options& options, asio::thread_pool& pool)
{
    if (fs::is_directory(options.target))
    {
        for (auto&& source : options.sources)
        {
            if (state.quit.load(std::memory_order_relaxed)) break;
            if (!fs::exists(source)) throw_filesystem_error(std::errc::no_such_file_or_directory, source);

            if (fs::is_directory(source))
            {
                auto trailing_slash = !source.has_filename();
                auto target = trailing_slash ? options.target : options.target / source.filename();

                walk_dir(state, options, pool, source, target);
            }
            else
            {
                auto target = options.target / source.filename();
                post_copy_task(state, options, pool, source, target);
            }
        }
    }
    else
    {
        if (options.sources.size() > 1) throw_filesystem_error(std::errc::not_a_directory, options.target);

        auto&& source = options.sources.front();
        auto&& target = options.target;

        if (!fs::exists(source)) throw_filesystem_error(std::errc::no_such_file_or_directory, source);

        if (fs::is_directory(source)) walk_dir(state, options, pool, source, target);
        else post_copy_task(state, options, pool, source, target);
    }
}
