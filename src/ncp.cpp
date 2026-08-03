////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "pgm/args.hpp"
#include "state.hpp"

#include <asio.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

void show_usage(const pgm::args& args, std::string_view name);
void show_version(std::string_view name);

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
        state state;

        asio::io_context io;
        asio::signal_set sigset{ io, SIGINT, SIGTERM };
        sigset.async_wait([&](auto&& ec, int signal)
        {
            if (!ec)
            {
                std::cerr << "Received signal " << signal << ", exiting..." << std::endl;
                state.quit = true;
            }
        });
    }

    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
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
