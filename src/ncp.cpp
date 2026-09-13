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
#include <cstdio> // std::getchar
#include <exception>
#include <format>
#include <future>
#include <optional>
#include <print>
#include <ranges> // std::views::reverse
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
enum class status { failed, copied, moved, unchanged, skipped };
enum class unlink { never, always, auto_ };
enum class update { none, all, older, changed, size, };

struct context
{
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
    std::optional<asio::thread_pool> pool;
    std::optional<std::counting_semaphore<>> semaphore;

    std::atomic<int> exit_signal{0};
    std::atomic<bool> exit{ false };
    inline bool exiting() noexcept { return exit.load(std::memory_order_relaxed); }

    std::atomic<bool> failed{ false }, attr_failed{ false };
    bool copy_all = false, skip_all = false;

    std::atomic<int> files_total{0}, files_copied{0};
    std::atomic<io::file_size> bytes_total{0}, bytes_copied{0};

    inline void add_files_total(int n) noexcept { files_total.fetch_add(n, std::memory_order_relaxed); }
    inline void add_files_copied(int n) noexcept { files_copied.fetch_add(n, std::memory_order_relaxed); }

    inline void add_bytes_total(io::file_size b) noexcept { bytes_total.fetch_add(b, std::memory_order_relaxed); }
    inline void add_bytes_copied(io::file_size b) noexcept { bytes_copied.fetch_add(b, std::memory_order_relaxed); }

    inline void add_files_bytes_total(int n, io::file_size b) noexcept { add_files_total(n); add_bytes_total(b); }
    inline void add_files_bytes_copied(int n, io::file_size b) noexcept { add_files_copied(n); add_bytes_copied(b); }

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
    io::file parent;
    io::path name;
    io::file file;
};

////////////////////////////////////////////////////////////////////////////////
namespace std
{
template <> struct formatter<node> : formatter<string_view> {
    auto format(auto&& n, auto& ctx) const { return formatter<string_view>::format(n.file.path().string(), ctx); }
};
}

constexpr auto E = "E:";
constexpr auto I = "I:";
constexpr auto V = "V:";
constexpr auto W = "W:";

auto fail(auto&&... args)
{
    message(E, std::forward<decltype (args)>(args)...);
    ctx.failed.store(true, std::memory_order_relaxed);
    return status::failed;
}

void verbose(auto&&... args) { if (ctx.verbose) message(V, std::forward<decltype (args)>(args)...); }

bool confirm(std::string_view action, const node& target)
{
    if (ctx.copy_all) return true;
    if (ctx.skip_all) return false;

    for (auto lock = get_print_lock();;)
    {
        print_locked(retain, "{} '{}'? [Y/n/a/s/q] ", action, target.file.path().string());

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
            case 'q': case 'Q': ctx.exit = true; return false;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
bool copy_file(const node& source, const node& target, const io::progress_callback& cb)
{
    std::error_code ec;
    io::copy_file(source.file, target.parent, target.name, ec, cb);

    if (ec)
    {
        fail("copy", source.file.path().string(), target.file.path().string(), ec);
        return false;
    }
    else
    {
        if (ctx.verbose) message(V, "copy", source.file.path().string(), target.file.path().string());
        return true;
    }
}

bool remove_file(const node& node)
{
    std::error_code ec;
    io::remove(node.parent, node.name, ec);
    if (ec)
    {
        fail("remove", node.file.path().string());
        return false;
    }
    else return true;
}

bool rename_file(const node& source, const node& target)
{
    std::error_code ec;
    io::rename(source.parent, source.name, target.parent, target.name, ec);
    if (!ec)
    {
        if (ctx.verbose) message(V, "move", source.file.path().string(), target.file.path().string());
        return true;
    }
    else return false;
}

void apply_attr(std::string_view type,
    const io::file& source, io::file& target, std::error_code& ec, auto&& apply)
{
    constexpr auto not_permitted = std::errc::operation_not_permitted;
    constexpr auto not_supported = std::errc::not_supported;

    std::error_code ed;
    apply(source, target, ed);
    if (ed)
    {
        if (ed == not_permitted || ed == not_supported)
        {
            if (ctx.verbose) message(W, type, target.path().string(), ed);
            ctx.attr_failed.store(true, std::memory_order_relaxed);
        }
        else if (!ec) fail(type, target.path().string(), ec = ed);
    }
}

bool apply_attrs(const io::file& source, io::file& target, bool verbose = false)
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
                auto new_mode = mode & ~(io::mode::set_uid | io::mode::set_gid);
                if (new_mode != mode)
                {
                    mode = new_mode;
                    ctx.attr_failed.store(true, std::memory_order_relaxed);
                }
            }
        }
        else uid = source.user_id();
    }
    if (ctx.keep_group) gid = source.group_id();

    std::error_code ec;

    // owner must be first, as it will strip suid/sgid bits; time must be last
    if (ctx.keep_user || ctx.keep_group) apply_attr("owner", source, target, ec,
        [uid, gid](auto&&, auto&& t, std::error_code& ed) { t.owner(uid, gid, ed); }
    );
    if (ctx.keep_mode) apply_attr("mode", source, target, ec,
        [mode](auto&&, auto&& t, std::error_code& ed) { t.mode(mode, ed); }
    );
    if (ctx.keep_time) apply_attr("time", source, target, ec,
        [](auto&& s, auto&& t, std::error_code& ed) { t.time(s.time(), ed); }
    );

    if (!ec)
    {
        if (verbose && ctx.verbose) message(V, "attrs", target.path().string());
        return true;
    }
    else return false;
}

auto post_copy_file(node source, node target)
{
    ctx.semaphore->acquire();
    asio::post(*ctx.pool, [source = std::move(source), target = std::move(target)] mutable
    {
        struct scope_exit { ~scope_exit() { ctx.semaphore->release(); } } guard;

        if (ctx.exiting()) return;

        if (!copy_file(source, target, source.file.size()
            ? [](io::file_size b) { ctx.add_bytes_copied(b); return !ctx.exiting(); }
            : [](io::file_size b) { ctx.add_bytes_total(b); ctx.add_bytes_copied(b); return !ctx.exiting(); }
        )) return;

        if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
        {
            std::error_code ec;
            target.file = io::file{target.parent, target.name, ec};
            if (ec) { fail("access", target, ec); return; }

            if (!apply_attrs(source.file, target.file)) return;
        }

        ctx.add_files_copied(1);
        if (ctx.move && source.file.is_regular_file()) remove_file(source);
    });

    return status::copied;
}

auto copy_top_level(node source, node target)
{
    std::error_code ec;

    if (target.file)
    {
        if (target.file.is_directory() || ctx.unlink_ == unlink::always)
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target);
            if (ctx.interactive && !confirm("replace", target)) return status::skipped;

            if (!remove_file(target)) return status::failed;
        }
        else
        {
            if (ctx.update_ == update::none) return status::unchanged;
            if (ctx.interactive && !confirm("overwrite", target)) return status::skipped;
        }
    }

    ctx.add_files_bytes_total(1, source.file.size());

    return post_copy_file(std::move(source), std::move(target));
}

auto copy_regular_file(node source, node target)
{
    std::error_code ec;
    bool create = false;

    if (target.file)
    {
        if (!target.file.is_regular_file() || ctx.unlink_ == unlink::always)
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target);
            if (ctx.interactive && !confirm("overwrite", target)) return status::skipped;

            if (!remove_file(target)) return status::failed;
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

            if (create) {
                if (ctx.interactive && !confirm("overwrite", target)) return status::skipped;
            }
            else if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group) {
                if (ctx.interactive && !confirm("update", target)) return status::skipped;
            }
            else return status::unchanged;
        }
    }
    else create = true;

    ctx.add_files_bytes_total(1, source.file.size());

    if (create)
    {
        if (ctx.move && rename_file(source, target))
        {
            ctx.add_files_bytes_copied(1, source.file.size());
            return status::moved;
        }
        else return post_copy_file(std::move(source), std::move(target));
    }
    else // already checked ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group
    {
        if (!apply_attrs(source.file, target.file, true)) return status::failed;

        ctx.add_files_bytes_copied(1, source.file.size());
        if (ctx.move) remove_file(source);

        return status::unchanged;
    }
}

auto copy_directory(node source, node target)
{
    std::error_code ec;
    bool create = false;

    if (target.file)
    {
        if (!target.file.is_directory())
        {
            if (ctx.unlink_ == unlink::never) return fail("exists", target);
            if (ctx.interactive && !confirm("replace", target)) return status::skipped;

            if (!remove_file(target)) return status::failed;
            create = true;
        }
        else if (ctx.update_ == update::none) return status::unchanged;
    }
    else create = true;

    ctx.add_files_total(1);

    if (create)
    {
        if (ctx.move && rename_file(source, target))
        {
            ctx.add_files_copied(1);
            return status::moved;
        }

        io::create_directory(target.parent, target.name, ec);
        if (ec) return fail("create dir", target, ec);
        else verbose("create dir", target);
    }

    if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
    {
        if (create)
        {
            target.file = io::file{target.parent, target.name, ec};
            if (ec) return fail("access", target, ec);
        }

        ctx.dir_attrs.emplace_back(std::move(source.file), std::move(target.file));
    }
    else ctx.add_files_copied(1);

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
            if (ctx.unlink_ == unlink::never) return fail("exists", target);
            if (ctx.interactive && !confirm("replace", target)) return status::skipped;

            if (!remove_file(target)) return status::failed;
            create = true;
        }
        else if (ctx.update_ == update::none) return status::unchanged;
    }
    else create = true;

    ctx.add_files_total(1);

    if (create)
    {
        if (ctx.move && rename_file(source, target))
        {
            ctx.add_files_copied(1);
            return status::moved;
        }

        create_fn(source, target, ec);
        if (ec) return fail("create", target, ec);
        else verbose("create", target);
    }

    if (ctx.keep_time || ctx.keep_mode || ctx.keep_user || ctx.keep_group)
    {
        if (create)
        {
            target.file = io::file{target.parent, target.name, ec};
            if (ec) return fail("access", target, ec);
        }

        if (!apply_attrs(source.file, target.file, !create)) return status::failed;
    }

    ctx.add_files_copied(1);
    if (ctx.move) remove_file(source);

    return create ? status::copied : status::unchanged;
}

auto copy_dispatch(node source, node target, bool top_level)
{
    if (target.file == source.file)
    {
        message(I, "skipping same file", source.file.path().string(), target.file.path().string());
        return status::skipped;
    }

    switch (source.file.type())
    {
        case io::file_type::regular:
            if (top_level && (target.file.is_device() || target.file.is_special()))
                return copy_top_level(std::move(source), std::move(target));
            else return copy_regular_file(std::move(source), std::move(target));

        case io::file_type::directory:
            return copy_directory(std::move(source), std::move(target));

        case io::file_type::symlink:
        {
            std::error_code ec;
            auto p = source.file.get_target_path(ec);
            if (ec) return fail("read symlink", source, ec);

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
            else if (!top_level)
            {
                message(I, "skipping block dev", source.file.path().string());
                return status::skipped;
            }
            else return copy_top_level(std::move(source), std::move(target));

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
            else if (!top_level)
            {
                message(I, "skipping char dev", source);
                return status::skipped;
            }
            else return copy_top_level(std::move(source), std::move(target));

        case io::file_type::fifo:
            if (ctx.keep_special)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) { return s.file.type() == t.file.type(); },
                    [](auto&& s, auto&& t, std::error_code& ec) { io::create_fifo(t.parent, t.name, ec); }
                );
            else if (!top_level)
            {
                message(I, "skipping fifo", source.file.path().string());
                return status::skipped;
            }
            else return copy_top_level(std::move(source), std::move(target));

        case io::file_type::socket:
            if (ctx.keep_special)
                return copy_generic(std::move(source), std::move(target),
                    [](auto&& s, auto&& t) { return s.file.type() == t.file.type(); },
                    [](auto&& s, auto&& t, std::error_code& ec) { io::create_socket(t.parent, t.name, ec); }
                );
            else if (!top_level)
            {
                message(I, "skipping socket", source.file.path().string());
                return status::skipped;
            }
            else return copy_top_level(std::move(source), std::move(target));

        case io::file_type::not_found:
            return fail("non-extant", source);

        default: return fail("unknown file", source);
    }
}

////////////////////////////////////////////////////////////////////////////////
void copy_tree(node source, node target, bool top_level)
{
    if (source.file.is_directory())
    {
        if (!ctx.recursive)
        {
            message(I, "skipping dir", source.file.path().string());
            return;
        }

        std::error_code ec;
        // pass copies of source and target, as we need them below
        auto status = copy_dispatch(source, target, top_level);

        switch (status)
        {
            case status::copied:
                // reread target, as it has changed
                target.file = source.file.is_symlink()
                    ? io::file{target.parent, target.name, ec}
                    : io::file{target.parent, target.name, io::follow_symlinks, ec};
                if (ec) { fail("access", target, ec); return; }
                // fallthrough

            case status::unchanged:
                for (auto&& name : io::directory_iterator(source.file))
                    if (name)
                    {
                        if (ctx.exiting()) break;

                        node child_source{ .parent = source.file, .name = *name };
                        child_source.file = ctx.keep_links
                            ? io::file{source.file, *name, ec}
                            : io::file{source.file, *name, io::follow_symlinks, ec};
                        if (ec) { fail("access", child_source, ec); continue; }

                        node child_target{ .parent = target.file, .name = *name };
                        child_target.file = child_source.file.is_symlink()
                            ? io::file{target.file, *name, ec}
                            : io::file{target.file, *name, io::follow_symlinks, ec};
                        if (ec) { fail("access", child_target, ec); continue; }

                        copy_tree(std::move(child_source), std::move(child_target), false);
                    }
                    else fail("read dir", source, name.error());

            default:;
        }
    }
    else copy_dispatch(std::move(source), std::move(target), top_level);
}

void copy_sources(std::vector<node> sources, node target)
{
    if (target.file.is_directory())
    {
        for (auto&& source : sources)
        {
            if (ctx.exiting()) break;

            if (source.name.has_filename()) // rsync-style behavior
            {
                std::error_code ec;
                auto name = source.name.filename();

                node new_target{ .parent = target.file, .name = name };
                new_target.file = source.file.is_symlink()
                    ? io::file{target.file, name, ec}
                    : io::file{target.file, name, io::follow_symlinks, ec};
                if (ec) { fail("access", new_target, ec); continue; }

                copy_tree(std::move(source), std::move(new_target), true);
            }
            else
            {
                // pass copy of the target, we still need it
                copy_tree(std::move(source), target, true);
            }
        }
    }
    else if (sources.size() == 1)
    {
        if (sources.front().file.is_symlink()) // --keep-links
        {
            std::error_code ec;
            target.file = io::file{target.parent, target.name, ec};
            if (ec) { fail("access", target, ec); return; }
        }
        copy_tree(std::move(sources.front()), std::move(target), true);
    }
    else if (sources.size() > 1)
        fail("copy", target, std::make_error_code(std::errc::not_a_directory));
}

void process_dirs()
{
    std::error_code ec;
    for (auto&& [source, target] : std::views::reverse(ctx.dir_attrs))
    {
        if (!apply_attrs(source, target, true)) continue;
        ctx.add_files_copied(1);
    }

    for (auto&& [parent, name] : std::views::reverse(ctx.rmdirs))
    {
        io::remove_directory(parent, name, ec);
        if (ec) fail("remove dir", (parent.path() / name).string(), ec);
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
    if (ctx.exiting()) ctx.percent_copied = percent;
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

        auto threads = 1;
        if (auto&& jobs = args["--jobs"])
        {
            threads = parse(jobs.value()).value_or(-1);
            if (threads < 1 || threads > 16) throw pgm::invalid_argument{ "bad --jobs value '" + jobs.value() + "'"};
        }
        ctx.pool.emplace(threads);

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
            if (ec) fail("access", source, ec);
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
                if (ec) fail("access", source, ec);
                else sources.push_back(std::move(source));
            }

            target.name = target_path.value();
            target.file = io::file{target.name, io::follow_symlinks, ec};
            if (ec) throw io::exception{"access", target.name, ec};
        }
        else if (destination_path)
        {
            target.name = destination_path.value();
            target.file = io::file{target.name, io::follow_symlinks, ec};
            if (ec) throw io::exception{"access", target.name, ec};
        }
        else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};

        ////////////////////
        io::set_signal_callback([](int signal) { ctx.exit_signal = signal; ctx.exit = true; });

        auto max = io::max_open_file_limit(ec);
        if (!ec) io::set_open_file_limit(max, ec);

        ctx.semaphore.emplace(max / 5); // 4 desc per task @ 80% capacity

        std::future<void> progress;
        if (ctx.progress) progress = std::async(std::launch::async, []
        {
            while (!ctx.exiting())
            {
                std::this_thread::sleep_for(100ms);
                show_progress();
            }
        });

        copy_sources(std::move(sources), std::move(target));
        ctx.pool->join();

        // don't process dirs on Ctrl+C
        if (!ctx.exit.exchange(true)) process_dirs();

        if (auto signal = ctx.exit_signal.exchange(0))
        {
            message(I, "received signal " + std::to_string(signal) + ", exiting");
            code = interrupted;
        }
        else
        {
            if (ctx.failed) code = copy_failed;
            else if (ctx.attr_failed) code = attr_failed;

            if (ctx.attr_failed) message(W, "some attrs could not be preserved");
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
    message(E, "access", e.path1().string(), e.code());
    return invalid_argument;
}
catch (const std::exception& e)
{
    message(E, e.what());
    return invalid_argument;
};
