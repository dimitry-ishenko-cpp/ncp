////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "args.hpp"
#include "file.hpp"
#include "options.hpp"
#include "state.hpp"

#include <array>
#include <asio.hpp>
#include <csignal>
#include <exception>
#include <format>
#include <generator>
#include <print>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
void show_usage(const pgm::args& args, const std::string& name)
{
    auto preamble = R"(
ncp – new and improved, now asbestos-free copy utility.)";

    std::print("{}\n", args.usage(name, preamble));
}

void show_version(const std::string& name)
{
    std::print("{} version {}\n", name, VERSION);
}

state* state_ptr = nullptr;
extern "C" void signal_handler(int signal)
{
    if (state_ptr)
    {
        state_ptr->add_error(std::format("Received signal {}, exiting...", signal));
        state_ptr->quit = true;
    }
}

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

        io::attrib attr;
        if (options.keep_group) attr.gid = source.gid();
        if (options.keep_mode ) attr.mode= source.mode();
        if (options.keep_user ) attr.uid = source.uid();

        std::error_code ec;
        io::copy_file(source, target, attr, ec);
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

////////////////////////////////////////////////////////////////////////////////
auto human(long bytes)
{
    constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};
    
    auto n = 0;
    auto dbl_bytes = static_cast<double>(bytes);
    for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

    return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
}

void report_one(state& state, bool overwrite = true)
{
    if (overwrite) std::print("\033[{}F\033[K", 1);

    auto errors = state.drain_errors();
    for (auto&& error : errors) print("{}\n", error);

    auto files_total  = state.files_total.load(std::memory_order_relaxed);
    auto files_copied = state.files_copied.load(std::memory_order_relaxed);
    auto bytes_total  = state.bytes_total.load(std::memory_order_relaxed);
    auto bytes_copied = state.bytes_copied.load(std::memory_order_relaxed);

    auto percent_copied = bytes_total ? (100 * bytes_copied / bytes_total) : 0;

    constexpr auto bar_width = 40;
    auto bar_fill = percent_copied * bar_width / 100;
    std::string bar;
    for (auto n = 0; n < bar_fill; ++n) bar += "█";
    for (auto n = bar_fill; n < bar_width; ++n) bar += "░";

    std::print(" {:>3}% {} {}/{} ● {}/{}\n",
        percent_copied, bar, files_copied, files_total, human(bytes_copied), human(bytes_total)
    );
    std::fflush(stdout);
}

void report(state& state)
{
    bool overwrite = false;
    do
    {
        std::this_thread::sleep_for(100ms);

        report_one(state, overwrite);
        overwrite = true;
    }
    while (!state.quit.load(std::memory_order_relaxed));
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
try
{
    int exit_code = 0;
    auto name = io::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-g", "--group",          "Preserve group ownership."                 },
        { "-h", "--help",           "Show this help message and exit."          },
        { "-L", "--follow-links",   "Dereference symbolic links (default in non-recursive mode)." },
        { "-m", "--mode",           "Preserve file permissions (ie, mode bits)."},
        { "-o", "--ownership",      "Preserve user and group ownership (same as -ug)." },
        { "-P", "--keep-links",     "Preserve symbolic links (default in recursive mode)." },
        { "-r", "--recursive",      "Copy directories recursively."             },
        { "-t", "--target", "dir",  "Target directory to copy/move into."       },
        { "-u", "--user",           "Preserve user ownership."                  },
        { "-v", "--version",        "Show program version and exit."            },

        { "SOURCE", pgm::mul,       "Files or directories to copy or move."     },
        { "DESTINATION", pgm::opt,  "Destination file or directory."            },
    };

    std::exception_ptr ep;
    try { args.parse(argc, argv); }
    catch (...) { ep = std::current_exception(); }

    if (args["--help"])
        show_usage(args, name);

    else if (args["--version"])
        show_version(name);

    else if (ep)
        std::rethrow_exception(ep);

    else
    {
        options options;

        std::error_code ec;
        std::vector<io::file> sources;
        io::file target;

        for (auto&& path : args["SOURCE"].values()) sources.emplace_back(path, ec);

        if (args["DESTINATION"])
        {
            target = io::file{args["DESTINATION"].value(), ec};
            if (ec) throw io::exception{"main", target.path(), ec};
        }

        if (args["--target"])
        {
            // DESTINATION will capture the last positional parameter,
            // but if --target was specified that value belongs in SOURCES
            if (target) sources.push_back(std::move(target));

            target = io::file{args["--target"].value(), io::follow_symlinks, ec};
            if (ec) throw io::exception{"main", target.path(), ec};

            if (!target.is_directory()) throw io::exception{
                "main", target.path(), std::make_error_code(std::errc::not_a_directory)
            };
        }
        else
        {
            if (!target) throw pgm::missing_argument{
                "neither DESTINATION nor --target was specified"
            };

            target = target.follow_symlinks(ec);
            if (ec) throw io::exception{"main", target.path(), ec};
        }

        if (args["--group"]) options.keep_group = true;

        if (args["--recursive"]) options.recursive = true;
        // keep symlinks in recursive mode by default
        options.keep_links = options.recursive;

        auto&& follow_links = args["--follow-links"];
        auto&& keep_links = args["--keep-links"];

        if (follow_links && keep_links) throw pgm::invalid_argument{
            "'--follow-links' and '--keep-links' are mutually exclusive"
        };

        if (follow_links) options.keep_links = false;
        else if (keep_links) options.keep_links = true;

        if (args["--mode"]) options.keep_mode = true;
        if (args["--ownership"]) options.keep_group = options.keep_user = true;
        if (args["--user"]) options.keep_user = true;

        ////////////////////
        asio::thread_pool pool{1};
        state state;

        state_ptr = &state;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        auto report_task = std::async(std::launch::async, report, std::ref(state));
        walk_all(options, state, pool, sources, target);

        pool.join();

        state.quit = true;
        report_task.wait();

        // final report
        report_one(state);
        exit_code = state.get_error_count() ? 2 : 0;
    }

    return exit_code;
}
catch (const io::exception& e)
{
    std::print("{}\n", e);
    return 1;
}
catch (const std::exception& e)
{
    std::print("{}\n", e.what());
    return 1;
};
