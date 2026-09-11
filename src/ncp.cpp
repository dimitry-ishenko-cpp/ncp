////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "context.hpp"
#include "io/file.hpp"
#include "io/misc.hpp"
#include "pgm/args.hpp"

#include <array>
#include <asio.hpp>
#include <charconv> // std::from_chars
#include <csignal>
#include <cstdio> // std::getchar
#include <exception>
#include <format>
#include <future>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
enum class status { failed, copied, moved, unchanged, skipped };

inline void message(context& ctx, auto type, auto msg) {
    ctx.print(retain, "{} {}\n", type, msg);
}
inline void message(context& ctx, auto type, auto msg, const io::file& file) {
    ctx.print(retain, "{} {} '{}'\n", type, msg, file.path().string());
}
inline void message(context& ctx, auto type, auto msg, const io::file& source, const io::file& target) {
    ctx.print(retain, "{} {} '{}' => '{}'\n", type, msg, source.path().string(), target.path().string());
}
inline void message(context& ctx, auto type, auto msg, const io::file& file, std::error_code ec) {
    ctx.print(retain, "{} {} '{}': {}\n", type, msg, file.path().string(), ec.message());
}
inline void message(context& ctx, auto type, auto msg, const io::file& source, const io::file& target, std::error_code ec) {
    ctx.print(retain, "{} {} '{}' => '{}': {}\n", type, msg, source.path().string(), target.path().string(), ec.message());
}

void attr_fail(context& ctx, auto&&... args) {
    if (ctx.verbose) message(ctx, "E:", std::forward<decltype (args)>(args)...);
    ctx.attr_failed.store(true, std::memory_order_relaxed);
}

auto fail(context& ctx, auto&&... args)
{
    message(ctx, "E:", std::forward<decltype (args)>(args)...);
    ctx.failed.store(true, std::memory_order_relaxed);
    return status::failed;
}

void info(context& ctx, auto&&... args) {
    message(ctx, "I:", std::forward<decltype (args)>(args)...);
}

auto skip(context& ctx, auto&&... args)
{
    message(ctx, "I:", std::forward<decltype (args)>(args)...);
    return status::skipped;
}

void verbose(context& ctx, auto&&... args) {
    if (ctx.verbose) message(ctx, "V:", std::forward<decltype (args)>(args)...);
}

////////////////////////////////////////////////////////////////////////////////
bool confirm(context& ctx, std::string_view action, const io::file& target)
{
    if (ctx.copy_all) return true;
    if (ctx.skip_all) return false;

    for (auto lock = ctx.get_print_lock();;)
    {
        ctx.print_locked(retain, "{} '{}'? [Y/n/a/s/q] ", action, target.path().string());

        auto c = std::getchar();
        auto reply = c;
        while (c != '\n' && c != EOF) c = std::getchar();

        switch (reply)
        {
            case 'y': case 'Y': case '\n': return true;
            case 'n': case 'N': return false;

            case 'a': case 'A': ctx.copy_all = true; return true;
            case 's': case 'S': ctx.skip_all = true; return false;

            case EOF: std::print("q\n");
            case 'q': case 'Q': ctx.quit = true; return false;
        }
    }
}

enum attr_option { include_all, exclude_mode, exclude_time };
auto get_attr(context& ctx, const io::file& source, attr_option option)
{
    io::attrib attr;
    if (ctx.keep_group) attr.gid = source.group_id();
    if (ctx.keep_mode && option != exclude_mode) attr.mode= source.mode();
    if (ctx.keep_time && option != exclude_time) attr.time= source.time();
    if (ctx.keep_user )
    {
        if (!ctx.can_chown && source.user_id() != ctx.uid)
        {
            if (attr.mode)
            {
                *attr.mode &= ~(io::mode::set_uid | io::mode::set_gid);
                ctx.attr_failed.store(true, std::memory_order_relaxed);
            }
        }
        else attr.uid = source.user_id();
    }
    return attr;
}

bool is_attr_error(const std::error_code& ec) {
    return ec == std::errc::operation_not_permitted || ec == std::errc::not_supported;
}

auto copy_regular_file(context& ctx, asio::thread_pool& pool, io::file source, io::file target)
{
    if (target && ctx.update_ == update::none) return status::unchanged;

    bool is_match = false;
    if (target.is_regular_file()) switch (ctx.update_)
    {
        case update::older:   is_match = target.time() >= source.time(); break;
        case update::size:    is_match = target.size() == source.size(); break;
        case update::changed: is_match = target.size() == source.size() && target.time() == source.time(); break;
        default:              is_match = false; break;
    }

    std::error_code ec;
    bool need_create = !target || !is_match || ctx.unlink_ == unlink::always;
    if (target && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "exists", target);

        if (ctx.interactive && !confirm(ctx, "overwrite", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, "remove", target, ec);
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
            io::copy_file(source, target, ec,
                [&ctx](io::file_size chunk)
                {
                    ctx.bytes_copied.fetch_add(chunk, std::memory_order_relaxed);
                    return !ctx.quit.load(std::memory_order_relaxed);
                });

            if (ec)
            {
                fail(ctx, "copy", source, target, ec);
                return;
            }
            else verbose(ctx, "copy", source, target);

            if (auto attr = get_attr(ctx, source, include_all))
            {
                io::modify(target.path(), attr, ec);
                if (ec)
                {
                    if (is_attr_error(ec)) attr_fail(ctx, "attrs", target, ec);
                    else { fail(ctx, "attrs", target, ec); return; }
                }
            }

            ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

            if (ctx.move)
            {
                io::remove(source.path(), ec);
                if (ec) fail(ctx, "remove", source, ec);
            }
        });
    }
    else
    {
        if (auto attr = get_attr(ctx, source, include_all))
        {
            io::modify(target.path(), attr, ec);
            if (ec)
            {
                if (is_attr_error(ec)) attr_fail(ctx, "attrs", target, ec);
                else return fail(ctx, "attrs", target, ec);
            }
            else verbose(ctx, "attrs", target);
        }

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
        ctx.bytes_copied.fetch_add(source.size(), std::memory_order_relaxed);

        if (ctx.move)
        {
            io::remove(source.path(), ec);
            if (ec) fail(ctx, "remove", source, ec);
        }
    }

    return status::copied;
}

auto copy_directory(context& ctx, io::file source, io::file target)
{
    bool need_create = !target || !target.is_directory();
    if (!need_create && ctx.update_ == update::none) return status::unchanged;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    std::error_code ec;
    if (target && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "exists", target);

        if (ctx.interactive && !confirm(ctx, "replace", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, "remove", target, ec);
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
        if (ec) return fail(ctx, "create dir", target, ec);
        verbose(ctx, "create dir", target);
    }

    if (auto attr = get_attr(ctx, source, include_all))
        ctx.add_dir_attr(std::move(target), attr);
    else ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

    if (ctx.move) ctx.add_rmdir(std::move(source));

    return status::copied;
}

template <typename MatchFn, typename CreateFn>
auto copy_generic(context& ctx, io::file source, io::file target, attr_option option,
    MatchFn&& is_match, CreateFn&& create)
{
    if (target && ctx.update_ == update::none) return status::unchanged;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    std::error_code ec;
    bool need_create = !target || !is_match(source, target) || ctx.unlink_ == unlink::always;
    if (target && need_create)
    {
        if (ctx.unlink_ == unlink::never)
            return fail(ctx, "exists", target);

        if (ctx.interactive && !confirm(ctx, "replace", target))
            return status::skipped;

        io::remove(target.path(), ec);
        if (ec) return fail(ctx, "remove", target, ec);
    }

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

        create(source, target, ec);
        if (ec) return fail(ctx, "create", target, ec);
        verbose(ctx, "create", target);
    }

    if (auto attr = get_attr(ctx, source, option))
    {
        io::modify(target.path(), attr, ec);
        if (ec)
        {
            if (is_attr_error(ec)) attr_fail(ctx, "attrs", target, ec);
            else return fail(ctx, "attrs", target, ec);
        }
        else verbose(ctx, "attrs", target);
    }

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

    if (ctx.move)
    {
        io::remove(source.path(), ec);
        if (ec) fail(ctx, "remove", source, ec);
    }

    return status::copied;
}

auto copy_symlink(context& ctx, io::file source, io::file target)
{
    std::error_code ec;
    auto link_target = source.get_target_path(ec);
    if (ec) return fail(ctx, "read symlink", source, ec);

    return copy_generic(ctx, std::move(source), std::move(target), exclude_mode,
        [&link_target](auto&& src, auto&& tgt) {
            std::error_code ec;
            return tgt.get_target_path(ec) == link_target;
        },
        [&link_target](auto&& src, auto&& tgt, std::error_code& ec) {
            io::create_symlink(tgt.path(), link_target, ec);
        });
}

auto copy_device(context& ctx, io::file source, io::file target)
{
    if (!ctx.keep_devices)
        return skip(ctx, "skipping device file", source);

    return copy_generic(ctx, std::move(source), std::move(target), include_all,
        [](auto&& src, auto&& tgt) {
            return tgt.type() == src.type() && tgt.device_type() == src.device_type();
        },
        [](auto&& src, auto&& tgt, std::error_code& ec) {
            if (src.is_block_device())
                io::create_block_device(tgt.path(), src.device_type(), ec);
            else io::create_char_device(tgt.path(), src.device_type(), ec);
        }
    );
}

auto copy_special(context& ctx, io::file source, io::file target)
{
    if (!ctx.keep_special)
        return skip(ctx, "special file", source);

    return copy_generic(ctx, std::move(source), std::move(target), include_all,
        [](auto&& src, auto&& tgt) {
            return tgt.type() == src.type();
        },
        [](auto&& src, auto&& tgt, std::error_code& ec) {
            if (src.is_fifo()) io::create_fifo(tgt.path(), ec);
            else io::create_socket(tgt.path(), ec);
        });
}

auto copy_entry(context& ctx, asio::thread_pool& pool, io::file source, io::file target, bool from_walk)
{
    if (target == source) return skip(ctx, "skipping same file", source, target);

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
            else return fail(ctx, "read socket", source);

        case io::file_type::not_found:
            return fail(ctx, "non-extant", source);

        default: return fail(ctx, "unknown file", source);
    }
}

////////////////////////////////////////////////////////////////////////////////
void copy_tree(context& ctx, asio::thread_pool& pool, io::file source, io::file target, bool from_walk)
{
    if (source.is_directory())
    {
        if (!ctx.recursive) { skip(ctx, "skipping dir", source); return; }

        std::error_code ec;
        auto status = copy_entry(ctx, pool, source, target, from_walk); // copy source and target

        switch (status)
        {
            case status::copied: // reread target, as it's changed
                target = source.is_symlink() ? io::file{target.path(), ec}
                    : io::file{target.path(), io::follow_symlinks, ec};
                if (ec) { fail(ctx, "access", target, ec); return; }

            case status::unchanged:
                for (auto&& name : io::directory_iterator(source))
                    if (name)
                    {
                        if (ctx.quit.load(std::memory_order_relaxed)) break;

                        auto child_source = ctx.keep_links ? io::file{source, name.value(), ec}
                            : io::file{source, name.value(), io::follow_symlinks, ec};
                        if (ec) { fail(ctx, "access", child_source, ec); continue; }

                        auto child_target = child_source.is_symlink() ? io::file{target, name.value(), ec}
                            : io::file{target, name.value(), io::follow_symlinks, ec};
                        if (ec) { fail(ctx, "access", child_target, ec); continue; }

                        copy_tree(ctx, pool, std::move(child_source), std::move(child_target), true);
                    }
                    else fail(ctx, "read dir", source, name.error());

            default:;
        }
    }
    else copy_entry(ctx, pool, std::move(source), std::move(target), from_walk);
}

void copy_sources(context& ctx, asio::thread_pool& pool, std::vector<io::file> sources, io::file target)
{
    if (target.is_directory())
    {
        for (auto&& source : sources)
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            io::file target_;
            if (source.path().has_filename()) // rsync-style behavior
            {
                std::error_code ec;
                auto name = source.path().filename();

                target_ = source.is_symlink() ? io::file{target, name, ec}
                    : io::file{target, name, io::follow_symlinks, ec};
                if (ec) { fail(ctx, "access", target_, ec); continue; }
            }
            else target_ = target;

            copy_tree(ctx, pool, std::move(source), std::move(target_), false);
        }
    }
    else if (sources.size() == 1)
    {
        auto& source = sources.front();
        if (source.is_symlink()) // --keep-links
        {
            std::error_code ec;
            target = io::file{target.path(), ec};
            if (ec) { fail(ctx, "access", target, ec); return; }
        }
        copy_tree(ctx, pool, std::move(source), std::move(target), false);
    }
    else if (sources.size() > 1)
        fail(ctx, "copy", target, std::make_error_code(std::errc::not_a_directory));
}

void process_dirs(context& ctx)
{
    std::error_code ec;
    for (auto&& [dir, attr] : std::views::reverse(ctx.dir_attrs()))
    {
        io::modify(dir.path(), attr, ec);
        if (ec)
        {
            if (is_attr_error(ec)) attr_fail(ctx, "attrs", dir, ec);
            else { fail(ctx, "attrs", dir, ec); continue; }
        }
        else verbose(ctx, "attrs", dir);

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    }

    for (auto&& dir : std::views::reverse(ctx.rmdirs()))
    {
        io::remove_directory(dir.path(), ec);
        if (ec) fail(ctx, "remove dir", dir, ec);
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

void show_progress(context& ctx, bool final = false)
{
    auto files_total = ctx.files_total.load(std::memory_order_relaxed);
    auto files_copied = ctx.files_copied.load(std::memory_order_relaxed);
    auto bytes_total = ctx.bytes_total.load(std::memory_order_relaxed);
    auto bytes_copied = ctx.bytes_copied.load(std::memory_order_relaxed);

    auto percent = bytes_total ? (100.0 * bytes_copied / bytes_total) : 100.0;
    if (ctx.quit.load(std::memory_order_relaxed)) ctx.percent_copied = percent;
    else ctx.percent_copied += (percent - ctx.percent_copied) * 0.33;

    using namespace std::chrono;
    auto now = steady_clock::now();
    auto elapsed = duration_cast<seconds>(now - ctx.start_time);

    if (auto delta = duration<double>{now - ctx.last_time}.count())
    {
        auto speed = (bytes_copied - ctx.last_bytes) / delta;
        ctx.speed = ctx.speed ? (ctx.speed + (speed - ctx.speed) * 0.1) : speed;

        ctx.last_time = now;
        ctx.last_bytes = bytes_copied;
    }

    seconds eta{ ctx.speed ? static_cast<long>((bytes_total - bytes_copied) / ctx.speed) : 0 };

    ////////////////////
    constexpr auto min_bar_width = 15, max_bar_width = 41;
    constexpr auto b_x = 2; // ● takes up 3 chars

    auto width = io::term_width();

    auto metric = std::format(" {}/{} ● {}/{}", files_copied, files_total,
        format_bytes(bytes_copied), format_bytes(bytes_total)
    );
    if (width > metric.size() - b_x)
    {
        width -= metric.size() - b_x;

        std::string time;
        if (final) time = std::format(" ● {}", format_time(elapsed), format_time(eta));
        else time = std::format(" ● {} ETA {}", format_time(elapsed), format_time(eta));
        if (width > time.size() - b_x)
        {
            width -= time.size() - b_x;

            auto speed = std::format(" ● {}/s", format_bytes(ctx.speed));
            if (width > speed.size() - b_x) { width -= speed.size() - b_x; metric += speed; }

            metric += time;
        }
    }
    else
    {
        if (final) metric = std::format(" ● {}", format_time(elapsed), format_time(eta));
        else metric = std::format(" ● {} ETA {}", format_time(elapsed), format_time(eta));
        if (width > metric.size()) width -= metric.size(); else metric.clear();
    }

    auto bar = std::format(" {:>3.0f}%", ctx.percent_copied);
    if (width > bar.size())
    {
        width -= bar.size();

        if (width > min_bar_width)
        {
            if (width > max_bar_width) width = max_bar_width;
            bar += " "; width -= 2;

            int done = ctx.percent_copied * width / 100;
            for (auto n = 0; n < done; ++n) bar += "|";
            for (auto n = done; n < width; ++n) bar += ".";
        }
    }
    else bar.clear();

    ctx.print(replace, "{}{}\n", bar, metric);
}

////////////////////////////////////////////////////////////////////////////////
context* pctx = nullptr;
extern "C" void signal_handler(int signal)
{
    if (pctx)
    {
        pctx->exit_signal = signal;
        pctx->quit = true;
    }
}

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

std::optional<int> parse(std::string_view text)
{
    int n;
    auto from = text.data(), to = from + text.size();
    auto [p, ec] = std::from_chars(from, to, n);
    if (ec == std::errc{} && p == to) return n; else return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
enum exit_code
{
    success = 0,
    invalid_argument = 1,
    interrupted = 2,
    copy_failed = 3,
    attr_failed = 4,
};

int main(int argc, char* argv[])
try
{
    auto code = success;
    auto name = io::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-a", "--archive",        "Archive mode (equivalent to -rmotD --unlink=auto)."},
        { "-D",                     "Same as --special --devices."                      },
        {       "--devices",        "Preserve device files."                            },
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
        ctx.uid = io::effective_user_id();
        ctx.can_chown = ctx.uid ? io::have_cap_chown() : true;

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

        std::error_code ec;
        std::vector<io::file> sources;
        io::file target;

        for (auto&& path : args["SOURCE"].values())
        {
            auto source = ctx.keep_links ? io::file{path, ec} : io::file{path, io::follow_symlinks, ec};
            if (ec) fail(ctx, "access", source, ec);
            else sources.push_back(std::move(source));
        }

        auto&& destination_path = args["DESTINATION"];
        auto&& target_path = args["--target"];

        if (target_path)
        {
            // DESTINATION will capture the last positional parameter,
            // but if --target was specified that value belongs in SOURCES
            if (destination_path)
            {
                auto source = ctx.keep_links ? io::file{destination_path.value(), ec}
                    : io::file{destination_path.value(), io::follow_symlinks, ec};
                if (ec) fail(ctx, "access", source, ec);
                else sources.push_back(std::move(source));
            }

            target = io::file{target_path.value(), io::follow_symlinks, ec};
            if (ec) throw io::exception{"main", target.path(), ec};
        }
        else if (destination_path)
        {
            target = io::file{destination_path.value(), io::follow_symlinks, ec};
            if (ec) throw io::exception{"main", target.path(), ec};
        }
        else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};

        ////////////////////
        asio::thread_pool pool{ ctx.jobs };

        pctx = &ctx;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        io::raise_open_file_limit();

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

        // don't process dirs on Ctrl+C
        if (!ctx.quit.exchange(true)) process_dirs(ctx);

        if (auto signal = ctx.exit_signal.exchange(0))
        {
            info(ctx, "received signal " + std::to_string(signal) + ", exiting");
            code = interrupted;
        }
        else
        {
            if (ctx.failed) code = copy_failed;
            else if (ctx.attr_failed) code = attr_failed;

            if (ctx.attr_failed) info(ctx, "some attrs could not be preserved");
        }

        if (ctx.progress)
        {
            progress.wait();
            show_progress(ctx, true); // final status
        }
    }

    return code;
}
catch (const io::exception& e)
{
    std::print("E: {}: '{}'\n", e.code().message(), e.path1().string());
    return invalid_argument;
}
catch (const std::exception& e)
{
    std::print("E: {}\n", e.what());
    return invalid_argument;
};
