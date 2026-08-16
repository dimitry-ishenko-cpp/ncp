////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "args.hpp"
#include "context.hpp"
#include "file.hpp"

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

context* pctx = nullptr;
extern "C" void signal_handler(int signal)
{
    if (pctx)
    {
        pctx->signal = signal;
        pctx->quit = true;
    }
}

////////////////////////////////////////////////////////////////////////////////
bool should_copy(context& ctx, const io::file& source, const io::file& target)
{
    if (target.exists())
        switch (ctx.update_)
        {
            case update::all:     return true;
            case update::none:    return false;
            case update::older:   return source.time() >  target.time();
            case update::changed: return source.time() != target.time() || source.size() != target.size();
            case update::size:    return source.size() != target.size();
        }

    return true;
}

void copy_entry(context& ctx, asio::thread_pool& pool, io::file source, io::file target, std::error_code& ec)
{
    if (source.is_directory())
    {
        io::attrib attr;
        if (ctx.keep_group) attr.gid = source.gid();
        if (ctx.keep_mode ) attr.mode= source.mode();
        if (ctx.keep_user ) attr.uid = source.uid();

        io::create_directory(target.path(), attr, ec);
        if (ec) { ctx.add_error(ec, target.path()); return; }

        if (ctx.keep_time) ctx.dir_times.emplace_back(target.path(), source.time());
    }
    ////////////////////
    else if (source.is_symlink())
    {
        auto link_target = source.get_target_path(ec);
        if (!ec)
        {
            io::attrib attr;
            if (ctx.keep_group) attr.gid = source.gid();
            if (ctx.keep_time ) attr.time= source.time();
            if (ctx.keep_user ) attr.uid = source.uid();

            io::create_symlink(link_target, target.path(), attr, ec);
            if (ec) ctx.add_error(ec, target.path(), link_target);
        }
        else ctx.add_error(ec, source.path());
    }
    ////////////////////
    else if (source.is_regular_file())
    {
        if (should_copy(ctx, source, target))
        {
            ctx.files_total.fetch_add(1, std::memory_order_relaxed);
            ctx.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

            asio::post(pool, [&ctx, source = std::move(source), target = std::move(target)]
            {
                if (ctx.quit.load(std::memory_order_relaxed)) return;

                io::attrib attr;
                if (ctx.keep_group) attr.gid = source.gid();
                if (ctx.keep_mode ) attr.mode= source.mode();
                if (ctx.keep_time ) attr.time= source.time();
                if (ctx.keep_user ) attr.uid = source.uid();

                std::error_code ec;
                io::copy_file(source, target, attr, ec);
                if (ec) { ctx.add_error(ec, source.path(), target.path()); return; }

                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                ctx.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
            });
        }
        ec.clear();
    }
    ////////////////////
    else if (source.is_device())
    {
        if (ctx.keep_devices)
        {
            io::attrib attr;
            if (ctx.keep_group) attr.gid = source.gid();
            if (ctx.keep_mode ) attr.mode= source.mode();
            if (ctx.keep_time ) attr.time= source.time();
            if (ctx.keep_user ) attr.uid = source.uid();

            if (source.is_block_device()) io::create_block_device(target.path(), source.dev_type(), attr, ec);
            else io::create_char_device(target.path(), source.dev_type(), attr, ec);

            if (ec) ctx.add_error(ec, target.path());
        }
        else ctx.add_error("Skipping device file", source.path());
    }
    ////////////////////
    else if (source.is_special())
    {
        if (ctx.keep_special)
        {
            io::attrib attr;
            if (ctx.keep_group) attr.gid = source.gid();
            if (ctx.keep_mode ) attr.mode= source.mode();
            if (ctx.keep_time ) attr.time= source.time();
            if (ctx.keep_user ) attr.uid = source.uid();

            if (source.is_fifo()) io::create_fifo(target.path(), attr, ec);
            else io::create_socket(target.path(), attr, ec);

            if (ec) ctx.add_error(ec, target.path());
        }
        else ctx.add_error("Skipping special file", source.path());
    }
    ////////////////////
    else if (!source.exists())
    {
        ctx.add_error("Source does not exist", source.path());
        ec = std::make_error_code(std::errc::no_such_file_or_directory);
    }
    else
    {
        ctx.add_error("Skipping unknown file", source.path());
        ec = std::make_error_code(std::errc::not_supported);
    }
}

////////////////////////////////////////////////////////////////////////////////
std::generator<io::file> walk_tree(context& ctx, const io::file& dir)
{
    for (auto&& expected_path : io::directory_iterator(dir.path()))
        if (expected_path)
        {
            std::error_code ec;
            io::file child{*expected_path, ec};
            if (ec) { ctx.add_error(ec, *expected_path); continue; }

            if (child.is_symlink() && !ctx.keep_links)
            {
                child = child.follow_symlinks(ec);
                if (ec) { ctx.add_error(ec, child.path()); continue; }
            }

            co_yield child;

            if (child.is_directory())
                co_yield std::ranges::elements_of( walk_tree(ctx, child) );
        }
        else ctx.add_error(expected_path.error());
}

void copy_source(context& ctx, asio::thread_pool& pool, io::file source, io::file target)
{
    if (source.is_symlink() && !ctx.keep_links)
    {
        std::error_code ec;
        source = source.follow_symlinks(ec);
        if (ec) { ctx.add_error(ec, source.path()); return; }
    }

    if (source.is_directory() && !ctx.recursive)
    {
        ctx.add_error("Skipping directory", source.path());
        return;
    }

    std::error_code ec;
    copy_entry(ctx, pool, source, target, ec);

    if (!ec && source.is_directory())
        for (auto&& source_child : walk_tree(ctx, source))
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            auto name = source_child.path().lexically_relative(source.path());
            io::file target_child{ target.path() / name, ec };

            if (ec) ctx.add_error(ec, target_child.path());
            else copy_entry(ctx, pool, std::move(source_child), std::move(target_child), ec);
        }
}

void copy_sources(context& ctx, asio::thread_pool& pool, std::vector<io::file> sources, io::file target)
{
    if (target.is_directory())
    {
        for (auto&& source : sources)
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            auto target_ = target;
            if (source.path().has_filename()) // trailing_slash
            {
                std::error_code ec;
                target_ = io::file{target.path() / source.path().filename(), io::follow_symlinks, ec};
                if (ec) throw io::exception{"walk_all", target_.path(), ec};
            }

            copy_source(ctx, pool, std::move(source), std::move(target_));
        }
    }
    else if (sources.size() == 1)
    {
        copy_source(ctx, pool, std::move(sources.front()), std::move(target));
    }
    else throw io::exception{"walk_all",
        target.path(), std::make_error_code(std::errc::not_a_directory)
    };
}

void update_dirs(context& ctx)
{
    for (auto&& [path, time] : ctx.dir_times)
    {
        std::error_code ec;
        io::modify(path, io::attrib{.time = time}, ec);
        if (ec) ctx.add_error(ec, path);
    }
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

void print_status(context& ctx)
{
    static bool overwrite = false;

    if (overwrite) std::print("\033[{}F\033[K", 1);
    else overwrite = true;

    if (int signal = ctx.signal.exchange(0))
        std::print("Received signal {}, exiting...\n", signal);

    for (auto&& error : ctx.drain_errors()) std::print("{}\n", error);

    auto files_total  = ctx.files_total.load(std::memory_order_relaxed);
    auto files_copied = ctx.files_copied.load(std::memory_order_relaxed);
    auto bytes_total  = ctx.bytes_total.load(std::memory_order_relaxed);
    auto bytes_copied = ctx.bytes_copied.load(std::memory_order_relaxed);

    long percent_copied = bytes_total ? (100.0 * bytes_copied / bytes_total) : 0;

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

void report_status(context& ctx)
{
    do
    {
        std::this_thread::sleep_for(100ms);
        print_status(ctx);
    }
    while (!ctx.quit.load(std::memory_order_relaxed));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
try
{
    int exit_code = 0;
    auto name = io::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-D",                     "Same as --special --devices."              },
        {       "--devices",        "Preserve device files."                    },
        { "-g", "--group",          "Preserve group ownership."                 },
        { "-h", "--help",           "Show this help message and exit."          },
        { "-L", "--follow-links",   "Dereference symbolic links (default when non-recursive)." },
        { "-m", "--mode",           "Preserve file permissions (mode bits)."    },
        { "-o", "--ownership",      "Same as --user --group."                   },
        { "-P", "--keep-links",     "Preserve symbolic links (default when recursive)." },
        { "-r", "--recursive",      "Copy directories recursively."             },
        {       "--special",        "Preserve named pipes and sockets."         },
        { "-T", "--target", "dir",  "Target directory to copy into."            },
        { "-t", "--time",           "Preserve modification time."               },
        { "-u", "--user",           "Preserve user ownership."                  },
        { "-U", "--update", "when", pgm::optval,
                                    "Update existing files. [when] can be one of:\n"
                                    "'all', 'none', 'older', 'changed' (size or time) or 'size'.\n"
                                    "If [when] is omitted, 'older' is assumed.\n"
                                    "If the option is omitted entirely, all files are updated,\n"
                                    "which is equivalent to --update=all."      },
        { "-V", "--version",        "Show program version and exit."            },

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
        context ctx;

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

        if (args["-D"]) ctx.keep_devices = ctx.keep_special = true;
        if (args["--devices"]) ctx.keep_devices = true;
        if (args["--group"]) ctx.keep_group = true;

        if (args["--recursive"]) ctx.recursive = true;
        // keep symlinks in recursive mode by default
        ctx.keep_links = ctx.recursive;

        auto&& follow_links = args["--follow-links"];
        auto&& keep_links = args["--keep-links"];

        if (follow_links && keep_links) throw pgm::invalid_argument{
            "'--follow-links' and '--keep-links' are mutually exclusive"
        };

        if (follow_links) ctx.keep_links = false;
        else if (keep_links) ctx.keep_links = true;

        if (args["--mode"]) ctx.keep_mode = true;
        if (args["--ownership"]) ctx.keep_group = ctx.keep_user = true;
        if (args["--special"]) ctx.keep_special = true;
        if (args["--time"]) ctx.keep_time = true;
        if (args["--user"]) ctx.keep_user = true;

        if (auto&& update = args["--update"])
        {
            auto&& when = update.value();
            if (when == "all") ctx.update_ = update::all;
            else if (when == "none") ctx.update_ = update::none;
            else if (when.empty() || when == "older") ctx.update_ = update::older;
            else if (when == "changed") ctx.update_ = update::changed;
            else if (when == "size") ctx.update_ = update::size;
            else throw pgm::invalid_argument{ "bad --update value '" + when + "'" };
        }

        ////////////////////
        asio::thread_pool pool{ ctx.jobs };

        pctx = &ctx;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        auto status_task = std::async(std::launch::async, [&]{ report_status(ctx); });

        copy_sources(ctx, pool, std::move(sources), std::move(target));
        pool.join();
        update_dirs(ctx);

        ctx.quit = true;
        status_task.wait();
        print_status(ctx); // final status

        exit_code = ctx.get_error_count() ? 2 : 0;
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
