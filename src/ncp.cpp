////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "args.hpp"
#include "options.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <csignal>
#include <exception>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>
#include <string>

namespace fs = std::filesystem;

void show_usage(const pgm::args& args, const std::string& name);
void show_version(const std::string& name);

state* state_ptr = nullptr;
extern "C" void signal_handler(int signal)
{
    if (state_ptr)
    {
        state_ptr->add_error(std::format("Received signal {}, exiting...", signal));
        state_ptr->quit = true;
    }
}

void report_one(state&, bool overwrite = true);
void report(state&);

void walk_all(const options&, state&, asio::thread_pool&);

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
try
{
    int exit_code = 0;
    auto name = fs::path{argv[0]}.filename().string();

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

        auto&& sources = args["SOURCE"];
        options.source_paths = std::ranges::to< decltype(options.source_paths) >(sources.values());

        auto&& destination = args["DESTINATION"];
        auto&& target = args["--target"];
        if (target)
        {
            options.target_path = target.value();
            if (fs::is_directory(options.target_path))
            {
                // DESTINATION will capture the last positional parameter,
                // but if --target was specified that value belongs in SOURCES
                if (destination) options.source_paths.push_back(destination.value());
            }
            else throw fs::filesystem_error{"main",
                options.target_path, std::make_error_code(std::errc::not_a_directory)
            };
        }
        else
        {
            if (destination) options.target_path = destination.value();
            else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};
        }

        if (args["--group"]) options.keep_group = true;

        if (args["--recursive"]) options.recursive = true;
        // keep symlinks in recursive mode by default
        options.keep_links = options.recursive;

        auto&& follow_links = args["--follow-links"];
        auto&& keep_links = args["--keep-links"];

        if (follow_links && keep_links)
            throw pgm::invalid_argument{"'--follow-links' and '--keep-links' are mutually exclusive"};

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
        walk_all(options, state, pool);

        pool.join();

        state.quit = true;
        report_task.wait();

        // final report
        report_one(state);
        exit_code = state.get_error_count() ? 2 : 0;
    }

    return exit_code;
}
catch (const fs::filesystem_error& e)
{
    std::print("{}: '{}'\n", e.code().message(), e.path1().string());
    return 1;
}
catch (const std::exception& e)
{
    std::print("{}\n", e.what());
    return 1;
};

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
