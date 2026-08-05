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
#include <print>
#include <ranges>
#include <string_view>

namespace fs = std::filesystem;

void show_usage(const pgm::args& args, std::string_view name);
void show_version(std::string_view name);

void walk_all(const options&, state&, asio::thread_pool&);

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
int main(int argc, char* argv[])
try
{
    auto name = fs::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "--follow-links", "when", "Specify when to follow symbolic links.\n"
                                    "Can be one of 'never', 'always' or 'files'.\n"
                                    "Default: 'never' if -r or --recursive was specified,\n"
                                    "and 'always' otherwise."           },
        { "-h", "--help",           "Show this help screen and exit."   },
        { "-L",                     "Same as --follow-links=always."    },
        { "-P",                     "Same as --follow-links=never."     },
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
            else throw pgm::invalid_argument{"target '" + options.target.string() + "' is not a directory"};
        }
        else
        {
            if (destination) options.target = destination.value();
            else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};
        }

        options.recursive = !!args["--recursive"];
        // create symlinks in recursive mode by default
        options.symlink_files = options.symlink_other = options.recursive;

        auto&& follow_links = args["--follow-links"];
        auto&& L = args["-L"];
        auto&& P = args["-P"];

        if ( (follow_links && P) || (follow_links && L) || (L && P) )
            throw pgm::invalid_argument{"'--follow-links', '-L' and '-P' cannot be combined"};

        if (follow_links)
        {
            auto when = follow_links.value();
            if (when == "never" )
                options.symlink_files = true,  options.symlink_other = true;
            else if (when == "always")
                options.symlink_files = false, options.symlink_other = false;
            else if (when == "files" )
                options.symlink_files = false, options.symlink_files = true;
            else throw pgm::invalid_argument{"bad '--follow-links' value"};
        }
        else if (L) options.symlink_files = false, options.symlink_other = false;
        else if (P) options.symlink_files = true,  options.symlink_other = true;

        ////////////////////
        asio::thread_pool pool{1};
        state state;

        state_ptr = &state;
        std::signal(SIGINT, &signal_handler);
        std::signal(SIGTERM, &signal_handler);

        walk_all(options, state, pool);

        pool.join();
    }

    return 0;
}
catch (const fs::filesystem_error& e)
{
    if (e.path1().empty()) std::print("{}\n", e.code().message());
    else std::print("{}: '{}'\n", e.code().message(), e.path1().string());
    return 1;
}
catch (const std::exception& e)
{
    std::print("{}\n", e.what());
    return 1;
};

////////////////////////////////////////////////////////////////////////////////
void show_usage(const pgm::args& args, std::string_view name)
{
    auto preamble = R"(
ncp – new and improved, now asbestos-free copy utility.)";

    std::print("{}\n", args.usage(name, preamble));
}

void show_version(std::string_view name)
{
    std::print("{} version {}\n", name, VERSION);
}
