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
#include <charconv> // std::from_chars
#include <csignal>
#include <cstdio> // std::getchar
#include <exception>
#include <format>
#include <future>
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
    auto preamble = std::format(R"(
{} – new and improved, now asbestos-free copy utility.)",
    name);

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
        pctx->exit_signal = signal;
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
enum class status { failed, copied, moved, unchanged, skipped };

template <typename... Args>
auto error_(context& ctx, std::format_string<Args...> fmt, Args&&... args)
{
    ctx.print(retain, fmt, std::forward<Args>(args)...);
    ctx.error_free.store(false, std::memory_order_relaxed);
    return status::failed;
}
inline auto error(context& ctx, std::string_view msg, const io::file& file) {
    return error_(ctx, "{}: '{}'\n", msg, file.path().string());
}
inline auto error(context& ctx, std::string_view msg, const io::file& src, const io::file& tgt) {
    return error_(ctx, "{}: '{}' => '{}'\n", msg, src.path().string(), tgt.path().string());
}
inline auto cannot(context& ctx, std::string_view act, const io::path& path, std::error_code ec) {
    return error_(ctx, "cannot {} '{}': {}\n", act, path.string(), ec.message());
}
inline auto cannot(context& ctx, std::string_view act, const io::file& file, std::error_code ec) {
    return cannot(ctx, act, file.path(), ec);
}
inline auto cannot(context& ctx, std::string_view act, const io::file& src, const io::file& tgt, std::error_code ec) {
    return error_(ctx, "cannot {} '{}' to '{}': {}\n", act, src.path().string(), tgt.path().string(), ec.message());
}

inline auto good(status status) {
    return status == status::copied || status == status::unchanged;
}

void verbose(context& ctx, std::string_view act, const io::file& file) {
    if (ctx.verbose) ctx.print(retain, "{}: '{}'\n", act, file.path().string());
}
void verbose(context& ctx, std::string_view act, const io::file& src, const io::file& tgt) {
    if (ctx.verbose) ctx.print(retain, "{}: '{}' => '{}'\n", act, src.path().string(), tgt.path().string());
}

////////////////////////////////////////////////////////////////////////////////
bool confirm(context& ctx, std::string_view action, const io::file& file)
{
    if (ctx.confirm_all) return true;

    for (auto lock = ctx.get_print_lock();;)
    {
        ctx.print_locked(retain, "{} '{}'? [Y/n/a/q] ", action, file.path().string());

        auto c = std::getchar();
        auto reply = c;
        while (c != '\n' && c != EOF) c = std::getchar();

        switch (reply)
        {
            case 'y': case 'Y': case '\n': return true;
            case 'n': case 'N': return false;
            case 'a': case 'A': ctx.confirm_all = true; return true;
            case EOF: std::print("q\n");
            case 'q': case 'Q': ctx.quit = true; return false;
        }
    }
}

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
            return error(ctx, "not replacing existing file", target);

        if (ctx.interactive && !confirm(ctx, "overwrite", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return cannot(ctx, "remove", target, ec);
    }

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);
    ctx.bytes_total.fetch_add(source.size(), std::memory_order_relaxed);

    if (need_create)
    {
        if (ctx.move)
        {
            io::rename(source.path(), target.path(), ec);
            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                ctx.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
                verbose(ctx, "move", source, target);
                return status::moved;
            }
        }

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

            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                verbose(ctx, "copy", source, target);

                if (ctx.move)
                {
                    io::remove(source.path(), ec);
                    if (ec) cannot(ctx, "remove", source, ec);
                }
            }
            else cannot(ctx, "copy", source, target, ec);
        });
    }
    else
    {
        auto attr = get_attr(ctx, source, include_all);
        io::modify(target.path(), attr, ec);
        if (ec) return cannot(ctx, "update attrs for", target, ec);
        
        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        ctx.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);
        verbose(ctx, "update attrs", target);

        if (ctx.move)
        {
            io::remove(source.path(), ec);
            if (ec) cannot(ctx, "remove", source, ec);
        }
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
            return error(ctx, "not replacing existing non-dir", target);

        if (ctx.interactive && !confirm(ctx, "replace", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return cannot(ctx, "remove", target, ec);
    }

    if (ctx.move)
    {
        io::rename(source.path(), target.path(), ec);
        if (!ec)
        {
            ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
            verbose(ctx, "move", source, target);
            return status::moved;
        }
    }

    if (need_create)
    {
        io::create_directory(target.path(), ec);
        if (ec) return cannot(ctx, "create dir", target, ec);
    }

    auto attr = get_attr(ctx, source, include_all);
    ctx.add_dir_attr(target.path(), attr);

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    verbose(ctx, need_create ? "create" : "update attrs", target);

    if (ctx.move) ctx.add_rmdir(source.path());

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
            return error(ctx, "not replacing existing file", target);

        if (ctx.interactive && !confirm(ctx, "replace", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return cannot(ctx, "remove", target, ec);
    }

    auto attr = get_attr(ctx, source, option);
    if (need_create)
    {
        if (ctx.move)
        {
            io::rename(source.path(), target.path(), ec);
            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                verbose(ctx, "move", source, target);
                return status::moved;
            }
        }
        create(source, target, attr, ec);
        if (ec) return cannot(ctx, "create", target, ec);

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        verbose(ctx, "create", target);
    }
    else
    {
        io::modify(target.path(), attr, ec);
        if (ec) return cannot(ctx, "update attrs for", target, ec);

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        verbose(ctx, "update attrs", target);
    }

    if (ctx.move)
    {
        io::remove(source.path(), ec);
        if (ec) cannot(ctx, "remove", source, ec);
    }

    return status::copied;
}

auto copy_symlink(context& ctx, io::file source, io::file target)
{
    std::error_code ec;
    auto link_target = source.get_target_path(ec);
    if (ec) return cannot(ctx, "read symlink", source, ec);

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
        return error(ctx, "skipping device file", source); 

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
        return error(ctx, "skipping special file", source); 

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
        if (ec) return cannot(ctx, "resolve symlink", target, ec);
    }

    if (target == source)
        return error(ctx, "skipping same file", source, target);

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
            else return error(ctx, "cannot copy from socket", source);

        case io::file_type::not_found:
            return error(ctx, "source does not exist", source);

        default: return error(ctx, "skipping unknown file", source);
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
            if (ec) { cannot(ctx, "access", child, ec); continue; }

            if (child.is_symlink() && !ctx.keep_links)
            {
                child = child.follow_symlinks(ec);
                if (ec) { cannot(ctx, "resolve symlink", child, ec); continue; }
            }

            entry entry{ child, true };
            co_yield entry;

            if (child.is_directory() && entry.descend)
                co_yield std::ranges::elements_of( walk_tree(ctx, child) );
        }
        else cannot(ctx, "read directory", dir, expected_path.error());
}

void copy_source(context& ctx, asio::thread_pool& pool, io::file source, io::file target)
{
    if (source.is_symlink() && !ctx.keep_links)
    {
        std::error_code ec;
        source = source.follow_symlinks(ec);
        if (ec) { cannot(ctx, "resolve symlink", source, ec); return; }
    }

    if (source.is_directory() && !ctx.recursive) {
        error(ctx, "skipping directory", source);
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

            status = ec ? cannot(ctx, "resolve path", target_child, ec)
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
                if (ec) { cannot(ctx, "resolve path", real_target, ec); continue; }
            }
            copy_source(ctx, pool, std::move(source), std::move(real_target));
        }
    }
    else if (sources.size() == 1)
        copy_source(ctx, pool, std::move(sources.front()), std::move(target));
    else cannot(ctx, "copy", target, std::make_error_code(std::errc::not_a_directory));
}

void process_dirs(context& ctx)
{
    std::error_code ec;
    for (auto&& [path, attr] : std::views::reverse(ctx.dir_attrs()))
    {
        io::modify(path, attr, ec);
        if (ec)
        {
            ctx.files_copied.fetch_sub(1, std::memory_order_relaxed);
            cannot(ctx, "update attrs for", path, ec);
        }
    }

    for (auto&& path : std::views::reverse(ctx.rmdirs()))
    {
        io::remove_directory(path, ec);
        if (ec) cannot(ctx, "remove directory", path, ec);
    }
}

////////////////////////////////////////////////////////////////////////////////
auto format_bytes(long bytes)
{
    constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};

    auto n = 0;
    auto dbl_bytes = static_cast<double>(bytes);
    for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

    return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
}

auto format_time(std::chrono::seconds dur)
{
    if (dur >= 1h) return std::format("{:%H:%M:%S}", dur);
    else return std::format("{:%M:%S}", dur);
}

void show_progress(context& ctx)
{
    auto files_total = ctx.files_total.load(std::memory_order_relaxed);
    auto files_copied = ctx.files_copied.load(std::memory_order_relaxed);
    auto bytes_total = ctx.bytes_total.load(std::memory_order_relaxed);
    auto bytes_copied = ctx.bytes_copied.load(std::memory_order_relaxed);

    auto percent = bytes_total ? (100.0 * bytes_copied / bytes_total) : 100.0;
    if (ctx.quit.load(std::memory_order_relaxed)) ctx.percent_copied = percent;
    else ctx.percent_copied += (percent - ctx.percent_copied) * 0.33;

    int width = 40;
    int done = ctx.percent_copied * width / 100;
    
    std::string bar;
    for (auto n = 0; n < done; ++n) bar += "█";
    for (auto n = done; n < width; ++n) bar += "░";

    using std::chrono::seconds;
    auto now = steady_clock::now();
    auto elapsed = std::chrono::duration_cast<seconds>(now - ctx.start_time);

    if (auto delta = std::chrono::duration<double>{now - ctx.last_time}.count())
    {
        auto speed = (bytes_copied - ctx.last_bytes) / delta;
        ctx.speed = ctx.speed ? (ctx.speed + (speed - ctx.speed) * 0.1) : speed;

        ctx.last_time = now;
        ctx.last_bytes = bytes_copied;
    }

    seconds eta{ ctx.speed ? static_cast<long>((bytes_total - bytes_copied) / ctx.speed) : 0 };

    ctx.print(replace, " {:>3.0f}% {} {}/{} ● {}/{} ● {}/s ● {} ETA {}\n",
        ctx.percent_copied, bar, files_copied, files_total,
        format_bytes(bytes_copied), format_bytes(bytes_total), format_bytes(ctx.speed),
        format_time(elapsed), format_time(eta)
    );
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
        { "-i", "--interactive",    "Prompt before overwriting files."                  },
        { "-j", "--jobs", "N",      "Number of files to copy in parallel (max: 16)."    },
        { "-L", "--follow-links",   "Dereference source symlinks (default when non-recursive)." },
        { "-M", "--move",           "Remove source files after copying."                },
        { "-m", "--mode",           "Preserve file permissions (mode bits)."            },
        { "-o", "--ownership",      "Same as --user --group."                           },
        { "-P", "--keep-links",     "Preserve source symlinks (default when recursive)."},
        { "-p", "--progress",       "Show progress bar."                                },
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
        { "-v", "--verbose",        "Explain what is being done."                       },

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
            if (ec) cannot(ctx, "access", source, ec);
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
        if (args["--interactive"]) ctx.interactive = true;
        if (args["--mode"]) ctx.keep_mode = true;
        if (args["--move"] || name == "nmv") ctx.move = true;
        if (args["--ownership"]) ctx.keep_group = ctx.keep_user = true;
        if (args["--progress"]) ctx.progress = true;
        if (args["--special"]) ctx.keep_special = true;
        if (args["--time"]) ctx.keep_time = true;
        if (args["--user"]) ctx.keep_user = true;
        if (args["--verbose"]) ctx.verbose = true;

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

        std::future<void> progress;
        if (ctx.progress) progress = std::async(std::launch::async, [&ctx]
        {
            while (!ctx.quit.load(std::memory_order_relaxed))
            {
                std::this_thread::sleep_for(100ms);
                show_progress(ctx);
            }
        });

        copy_sources(ctx, pool, std::move(sources), std::move(target));
        pool.join();

        if (!ctx.quit) process_dirs(ctx); // don't process dirs on Ctrl+C
        ctx.quit = true;

        if (ctx.progress)
        {
            progress.wait();
            show_progress(ctx); // final status
        }

        if (auto signal = ctx.exit_signal.exchange(0))
        {
            ctx.print(retain, "received signal {}, exiting\n", signal);
            exit_code = 3;
        }
        else if (!ctx.error_free) exit_code = 2;
    }

    return exit_code;
}
catch (const io::exception& e)
{
    std::print("{}: '{}'\n", e.code().message(), e.path1().string());
    return 1;
}
catch (const std::exception& e)
{
    std::print("{}\n", e.what());
    return 1;
};
