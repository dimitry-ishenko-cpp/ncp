////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "options.hpp"
#include "pgm/args.hpp"
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

void walk_all(const options&, state&, asio::thread_pool&);

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
try
{
    int exit_code = 0;
    auto name = fs::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-h", "--help",           "Show this help screen and exit."   },
        { "-L", "--follow-links",   "Dereference symbolic links."       },
        { "-P", "--keep-links",     "Preserve symbolic links."          },
        { "-r", "--recursive",      "Copy directories recursively."     },
        { "-t", "--target", "dir",  "Directory to copy/move into."      },
        { "-v", "--version",        "Show program version and exit."    },

        { "SOURCE", pgm::mul,       "Files/directories to copy or move."},
        { "DESTINATION", pgm::opt,  "Destination file or directory."    },
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
        options.sources = std::ranges::to< decltype(options.sources) >(sources.values());

        auto&& destination = args["DESTINATION"];
        auto&& target = args["--target"];
        if (target)
        {
            options.target = target.value();
            if (fs::is_directory(options.target))
            {
                // DESTINATION will capture the last positional parameter,
                // but if --target was specified that value belongs in SOURCES
                if (destination) options.sources.push_back(destination.value());
            }
            else throw fs::filesystem_error{"main",
                options.target, std::make_error_code(std::errc::not_a_directory)
            };
        }
        else
        {
            if (destination) options.target = destination.value();
            else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};
        }

        options.recursive = !!args["--recursive"];
        // keep symlinks in recursive mode by default
        options.keep_links = options.recursive;

        auto&& follow_links = args["--follow-links"];
        auto&& keep_links = args["--keep-links"];

        if (follow_links && keep_links)
            throw pgm::invalid_argument{"'--follow-links' and '--keep-links' are mutually exclusive"};

        if (follow_links) options.keep_links = false;
        else if (keep_links) options.keep_links = true;

        ////////////////////
        asio::thread_pool pool{1};
        state state;

        state_ptr = &state;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        walk_all(options, state, pool);

        pool.join();

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
