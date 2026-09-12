////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io/file.hpp"
#include "io/misc.hpp"
#include "message.hpp"
#include "pgm/args.hpp"

#include <array>
#include <asio.hpp>
#include <atomic>
#include <charconv> // std::from_chars
#include <chrono>
#include <csignal>
#include <cstdio> // std::getchar
#include <exception>
#include <format>
#include <future>
#include <optional>
#include <print>
#include <ranges> // std::views::reverse
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
enum class status { failed, copied, moved, unchanged, skipped };
enum class unlink { never, always, auto_ };
enum class update { none, all, older, changed, size, };

struct
{
    std::size_t jobs = 1;

    bool can_chown = false;
    io::user_id uid = -1;

    bool interactive = false;

    bool keep_mode = false;
    bool keep_time = false;
    bool keep_user = false, keep_group = false;

    bool keep_devices = false;
    bool keep_links = false;
    bool keep_special = false;

    bool move = false;
    bool progress = false;
    bool recursive = false;
    enum unlink unlink_ = unlink::auto_;
    enum update update_ = update::all;
    bool verbose = false;

    ////////////////////
    std::atomic<int> exit_signal{0};
    std::atomic<bool> quit{ false };

    std::atomic<bool> failed{ false }, attr_failed{ false };
    bool copy_all = false, skip_all = false;

    std::atomic<long> files_total{0}, files_copied{0};
    std::atomic<long> bytes_total{0}, bytes_copied{0};
    double percent_copied = 0;

    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_time = start_time;
    long last_bytes = 0;
    double speed = 0;

    struct dir_attr_entry { io::file source, target; };
    std::vector<dir_attr_entry> dir_attrs;

    struct rmdir_entry { io::file parent; io::path name; };
    std::vector<rmdir_entry> rmdirs;
}
ctx;

struct node
{
    const io::file& parent;
    io::path name;
    io::file file;
};

////////////////////////////////////////////////////////////////////////////////
namespace std
{
template <> struct formatter<io::path> : formatter<string_view> {
    auto format(auto&& p, auto& ctx) const { return formatter<string_view>::format(p.string(), ctx); }
};
template <> struct formatter<io::file> : formatter<string_view> {
    auto format(auto&& f, auto& ctx) const { return formatter<string_view>::format(f.path().string(), ctx); }
};
}

void attr_fail(auto&&... args)
{
    if (ctx.verbose) message("E:", std::forward<decltype (args)>(args)...);
    ctx.attr_failed.store(true, std::memory_order_relaxed);
}

auto fail(auto&&... args)
{
    message("E:", std::forward<decltype (args)>(args)...);
    ctx.failed.store(true, std::memory_order_relaxed);
    return status::failed;
}

void info(auto&&... args) { message("I:", std::forward<decltype (args)>(args)...); }

auto skip(auto&&... args)
{
    message("I:", std::forward<decltype (args)>(args)...);
    return status::skipped;
}

void verbose(auto&&... args) { if (ctx.verbose) message("V:", std::forward<decltype (args)>(args)...); }

bool confirm(std::string_view action, const io::file& target)
{
    if (ctx.copy_all) return true;
    if (ctx.skip_all) return false;

    for (auto lock = get_print_lock();;)
    {
        print_locked(retain, "{} '{}'? [Y/n/a/s/q] ", action, target.path().string());

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

////////////////////////////////////////////////////////////////////////////////
void apply_attrs(const io::file& source, io::file& target, std::error_code& ec)
{
    io::mode mode = source.mode();

    constexpr auto none = -1;
    io::user_id uid = none; io::group_id gid = none;

    if (ctx.keep_user)
    {
        if (!ctx.can_chown && source.user_id() != ctx.uid)
        {
            if (ctx.keep_mode)
            {
                mode &= ~(io::mode::set_uid | io::mode::set_gid);
                ctx.attr_failed.store(true, std::memory_order_relaxed);
            }
        }
        else uid = source.user_id();
    }
    if (ctx.keep_group) gid = source.group_id();

    ec.clear();
    // owner must be first, as it will strip suid/sgid bits; time must be last
    if (!ec && (ctx.keep_user || ctx.keep_group)) target.owner(uid, gid, ec);
    if (!ec && ctx.keep_mode) target.mode(mode, ec);
    if (!ec && ctx.keep_time) target.time(source.time(), ec);
}

bool is_attr_error(const std::error_code& ec) {
    return ec == std::errc::operation_not_permitted || ec == std::errc::not_supported;
}

auto copy_file(asio::thread_pool& pool, node source, node target)
{
    asio::post(pool, [source = std::move(source), target = std::move(target)] mutable
    {
        if (ctx.quit.load(std::memory_order_relaxed)) return;

        std::error_code ec;
        io::copy_file(source.file, target.parent, target.name, ec,
            [](io::file_size chunk)
            {
                ctx.bytes_copied.fetch_add(chunk, std::memory_order_relaxed);
                return !ctx.quit.load(std::memory_order_relaxed);
            });

        if (ec) { fail("copy", source.file, target.file, ec); return; }
        else verbose("copy", source.file, target.file);

        if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
        {
            target.file = io::file{target.parent, target.name, ec};
            if (ec) { fail("access", target.file, ec); return; }

            apply_attrs(source.file, target.file, ec);
            if (ec)
            {
                if (is_attr_error(ec)) attr_fail("attrs", target.file, ec);
                else { fail("attrs", target.file, ec); return; }
            }
        }

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

        if (ctx.move && source.file.is_regular_file())
        {
            io::remove(source.parent, source.name, ec);
            if (ec) fail("remove", source.file, ec);
        }
    });

    return status::copied;
}

auto copy_regular_file(asio::thread_pool& pool, node source, node target)
{
    std::error_code ec;
    bool create = false;

    if (target.file)
    {
        if (!target.file.is_regular_file() || ctx.unlink_ == unlink::always)
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target.file);
            if (ctx.interactive && !confirm("overwrite", target.file)) return status::skipped;

            io::remove(target.parent, target.name, ec);
            if (ec) return fail("remove", target.file, ec);

            create = true;
        }
        else
        {
            switch (ctx.update_)
            {
                case update::none: return status::unchanged;
                case update::older:
                    if (target.file.time() < source.file.time()) create = true;
                    else return status::unchanged; // don't touch newer files
                    break;
                case update::changed: 
                    create = target.file.size() != source.file.size() || target.file.time() != source.file.time();
                    break;
                case update::size:
                    create = target.file.size() != source.file.size();
                    break;
                default: create = true;
            }

            if (create)
            {
                if (ctx.interactive && !confirm("overwrite", target.file)) return status::skipped;
            }
            else if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
            {
                if (ctx.interactive && !confirm("update", target.file)) return status::skipped;
            }
            else return status::unchanged;
        }
    }
    else create = true;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);
    ctx.bytes_total.fetch_add(source.file.size(), std::memory_order_relaxed);

    if (create)
    {
        if (ctx.move)
        {
            io::rename(source.parent, source.name, target.parent, target.name, ec);
            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                ctx.bytes_copied.fetch_add(source.file.size(), std::memory_order_relaxed);
                verbose("move", source.file, target.file);
                return status::moved;
            }
        }

        return copy_file(pool, std::move(source), std::move(target));
    }

    // already checked: ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group
    apply_attrs(source.file, target.file, ec);
    if (ec)
    {
        if (is_attr_error(ec)) attr_fail("attrs", target.file, ec);
        else return fail("attrs", target.file, ec);
    }
    else verbose("attrs", target.file);

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    ctx.bytes_copied.fetch_add(source.file.size(), std::memory_order_relaxed);

    if (ctx.move)
    {
        io::remove(source.parent, source.name, ec);
        if (ec) fail("remove", source.file, ec);
    }

    return status::copied;
}

auto copy_directory(node source, node target)
{
    std::error_code ec;
    bool create = false;

    if (target.file)
    {
        if (!target.file.is_directory())
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target.file);
            if (ctx.interactive && !confirm("replace", target.file)) return status::skipped;

            io::remove(target.parent, target.name, ec);
            if (ec) return fail("remove", target.file, ec);
            
            create = true;
        }
        else if (ctx.update_ == update::none) return status::unchanged;
    }
    else create = true;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    if (create)
    {
        if (ctx.move)
        {
            io::rename(source.parent, source.name, target.parent, target.name, ec);
            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                verbose("move", source.file, target.file);
                return status::moved;
            }
        }

        io::create_directory(target.parent, target.name, ec);
        if (ec) return fail("create dir", target.file, ec);
        verbose("create dir", target.file);
    }

    if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
    {
        if (create)
        {
            target.file = io::file{target.parent, target.name, ec};
            if (ec) return fail("access", target.file, ec);
        }

        ctx.dir_attrs.emplace_back(std::move(source.file), std::move(target.file));
    }
    else ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

    if (ctx.move) ctx.rmdirs.emplace_back(source.parent, std::move(source.name));

    return create ? status::copied : status::unchanged;
}

template <typename MatchFn, typename CreateFn>
auto copy_generic(node source, node target, MatchFn&& match_fn, CreateFn&& create_fn)
{
    std::error_code ec;
    bool create = false;

    if (target.file)
    {
        if (!match_fn(source, target) || ctx.unlink_ == unlink::always)
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target.file);
            if (ctx.interactive && !confirm("replace", target.file)) return status::skipped;

            io::remove(target.parent, target.name, ec);
            if (ec) return fail("remove", target.file, ec);

            create = true;
        }
        else if (ctx.update_ == update::none) return status::unchanged;
    }
    else create = true;

    ctx.files_total.fetch_add(1, std::memory_order_relaxed);

    if (create)
    {
        if (ctx.move)
        {
            io::rename(source.parent, source.name, target.parent, target.name, ec);
            if (!ec)
            {
                ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
                verbose("move", source.file, target.file);
                return status::moved;
            }
        }

        create_fn(source, target, ec);
        if (ec) return fail("create", target.file, ec);
        verbose("create", target.file);
    }

    if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
    {
        if (create)
        {
            target.file = io::file{target.parent, target.name, ec};
            if (ec) return fail("access", target.file, ec);
        }

        apply_attrs(source.file, target.file, ec);
        if (ec)
        {
            if (is_attr_error(ec)) attr_fail("attrs", target.file, ec);
            else return fail("attrs", target.file, ec);
        }
        else verbose("attrs", target.file);
    }

    ctx.files_copied.fetch_add(1, std::memory_order_relaxed);

    if (ctx.move)
    {
        io::remove(source.parent, source.name, ec);
        if (ec) fail("remove", source.file, ec);
    }

    return create ? status::copied : status::unchanged;
}

auto copy_dispatch(asio::thread_pool& pool, node source, node target, bool top_level)
{
    if (target.file == source.file) return skip("skipping same file", source.file, target.file);

    switch (source.file.type())
    {
        case io::file_type::regular:
            return copy_regular_file(pool, std::move(source), std::move(target));

        case io::file_type::directory:
            return copy_directory(std::move(source), std::move(target));

        case io::file_type::symlink:
        {
            std::error_code ec;
            auto p = source.file.get_target_path(ec);
            if (ec) return fail("read symlink", source.file, ec);

            return copy_generic(std::move(source), std::move(target),
                [&](auto&& s, auto&& t) { return t.file.get_target_path(ec) == p; },
                [&](auto&& s, auto&& t, std::error_code& ec) { io::create_symlink(t.parent, t.name, p, ec); }
            );
        }

        case io::file_type::block:
            if (ctx.keep_devices)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) {
                        return s.file.type() == t.file.type() && s.file.device_type() == t.file.device_type();
                    },
                    [](auto&& s, auto&& t, std::error_code& ec) {
                        io::create_block_device(t.parent, t.name, s.file.device_type(), ec);
                    }
                );
            else if (top_level)
                return copy_regular_file(pool, std::move(source), std::move(target));
            else return skip("skipping block dev", source.file);

        case io::file_type::character:
            if (ctx.keep_devices)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) {
                        return s.file.type() == t.file.type() && s.file.device_type() == t.file.device_type();
                    },
                    [](auto&& s, auto&& t, std::error_code& ec) {
                        io::create_char_device(t.parent, t.name, s.file.device_type(), ec);
                    }
                );
            else if (top_level)
                return copy_regular_file(pool, std::move(source), std::move(target));
            else return skip("skipping char dev", source.file);

        case io::file_type::fifo:
            if (ctx.keep_special)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) { return s.file.type() == t.file.type(); },
                    [](auto&& s, auto&& t, std::error_code& ec) { io::create_fifo(t.parent, t.name, ec); }
                );
            else if (top_level)
                return copy_regular_file(pool, std::move(source), std::move(target));
            else return skip("skipping fifo", source.file);

        case io::file_type::socket:
            if (ctx.keep_special)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) { return s.file.type() == t.file.type(); },
                    [](auto&& s, auto&& t, std::error_code& ec) { io::create_socket(t.parent, t.name, ec); }
                );
            else if (top_level)
                return copy_regular_file(pool, std::move(source), std::move(target));
            else return skip("skipping socket", source.file);

        case io::file_type::not_found:
            return fail("non-extant", source.file);

        default: return fail("unknown file", source.file);
    }
}

////////////////////////////////////////////////////////////////////////////////
void copy_tree(asio::thread_pool& pool, node source, node target, bool top_level)
{
    if (source.file.is_directory())
    {
        if (!ctx.recursive) { skip("skipping dir", source.file); return; }

        std::error_code ec;
        // pass copies of source and target, as we need them below
        auto status = copy_dispatch(pool, source, target, top_level);

        switch (status)
        {
            case status::copied:
                // reread target, as it has changed
                target.file = source.file.is_symlink()
                    ? io::file{target.parent, target.name, ec}
                    : io::file{target.parent, target.name, io::follow_symlinks, ec};
                if (ec) { fail("access", target.file, ec); return; }
                // fallthrough

            case status::unchanged:
                for (auto&& name : io::directory_iterator(source.file))
                    if (name)
                    {
                        if (ctx.quit.load(std::memory_order_relaxed)) break;

                        node child_source{ .parent = source.file, .name = *name };
                        child_source.file = ctx.keep_links
                            ? io::file{source.file, *name, ec}
                            : io::file{source.file, *name, io::follow_symlinks, ec};
                        if (ec) { fail("access", child_source.file, ec); continue; }

                        node child_target{ .parent = target.file, .name = *name };
                        child_target.file = child_source.file.is_symlink()
                            ? io::file{target.file, *name, ec}
                            : io::file{target.file, *name, io::follow_symlinks, ec};
                        if (ec) { fail("access", child_target.file, ec); continue; }

                        copy_tree(pool, std::move(child_source), std::move(child_target), false);
                    }
                    else fail("read dir", source.file, name.error());

            default:;
        }
    }
    else copy_dispatch(pool, std::move(source), std::move(target), top_level);
}

void copy_sources(asio::thread_pool& pool, std::vector<node> sources, node target)
{
    if (target.file.is_directory())
    {
        for (auto&& source : sources)
        {
            if (ctx.quit.load(std::memory_order_relaxed)) break;

            if (source.name.has_filename()) // rsync-style behavior
            {
                std::error_code ec;
                auto name = source.name.filename();

                node new_target{ .parent = target.file, .name = name };
                new_target.file = source.file.is_symlink()
                    ? io::file{target.file, name, ec}
                    : io::file{target.file, name, io::follow_symlinks, ec};
                if (ec) { fail("access", new_target.file, ec); continue; }

                copy_tree(pool, std::move(source), std::move(new_target), true);
            }
            else
            {
                // pass copy of the target, we still need it
                copy_tree(pool, std::move(source), target, true);
            }
        }
    }
    else if (sources.size() == 1)
    {
        if (sources.front().file.is_symlink()) // --keep-links
        {
            std::error_code ec;
            target.file = io::file{target.parent, target.name, ec};
            if (ec) { fail("access", target.file, ec); return; }
        }
        copy_tree(pool, std::move(sources.front()), std::move(target), true);
    }
    else if (sources.size() > 1)
        fail("copy", target.file, std::make_error_code(std::errc::not_a_directory));
}

void process_dirs()
{
    std::error_code ec;
    for (auto&& [source, target] : std::views::reverse(ctx.dir_attrs))
    {
        apply_attrs(source, target, ec);
        if (ec)
        {
            if (is_attr_error(ec)) attr_fail("attrs", target, ec);
            else { fail("attrs", target, ec); continue; }
        }
        else verbose("attrs", target);

        ctx.files_copied.fetch_add(1, std::memory_order_relaxed);
    }

    for (auto&& [parent, name] : std::views::reverse(ctx.rmdirs))
    {
        io::remove_directory(parent, name, ec);
        if (ec) fail("remove dir", parent.path() / name, ec);
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

void show_progress(bool final = false)
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

    print(replace, "{}{}\n", bar, metric);
}

////////////////////////////////////////////////////////////////////////////////
extern "C" void signal_handler(int signal)
{
    ctx.exit_signal = signal;
    ctx.quit = true;
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
        const io::file cwd;

        std::vector<node> sources;
        node target{ .parent = cwd };

        for (auto&& path : args["SOURCE"].values())
        {
            node source{ .parent = cwd, .name = path };
            source.file = ctx.keep_links
                ? io::file{source.name, ec}
                : io::file{source.name, io::follow_symlinks, ec};
            if (ec) fail("access", source.file, ec);
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
                node source{ .parent = cwd, .name = destination_path.value() };
                source.file = ctx.keep_links
                    ? io::file{source.name, ec}
                    : io::file{source.name, io::follow_symlinks, ec};
                if (ec) fail("access", source.file, ec);
                else sources.push_back(std::move(source));
            }

            target.name = target_path.value();
            target.file = io::file{target.name, io::follow_symlinks, ec};
            if (ec) throw io::exception{"main", target.name, ec};
        }
        else if (destination_path)
        {
            target.name = destination_path.value();
            target.file = io::file{target.name, io::follow_symlinks, ec};
            if (ec) throw io::exception{"main", target.name, ec};
        }
        else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};

        ////////////////////
        asio::thread_pool pool{ ctx.jobs };

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        io::raise_open_file_limit();

        std::future<void> progress;
        if (ctx.progress) progress = std::async(std::launch::async, []
        {
            while (!ctx.quit.load(std::memory_order_relaxed))
            {
                std::this_thread::sleep_for(100ms);
                show_progress();
            }
        });

        copy_sources(pool, std::move(sources), std::move(target));
        pool.join();

        // don't process dirs on Ctrl+C
        if (!ctx.quit.exchange(true)) process_dirs();

        if (auto signal = ctx.exit_signal.exchange(0))
        {
            info("received signal " + std::to_string(signal) + ", exiting");
            code = interrupted;
        }
        else
        {
            if (ctx.failed) code = copy_failed;
            else if (ctx.attr_failed) code = attr_failed;

            if (ctx.attr_failed) info("some attrs could not be preserved");
        }

        if (ctx.progress)
        {
            progress.wait();
            show_progress(true); // final status
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
