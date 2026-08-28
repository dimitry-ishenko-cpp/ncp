////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "args.hpp"
#include "context.hpp"
#include "file.hpp"

#include <asio.hpp>
#include <charconv> // std::from_chars
#include <csignal>
#include <exception>
#include <generator>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
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

std::optional<int> parse(std::string_view text)
{
    int n;
    auto from = text.data(), to = from + text.size();
    auto [p, ec] = std::from_chars(from, to, n);
    if (ec == std::errc{} && p == to) return n; else return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
enum attr_option { include_all, exclude_mode, exclude_time };
auto get_attr(context& ctx, const io::file& source, attr_option option)
{
    io::attrib attr;
    if (ctx.keep_group) attr.gid = source.gid();
    if (ctx.keep_mode && option != exclude_mode) attr.mode= source.mode();
    if (ctx.keep_time && option != exclude_time) attr.time= source.time();
    if (ctx.keep_user ) attr.uid = source.uid();
    return attr;
}

enum class status { failed, copied, unchanged, skipped };

inline auto fail(context& ctx, auto&&... args)
{
    ctx.add_error(std::forward<decltype (args)>(args)...);
    return status::failed;
}

inline auto good(status status) {
    return status == status::copied || status == status::unchanged;
}

auto copy_regular_file(context& ctx, asio::thread_pool& pool, io::file source, io::file target)
{
    if (target.exists() && ctx.update_ == update::none) return status::unchanged;

    bool is_match = false;
    if (target.is_regular_file()) switch (ctx.update_)
    {
        case update::older:   is_match = target.time() >= source.time(); break;
        case update::size:    is_match = target.size() == source.size(); break;
        case update::changed: is_match = target.size() == source.size() && target.time() == source.time(); break;
        default:              is_match = false; break;
    }

    std::error_code ec;

    bool need_create = !target.exists() || !is_match || ctx.unlink_ == unlink::always;
    if (target.exists() && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "Not replacing existing file", target.path());

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, ec, target.path());
    }

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);
    ctx.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

    if (need_create)
    {
        asio::post(pool, [&ctx, source = std::move(source), target = std::move(target)]
        {
            if (ctx.quit.load(std::memory_order_relaxed)) return;

            std::error_code ec;
            auto attr = get_attr(ctx, source, include_all);
            io::copy_file(source, target, attr, ec,
                [&ctx](io::file_size chunk)
                {
                    ctx.bytes_copied.fetch_add(chunk, std::memory_order_relaxed);
                    return !ctx.quit.load(std::memory_order_relaxed);
                });

            if (ec) fail(ctx, ec, source.path(), target.path());
            else ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        });
    }
    else
    {
        auto attr = get_attr(ctx, source, include_all);
        io::modify(target.path(), attr, ec);
        if (ec) return fail(ctx, ec, target.path());
        
        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        ctx.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
    }

    return status::copied;
}

auto copy_directory(context& ctx, io::file source, io::file target)
{
    bool need_create = !target.exists() || !target.is_directory();
    if (!need_create && ctx.update_ == update::none) return status::unchanged;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    std::error_code ec;
    if (target.exists() && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "Not replacing existing non-directory", target.path());

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, ec, target.path());
    }

    if (need_create)
    {
        io::create_directory(target.path(), ec);
        if (ec) return fail(ctx, ec, target.path());
    }

    auto attr = get_attr(ctx, source, include_all);
    ctx.add_dir_attr(target.path(), attr);

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    return status::copied;
}

template <typename MatchFn, typename CreateFn>
auto copy_generic(context& ctx, io::file source, io::file target, attr_option option,
    MatchFn&& is_match, CreateFn&& create)
{
    if (target.exists() && ctx.update_ == update::none) return status::unchanged;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    std::error_code ec;
    bool need_create = !target.exists() || !is_match(source, target) || ctx.unlink_ == unlink::always;
    if (target.exists() && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "Not replacing existing file", target.path());

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, ec, target.path());
    }

    auto attr = get_attr(ctx, source, option);
    if (need_create) create(source, target, attr, ec);
    else io::modify(target.path(), attr, ec);

    if (ec) return fail(ctx, ec, target.path());

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    return status::copied;
}

auto copy_symlink(context& ctx, io::file source, io::file target)
{
    std::error_code ec;
    auto link_target = source.get_target_path(ec);
    if (ec) return fail(ctx, ec, source.path());

    return copy_generic(ctx, std::move(source), std::move(target), exclude_mode,
        [&link_target](auto&& src, auto&& tgt) {
            std::error_code ec;
            return tgt.get_target_path(ec) == link_target;
        },
        [&link_target](auto&& src, auto&& tgt, auto&& attr, std::error_code& ec) {
            io::create_symlink(link_target, tgt.path(), attr, ec);
        });
}

auto copy_device(context& ctx, io::file source, io::file target)
{
    if (!ctx.keep_devices) 
        return fail(ctx, "Skipping device file", source.path()); 

    return copy_generic(ctx, std::move(source), std::move(target), include_all,
        [](auto&& src, auto&& tgt) { 
            return tgt.type() == src.type() && tgt.dev_type() == src.dev_type(); 
        },
        [](auto&& src, auto&& tgt, auto&& attr, std::error_code& ec) {
            if (src.is_block_device())
                io::create_block_device(tgt.path(), src.dev_type(), attr, ec);
            else io::create_char_device(tgt.path(), src.dev_type(), attr, ec);
        }
    );
}

auto copy_special(context& ctx, io::file source, io::file target)
{
    if (!ctx.keep_special) 
        return fail(ctx, "Skipping special file", source.path()); 

    return copy_generic(ctx, std::move(source), std::move(target), include_all,
        [](auto&& src, auto&& tgt) {
            return tgt.type() == src.type();
        },
        [](auto&& src, auto&& tgt, auto&& attr, std::error_code& ec) {
            if (src.is_fifo())
                io::create_fifo(tgt.path(), attr, ec);
            else io::create_socket(tgt.path(), attr, ec);
        });
}

auto copy_entry(context& ctx, asio::thread_pool& pool, io::file source, io::file target, bool from_walk)
{
    if (ctx.follow_dest_links && target.is_symlink() && !source.is_symlink())
    {
        std::error_code ec;
        target = target.follow_symlinks(ec);
        if (ec) return fail(ctx, ec, target.path());
    }

    if (target == source)
        return fail(ctx, "Skipping same file", source.path(), target.path());

    switch (source.type())
    {
        case io::file_type::regular:
            return copy_regular_file(ctx, pool, std::move(source), std::move(target));

        case io::file_type::directory:
            return copy_directory(ctx, std::move(source), std::move(target));

        case io::file_type::symlink:
            return copy_symlink(ctx, std::move(source), std::move(target));

        case io::file_type::block:
        case io::file_type::character:
            if (from_walk) return copy_device(ctx, std::move(source), std::move(target));
            else return copy_regular_file(ctx, pool, std::move(source), std::move(target));

        case io::file_type::fifo:
            if (from_walk) return copy_special(ctx, std::move(source), std::move(target));
            else return copy_regular_file(ctx, pool, std::move(source), std::move(target));

        case io::file_type::socket:
            if (from_walk) return copy_special(ctx, std::move(source), std::move(target));
            else return fail(ctx, "Cannot copy from socket", source.path());

        case io::file_type::not_found:
            return fail(ctx, "Source does not exist", source.path());

        default: return fail(ctx, "Skipping unknown file", source.path());
    }
}

////////////////////////////////////////////////////////////////////////////////
struct entry { io::file file; bool descend; };

std::generator<entry&> walk_tree(context& ctx, const io::file& dir)
{
    for (auto&& expected_path : io::directory_iterator(dir.path()))
        if (expected_path)
        {
            std::error_code ec;
            io::file child{*expected_path, ec};
            if (ec) { fail(ctx, ec, *expected_path); continue; }

            if (child.is_symlink() && !ctx.keep_links)
            {
                child = child.follow_symlinks(ec);
                if (ec) { fail(ctx, ec, child.path()); continue; }
            }

            entry entry{ child, true };
            co_yield entry;

            if (child.is_directory() && entry.descend)
                co_yield std::ranges::elements_of( walk_tree(ctx, child) );
        }
        else fail(ctx, expected_path.error());
}

void copy_source(context& ctx, asio::thread_pool& pool, io::file source, io::file target)
{
    if (source.is_symlink() && !ctx.keep_links)
    {
        std::error_code ec;
        source = source.follow_symlinks(ec);
        if (ec) { fail(ctx, ec, source.path()); return; }
    }

    if (source.is_directory() && !ctx.recursive) {
        fail(ctx, "Skipping directory", source.path());
        return;
    }

    auto status = copy_entry(ctx, pool, std::as_const(source), std::as_const(target), false);
    if (good(status) && source.is_directory())
        for (auto&& [ source_child, descend ] : walk_tree(ctx, source))
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            std::error_code ec;
            auto name = source_child.path().lexically_relative(source.path());
            io::file target_child{ target.path() / name, ec };

            status = ec ? fail(ctx, ec, target_child.path())
                : copy_entry(ctx, pool, std::move(source_child), std::move(target_child), true);
            descend = good(status);
        }
}

void copy_sources(context& ctx, asio::thread_pool& pool, std::vector<io::file> sources, io::file target)
{
    if (target.is_directory())
    {
        for (auto&& source : sources)
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            auto real_target = target;
            if (source.path().has_filename())
            {
                std::error_code ec;
                real_target = io::file{target.path() / source.path().filename(), ec};
                if (ec) { fail(ctx, ec, real_target.path()); continue; }
            }
            copy_source(ctx, pool, std::move(source), std::move(real_target));
        }
    }
    else if (sources.size() == 1)
        copy_source(ctx, pool, std::move(sources.front()), std::move(target));
    else fail(ctx, std::make_error_code(std::errc::not_a_directory), target.path());
}

void update_dirs(context& ctx)
{
    for (auto&& [path, attr] : std::views::reverse(ctx.dir_attrs()))
    {
        std::error_code ec;
        io::modify(path, attr, ec);
        if (ec)
        {
            ctx.files_copied.fetch_sub(1, std::memory_order_relaxed);
            fail(ctx, ec, path);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
void report_status(context& ctx)
{
    do
    {
        std::this_thread::sleep_for(100ms);
        ctx.print_status();
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
        { "-a", "--archive",        "Archive mode (equivalent to -rmotD --unlink=auto)."},
        { "-D",                     "Same as --special --devices."                      },
        {       "--devices",        "Preserve device files."                            },
        { "-F", "--follow-dest-links", "Dereference destination symlinks."              },
        { "-f", "--unlink", "when", pgm::optval,
                                    "Unlink destination before writing. [when] can be one of:\n"
                                    "'never', 'always' or 'auto'.\n"
                                    "If [when] is omitted, 'always' is assumed.\n"
                                    "If the option is omitted entirely, 'auto' is used."},
        { "-g", "--group",          "Preserve group ownership."                         },
        { "-h", "--help",           "Show this help message and exit."                  },
        { "-j", "--jobs", "N",      "Number of files to copy in parallel (max: 16)."    },
        { "-L", "--follow-links",   "Dereference source symlinks (default when non-recursive)." },
        { "-m", "--mode",           "Preserve file permissions (mode bits)."            },
        { "-o", "--ownership",      "Same as --user --group."                           },
        { "-P", "--keep-links",     "Preserve source symlinks (default when recursive)."},
        { "-r", "--recursive",      "Copy directories recursively."                     },
        {       "--special",        "Preserve named pipes and sockets."                 },
        { "-T", "--target", "dir",  "Target directory to copy into."                    },
        { "-t", "--time",           "Preserve modification time."                       },
        { "-U", "--update", "when", pgm::optval,
                                    "Update existing files. [when] can be one of:\n"
                                    "'none', 'all', 'older', 'changed' (size or time) or 'size'.\n"
                                    "If [when] is omitted, 'older' is assumed.\n"
                                    "If the option is omitted entirely, all files are updated,\n"
                                    "which is equivalent to --update=all."              },
        { "-u", "--user",           "Preserve user ownership."                          },
        { "-V", "--version",        "Show program version and exit."                    },

        { "SOURCE", pgm::mul,       "Files or directories to copy or move."             },
        { "DESTINATION", pgm::opt,  "Destination file or directory."                    },
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

        for (auto&& path : args["SOURCE"].values())
        {
            io::file source{path, ec};
            if (ec) fail(ctx, ec, path);
            else sources.push_back(std::move(source));
        }

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

        if (args["--archive"])
        {
            ctx.keep_devices = true;
            ctx.keep_group = true;
            ctx.keep_mode  = true;
            ctx.keep_special = true;
            ctx.keep_time  = true;
            ctx.keep_user  = true;
            ctx.recursive  = true;
            ctx.unlink_ = unlink::auto_;
        }
        if (args["-D"]) ctx.keep_devices = ctx.keep_special = true;
        if (args["--devices"]) ctx.keep_devices = true;
        if (args["--follow-dest-links"]) ctx.follow_dest_links = true;
        if (args["--group"]) ctx.keep_group = true;
        if (args["--mode"]) ctx.keep_mode = true;
        if (args["--ownership"]) ctx.keep_group = ctx.keep_user = true;
        if (args["--special"]) ctx.keep_special = true;
        if (args["--time"]) ctx.keep_time = true;
        if (args["--user"]) ctx.keep_user = true;

        if (auto&& jobs = args["--jobs"])
        {
            auto n = parse(jobs.value()).value_or(-1);
            if (n < 1 || n > 16) throw pgm::invalid_argument{ "bad --jobs value '" + jobs.value() + "'"};
            ctx.jobs = n;
        }

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

        if (auto&& unlink = args["--unlink"])
        {
            auto&& when = unlink.value();
            if (when == "never") ctx.unlink_ = unlink::never;
            else if (when.empty() || when == "always") ctx.unlink_ = unlink::always;
            else if (when == "auto") ctx.unlink_ = unlink::auto_;
            else throw pgm::invalid_argument{ "bad --unlink value '" + when + "'" };
        }

        if (auto&& update = args["--update"])
        {
            auto&& when = update.value();
            if (when == "none") ctx.update_ = update::none;
            else if (when == "all") ctx.update_ = update::all;
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
        ctx.print_status(); // final status

        exit_code = ctx.error_free() ? 0 : 2;
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
