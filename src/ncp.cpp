////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "error.hpp"
#include "options.hpp"
#include "pgm/args.hpp"
#include "signal.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include <ranges>
#include <string_view>

namespace fs = std::filesystem;

void show_usage(const pgm::args& args, std::string_view name);
void show_version(std::string_view name);

void walk_task(state&, const options&, asio::thread_pool&);

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
try
{
    auto name = fs::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-h", "--help",           "Show this help screen and exit" },
        { "-t", "--target", "DIR",  "Directory to copy/move into" },
        { "-v", "--version",        "Show program version and exit" },

        { "SOURCE", pgm::mul,       "Files/directories to copy or move" },
        { "DESTINATION", pgm::opt,  "Destination file or directory" },
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

        asio::thread_pool pool{1};
        state state;

        signal_set sigset{ {SIGINT, SIGTERM}, [&](int signal)
        {
            std::print(stderr, "Received signal {}, exiting...\n", signal);
            state.quit = true;
        }};

        walk_task(state, options, pool);

        pool.join();
    }

    return 0;
}
catch (const fs::filesystem_error& e)
{
    print_error(e);
    return 1;
}
catch (const std::exception& e)
{
    print_error(e);
    return 1;
};

////////////////////////////////////////////////////////////////////////////////
void show_usage(const pgm::args& args, std::string_view name)
{
    auto preamble = R"(
ncp – new and improved, now asbestos-free copy utility.)";

    std::cout << args.usage(name, preamble) << std::endl;
}

void show_version(std::string_view name)
{
    std::cout << name << " version " << VERSION << std::endl;
}
