////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io/file.hpp"
#include "io/misc.hpp"
#include "options.hpp"
#include "pgm/args.hpp"
#include "printer.hpp"

#include <asio.hpp>
#include <atomic>
#include <charconv> // std::from_chars
#include <cstdio> // std::getchar
#include <exception>
#include <format>
#include <future>
#include <map>
#include <optional>
#include <print>
#include <ranges> // std::views::reverse
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
options o;
printer p;

std::optional<asio::thread_pool> pool;
std::optional<std::counting_semaphore<>> semaphore;

std::atomic<int> exit_signal{0};
std::atomic<bool> exit_{ false };
inline bool exiting() noexcept { return exit_.load(std::memory_order_relaxed); }

std::atomic<bool> failed{ false }, attrs_failed{ false };

void fail(auto&&... args)
{
    p.print_error(std::forward<decltype (args)>(args)...);
    failed.store(true, std::memory_order_relaxed);
}

void verbose(auto&&... args) {
    if (o.verbose) p.print_verbose(std::forward<decltype (args)>(args)...);
}

struct node
{
    io::file parent;
    io::path name;
    io::file file;

    node() noexcept = default;
    node(io::file parent, io::path name, bool follow_links = true) noexcept :
        parent{std::move(parent)}, name{std::move(name)}
    { reopen(follow_links); }

    bool reopen(bool follow_links) noexcept
    {
        std::error_code ec;
        file = io::file{parent, name, follow_links ? io::follow_links : io::no_follow_links, ec};
        if (!ec) return true;

        fail("access", file.path(), ec);
        return false;
    }

    auto empty() const noexcept { return file.empty(); }
};

std::vector<std::tuple<node, node>> dir_attrs;
std::vector<node> rmdirs;

using file_id = std::tuple<io::device, io::index_node>;
std::map<file_id, std::vector<std::tuple<node, node>>> hardlinks;

////////////////////////////////////////////////////////////////////////////////
bool confirm(std::string_view action, const node& target)
{
    if (o.copy_all) return true;
    if (o.skip_all) return false;

    for (auto lock = p.get_print_lock();;)
    {
        p.print_locked(retain, "{} '{}'? [Y/n/a/s/q] ", action, target.file.path());

        auto c = std::getchar();
        auto reply = c;
        while (c != '\n' && c != EOF) c = std::getchar();

        switch (reply)
        {
            case 'y': case 'Y': case '\n': return true;
            case 'n': case 'N': return false;

            case 'a': case 'A': o.copy_all = true; return true;
            case 's': case 'S': o.skip_all = true; return false;

            case EOF: std::print("q\n");
            case 'q': case 'Q': exit_ = true; return false;
        }
    }
}

bool copy_file(const node& source, const node& target, const io::copy_callback& cb)
{
    std::error_code ec;
    io::copy_file(source.file, target.parent, target.name, ec, cb);

    if (ec == std::errc::permission_denied && o.unlink == unlink::force && target.file.is_regular_file())
    {
        std::error_code ed;
        io::remove(target.parent, target.name, ed);
        if (!ed) io::copy_file(source.file, target.parent, target.name, ec, cb);
    }

    if (ec) { fail("copy", source.file.path(), target.file.path(), ec); return false; }
    else { verbose("copy", source.file.path(), target.file.path()); return true; }
}

bool create_generic(std::string_view type, const node& node, auto&& create_fn)
{
    std::error_code ec;
    create_fn(node, ec);

    if (ec) { fail(type, node.file.path(), ec); return false; }
    else { verbose(type, node.file.path()); return true; }
}

bool remove_file(const node& node)
{
    std::error_code ec;
    io::remove(node.parent, node.name, ec);
    if (!ec) return true; // quiet success

    fail("remove", node.file.path(), ec);
    return false;
}

bool rename_file(const node& source, const node& target)
{
    std::error_code ec;
    io::rename(source.parent, source.name, target.parent, target.name, ec);
    if (ec) return false; // quiet failure

    verbose("move", source.file.path(), target.file.path());
    return true;
}

void apply_attr(std::string_view type, const node& source, node& target, std::error_code& ec, auto&& apply_fn)
{
    constexpr auto not_permitted = std::errc::operation_not_permitted;
    constexpr auto not_supported = std::errc::not_supported;

    std::error_code ed;
    apply_fn(source.file, target.file, ed);
    if (ed)
    {
        if (ed == not_permitted || ed == not_supported)
        {
            if (o.verbose) p.print_warn(type, target.file.path(), ed);
            attrs_failed.store(true, std::memory_order_relaxed);
        }
        else if (!ec) fail(type, target.file.path(), ec = ed);
    }
}

bool apply_attrs(const node& source, node& target, bool announce = false)
{
    io::mode mode = source.file.mode();

    constexpr auto none = -1;
    io::user_id uid = none; io::group_id gid = none;

    if (o.keep_user)
    {
        if (!o.can_chown && source.file.user_id() != uid)
        {
            if (o.keep_mode)
            {
                auto new_mode = mode & ~(io::mode::set_uid | io::mode::set_gid);
                if (new_mode != mode)
                {
                    mode = new_mode;
                    attrs_failed.store(true, std::memory_order_relaxed);
                }
            }
        }
        else uid = source.file.user_id();
    }
    if (o.keep_group) gid = source.file.group_id();

    std::error_code ec;

    // owner must be first, as it will strip suid/sgid bits; time must be last
    if (o.keep_user || o.keep_group) apply_attr("owner", source, target, ec,
        [uid, gid](auto&&, auto&& tgt, std::error_code& ed) { tgt.owner(uid, gid, ed); }
    );
    if (o.keep_mode) apply_attr("mode", source, target, ec,
        [mode](auto&&, auto&& tgt, std::error_code& ed) { tgt.mode(mode, ed); }
    );
    if (o.keep_acl) apply_attr("acl", source, target, ec,
        [](auto&& src, auto&& tgt, std::error_code& ed) {
            auto acl = io::get_acl(src, ed); if (!ed) io::set_acl(tgt, acl, ed);
        }
    );
    if (o.keep_time) apply_attr("time", source, target, ec,
        [](auto&& src, auto&& tgt, std::error_code& ed) { tgt.time(src.time(), ed); }
    );
    if (ec) return false;

    if (announce) verbose("attrs", target.file.path());
    return true;
}

bool queue_hardlink(const node& source, const node& target)
{
    if (o.keep_hardlinks && source.file.hardlink_count() > 1)
    {
        auto [it, new_] = hardlinks.try_emplace({ source.file.device(), source.file.index_node() });
        it->second.emplace_back(source, target);
        return !new_;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
enum class status { failed, skipped, moved, success };

auto post_copy_file(node& source, node& target)
{
    semaphore->acquire();
    // we can steal source and target here
    asio::post(*pool, [source = std::move(source), target = std::move(target)] mutable
    {
        struct scope_exit { ~scope_exit() { semaphore->release(); } } guard;

        if (exiting()) return;

        if (!copy_file(source, target, source.file.size()
            ? [](io::file_size b) { p.add_bytes_copied(b); return !exiting(); }
            : [](io::file_size b) { p.add_bytes_total(b); p.add_bytes_copied(b); return !exiting(); }
        )) return;

        if (o.keep_attrs())
        {
            if (!target.reopen(io::follow_links)) return;
            if (!apply_attrs(source, target)) return;
        }

        p.add_files_copied(1);
        if (o.move) remove_file(source);
    });

    return status::success;
}

auto process_top_level(node& source, node& target)
{
    // called only in these cases:
    // * device/special => new
    // * device/special => file
    // * device/special => dir
    // * device/special => device/special
    // * regular => device/special

    if (target.file)
    {
        if (o.unlink == unlink::always)
        {
            if (!confirm("replace", target)) return status::skipped;
            if (!remove_file(target)) return status::failed;
        }
        else
        {
            if (o.update == update::none) return status::skipped;
            if (!confirm("overwrite", target)) return status::skipped;
        }
    }

    p.add_files_bytes_total(1, source.file.size());

    return post_copy_file(source, target);
}

auto process_file(node& source, node& target)
{
    bool copy = false;

    if (target.file)
    {
        if (o.unlink == unlink::always)
        {
            if (!confirm("replace", target)) return status::skipped;
            if (!remove_file(target)) return status::failed;
            copy = true;
        }
        else if (target.file.is_regular_file())
        {
            switch (o.update)
            {
                case update::none: return status::skipped;
                case update::older: // don't touch newer files
                    if (target.file.time() >= source.file.time()) return status::skipped;
                    copy = true;
                    break;
                case update::changed: 
                    copy = target.file.size() != source.file.size() || target.file.time() != source.file.time();
                    break;
                case update::size:
                    copy = target.file.size() != source.file.size();
                    break;
                case update::all: copy = true; break;
            }
            if (copy && !confirm("overwrite", target)) return status::skipped;
        }
        else
        {
            switch (o.update)
            {
                case update::none: return status::skipped;
                case update::older: // don't touch newer files
                    if (target.file.time() >= source.file.time()) return status::skipped;
                default: ;
            }
            if (o.unlink == unlink::never) {
                fail("exists", target.file.path()); return status::failed;
            }
            if (!confirm("replace", target)) return status::skipped;
            if (!remove_file(target)) return status::failed;
            copy = true;
        }
    }
    else copy = true;

    p.add_files_bytes_total(1, source.file.size());

    if (copy)
    {
        if (o.move && rename_file(source, target))
        {
            p.add_files_bytes_copied(1, source.file.size());
            return status::moved;
        }
        if (queue_hardlink(source, target)) return status::success;
        return post_copy_file(source, target);
    }
    else
    {
        if (queue_hardlink(source, target)) return status::success;
        if (o.keep_attrs() && !apply_attrs(source, target, true)) return status::failed;

        p.add_files_bytes_copied(1, source.file.size());
        if (o.move) remove_file(source);

        return status::success;
    }
}

auto process_directory(node& source, node& target)
{
    bool create = false;

    if (target.file)
    {
        if (!target.file.is_directory())
        {
            if (o.unlink == unlink::never) {
                fail("exists", target.file.path()); return status::failed;
            }
            if (!confirm("replace", target)) return status::skipped;
            if (!remove_file(target)) return status::failed;

            create = true;
        }
    }
    else create = true;

    p.add_files_total(1);

    if (create)
    {
        if (o.move && rename_file(source, target))
        {
            p.add_files_copied(1);
            return status::moved;
        }
        if (!create_generic("create dir", target, 
            [](auto&& node, std::error_code& ec) { io::create_directory(node.parent, node.name, ec); }
        )) return status::failed;

        if (!target.reopen(io::no_follow_links)) return status::failed;
    }

    if (o.keep_attrs()) dir_attrs.emplace_back(source, target);
    else p.add_files_copied(1);

    if (o.move) rmdirs.push_back(source);

    return status::success;
}

auto process_generic(node& source, node& target, std::string_view type, auto&& match_fn, auto&& create_fn)
{
    bool create = false;

    if (target.file)
    {
        if (o.unlink == unlink::always)
        {
            if (!confirm("replace", target)) return status::skipped;
            if (!remove_file(target)) return status::failed;
            create = true;
        }
        else
        {
            switch (o.update)
            {
                case update::none: return status::skipped;
                case update::older: // don't touch newer files
                    if (target.file.time() >= source.file.time()) return status::skipped;
                default: ;
            }
            if (!match_fn(source, target))
            {
                if (o.unlink == unlink::never) {
                    fail("exists", target.file.path()); return status::failed;
                }
                if (!confirm("replace", target)) return status::skipped;
                if (!remove_file(target)) return status::failed;
                create = true;
            }
        }
    }
    else create = true;

    p.add_files_total(1);

    if (create)
    {
        if (o.move && rename_file(source, target))
        {
            p.add_files_copied(1);
            return status::moved;
        }
        if (queue_hardlink(source, target)) return status::success;
        if (!create_generic(type, target, create_fn)) return status::failed;
    }
    else if (queue_hardlink(source, target)) return status::success;

    if (o.keep_attrs())
    {
        if (create && !target.reopen(io::no_follow_links)) return status::failed;
        if (!apply_attrs(source, target, !create)) return status::failed;
    }

    p.add_files_copied(1);
    if (o.move) remove_file(source);

    return status::success;
}

auto process_symlink(node& source, node& target)
{
    std::error_code ec;
    auto path = source.file.get_target_path(ec);
    if (ec) { fail("read symlink", source.file.path(), ec); return status::failed; }

    return process_generic(source, target, "symlink",
        [&path](auto&& src, auto&& tgt) {
            std::error_code ec;
            return src.file.type() == tgt.file.type() && tgt.file.get_target_path(ec) == path;
        },
        [&path](auto&& tgt, std::error_code& ec) {
            io::create_symlink(tgt.parent, tgt.name, path, ec);
        });
}

auto process_block_device(node& source, node& target)
{
    return process_generic(source, target, "create block",
        [](auto&& src, auto&& tgt) {
            return src.file.type() == tgt.file.type() && src.file.device_type() == tgt.file.device_type();
        },
        [rdev = source.file.device_type()](auto&& tgt, std::error_code& ec) {
            io::create_block_device(tgt.parent, tgt.name, rdev, ec);
        });
}

auto process_char_device(node& source, node& target)
{
    return process_generic(source, target, "create char",
        [](auto&& src, auto&& tgt) {
            return src.file.type() == tgt.file.type() && src.file.device_type() == tgt.file.device_type();
        },
        [rdev = source.file.device_type()](auto&& tgt, std::error_code& ec) {
            io::create_char_device(tgt.parent, tgt.name, rdev, ec);
        });
}

auto process_fifo(node& source, node& target)
{
    return process_generic(source, target, "create fifo",
        [](auto&& src, auto&& tgt) { return src.file.type() == tgt.file.type(); },
        [](auto&& tgt, std::error_code& ec) { io::create_fifo(tgt.parent, tgt.name, ec); });
}

auto process_socket(node& source, node& target)
{
    return process_generic(source, target, "create socket",
        [](auto&& src, auto&& tgt) { return src.file.type() == tgt.file.type(); },
        [](auto&& tgt, std::error_code& ec) { io::create_socket(tgt.parent, tgt.name, ec); });
}

auto dispatch(node& source, node& target, bool top_level)
{
    if (target.file == source.file)
    {
        p.print_info("skipping same file", source.file.path(), target.file.path());
        return status::skipped;
    }

    switch (source.file.type())
    {
        case io::file_type::regular:
            if (top_level && (target.file.is_device() || target.file.is_special()))
                return process_top_level(source, target);
            else return process_file(source, target);

        case io::file_type::directory:
            return process_directory(source, target);

        case io::file_type::symlink:
            return process_symlink(source, target);

        case io::file_type::block:
            if (o.keep_devices) return process_block_device(source, target);
            if (top_level && !o.move) return process_top_level(source, target);

            p.print_info("skipping block", source.file.path());
            return status::skipped;

        case io::file_type::character:
            if (o.keep_devices) return process_char_device(source, target);
            if (top_level && !o.move) return process_top_level(source, target);

            p.print_info("skipping char", source.file.path());
            return status::skipped;

        case io::file_type::fifo:
            if (o.keep_special) return process_fifo(source, target);
            if (top_level && !o.move) return process_top_level(source, target);

            p.print_info("skipping fifo", source.file.path());
            return status::skipped;

        case io::file_type::socket:
            if (o.keep_special) return process_socket(source, target);
            if (top_level && !o.move) return process_top_level(source, target);

            p.print_info("skipping socket", source.file.path());
            return status::skipped;

        case io::file_type::not_found:
            fail("not found", source.file.path());
            return status::failed;

        default: fail("unknown file", source.file.path());
            return status::failed;
    }
}

////////////////////////////////////////////////////////////////////////////////
void copy_tree(node& source, node& target, bool top_level)
{
    if (source.file.is_directory())
    {
        if (o.recursive)
        {
            if (dispatch(source, target, top_level) == status::success)
            {
                for (auto&& name : io::directory_iterator(source.file))
                    if (name)
                    {
                        if (exiting()) break;

                        node child_source{ source.file, *name, o.follow_links };
                        if (child_source.empty()) continue;

                        bool follow_target = o.follow_target && !source.file.is_symlink();
                        node child_target{ target.file, *name, follow_target };
                        if (child_target.empty()) continue;

                        copy_tree(child_source, child_target, false);
                    }
                    else fail("read dir", source.file.path(), name.error());
            }
        }
        else p.print_info("skipping dir", source.file.path());
    }
    else dispatch(source, target, top_level);
}

void copy_sources(std::vector<node>& sources, node& target)
{
    if (target.file.is_directory())
    {
        for (auto&& source : sources)
        {
            if (exiting()) break;

            if (source.name.has_filename()) // rsync-style behavior
            {
                bool follow_target = o.follow_target && !source.file.is_symlink();
                node new_target{ target.file, source.name.filename(), follow_target };
                if (new_target.empty()) continue;

                copy_tree(source, new_target, true);
            }
            else copy_tree(source, target, true); // no need to reopen - source is a dir (ends with /)
        }
    }
    else if (sources.size() == 1)
    {
        auto& source = sources.front();
        bool follow_target = o.follow_target && !source.file.is_symlink();
        if (!follow_target && !target.reopen(io::no_follow_links)) return;

        copy_tree(source, target, true);
    }
    else if (sources.size() > 1)
        fail("copy", target.file.path(), std::make_error_code(std::errc::not_a_directory));
}

void process_hardlinks()
{
    for (auto&& [id, hardlinks] : hardlinks)
    {
        auto it = hardlinks.begin();
        auto& [source, target] = *it;

        for (++it; it != hardlinks.end(); ++it)
        {
            auto& [link_source, link_target] = *it;

            std::error_code ec;
            io::create_hardlink(target.parent, target.name, link_target.parent, link_target.name, ec);
            if (ec == std::errc::file_exists)
            {
                remove_file(link_target);
                io::create_hardlink(target.parent, target.name, link_target.parent, link_target.name, ec);
            }
            if (!ec)
            {
                verbose("hardlink", target.file.path(), link_target.file.path(), ec);
                p.add_files_bytes_copied(1, source.file.size());
                if (o.move) remove_file(link_source);
            }
            else fail("hardlink", link_target.file.path(), ec);
        }
    }
}

void process_dirs()
{
    for (auto&& [source, target] : std::views::reverse(dir_attrs))
    {
        if (!apply_attrs(source, target, true)) continue;
        p.add_files_copied(1);
    }

    for (auto&& node : std::views::reverse(rmdirs))
    {
        std::error_code ec;
        io::remove_directory(node.parent, node.name, ec);
        if (ec) fail("remove dir", node.file.path(), ec);
    }
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
    partial_failed = 4,
};

int main(int argc, char* argv[])
try
{
    auto code = success;
    auto name = io::path{argv[0]}.filename().string();

    pgm::args args
    {
        { "-A", "--acl",            "Preserve ACL (access control list)."               },
        { "-a", "--archive",        "Archive mode (equivalent to -Dfmort)."             },
        { "-D",                     "Same as --special --devices."                      },
        {       "--devices",        "Preserve device files."                            },
        { "-f",                     "Same as --unlink=force."                           },
        { "-g", "--group",          "Preserve group ownership."                         },
        { "-H", "--hard-links",     "Preserve hard links."                              },
        { "-h", "--help",           "Show this help message and exit."                  },
        { "-i", "--interactive",    "Prompt before overwriting files."                  },
        { "-j", "--jobs", "N",      "Number of files to copy in parallel (max: 16)."    },
        { "-L", "--follow-links",   "Dereference source symlinks (default when non-recursive)." },
        { "-M", "--move",           "Remove source files after copying."                },
        { "-m", "--mode",           "Preserve file permissions (mode bits)."            },
        { "-n",                     "Same as --unlink=never."                           },
        { "-o", "--ownership",      "Same as --user --group."                           },
        { "-P", "--keep-links",     "Preserve source symlinks (default when recursive)."},
        { "-p", "--progress",       "Show progress bar."                                },
        { "-r", "--recursive",      "Copy directories recursively."                     },
        {       "--special",        "Preserve named pipes and sockets."                 },
        { "-T", "--target", "dir",  "Target directory to copy into."                    },
        { "-t", "--time",           "Preserve modification time."                       },
        { "-U",                     "Same as --update=older."                           },
        {       "--unlink", "when", pgm::optval,
                                    "Unlink destination before writing. [when] can be one of:\n"
                                    "'never', 'always', 'force' or 'auto'.\n"
                                    "If [when] is omitted, 'always' is assumed.\n"
                                    "If the option is omitted entirely, 'auto' is used."},
        {       "--update", "when", pgm::optval,
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
        o.uid = io::effective_user_id();
        o.can_chown = o.uid ? io::have_cap_chown() : true;

        if (args["--archive"])
        {
            o.keep_devices = true;
            o.keep_group = true;
            o.keep_mode  = true;
            o.keep_special = true;
            o.keep_time  = true;
            o.keep_user  = true;
            o.recursive  = true;
            o.unlink = unlink::force;
        }
        if (args["--acl"        ]) o.keep_acl = true;
        if (args["-D"           ]) o.keep_devices = o.keep_special = true;
        if (args["--devices"    ]) o.keep_devices = true;
        if (args["--group"      ]) o.keep_group = true;
        if (args["--hard-links" ]) o.keep_hardlinks = true;
        if (args["--interactive"]) o.copy_all = false;
        if (args["--mode"       ]) o.keep_mode = true;
        if (args["--ownership"  ]) o.keep_group = o.keep_user = true;
        if (args["--progress"   ]) o.progress = true;
        if (args["--special"    ]) o.keep_special = true;
        if (args["--time"       ]) o.keep_time = true;
        if (args["--user"       ]) o.keep_user = true;
        if (args["--verbose"    ]) o.verbose  = true;

        auto threads = 1;
        if (auto&& jobs = args["--jobs"])
        {
            threads = parse(jobs.value()).value_or(-1);
            if (threads < 1 || threads > 16) throw pgm::invalid_argument{ "bad --jobs value '" + jobs.value() + "'"};
        }
        pool.emplace(threads);

        if (args["--recursive"]) o.recursive = true;
        o.follow_links = !o.recursive; // don't follow symlinks in recursive mode by default

        auto&& follow_links = args["--follow-links"];
        auto&& keep_links = args["--keep-links"];

        auto&& move = args["--move"];
        bool nmv = (name == "nmv");

        if (follow_links && keep_links) throw pgm::invalid_argument{
            "'--follow-links' and '--keep-links' are mutually exclusive"
        };

        if (follow_links && nmv) throw pgm::invalid_argument{
            "'--follow-links' cannot be used with 'nmv'"
        };

        if (follow_links && move) throw pgm::invalid_argument{
            "'--follow-links' cannot be used with '--move'"
        };

        if (move || nmv)
        {
            o.follow_links = false; // don't follow symlinks when moving
            o.follow_target= false; //
            o.move = true;
            o.recursive = true; // turn on recursive mode when moving
        }
        else if (follow_links) o.follow_links = true;
        else if (keep_links) o.follow_links = false;

        auto&& unlink = args["--unlink"];
        auto&& f = args["-f"];
        auto&& n = args["-n"];

        if ((unlink && f) || (unlink && n) || (f && n)) throw pgm::invalid_argument{
            "'--unlink', '-f' and '-n' are mutually exclusive"
        };

        if (unlink)
        {
            auto&& when = unlink.value();
            if (when == "never") o.unlink = unlink::never;
            else if (when.empty() || when == "always") o.unlink = unlink::always;
            else if (when == "force") o.unlink = unlink::force;
            else if (when == "auto") o.unlink = unlink::auto_;
            else throw pgm::invalid_argument{ "bad --unlink value '" + when + "'" };
        }
        else if (f) o.unlink = unlink::force;
        else if (n) o.unlink = unlink::never;

        auto&& update = args["--update"];
        auto&& U = args["-U"];

        if (update && U) throw pgm::invalid_argument{
            "'--update' and '-U' are mutually exclusive"
        };

        if (update)
        {
            auto&& when = update.value();
            if (when == "none") o.update = update::none;
            else if (when == "all") o.update = update::all;
            else if (when.empty() || when == "older") o.update = update::older;
            else if (when == "changed") o.update = update::changed;
            else if (when == "size") o.update = update::size;
            else throw pgm::invalid_argument{ "bad --update value '" + when + "'" };
        }
        else if (U) o.update = update::older;

        const io::file cwd;
        std::vector<node> sources;
        node target;

        for (auto&& path : args["SOURCE"].values())
        {
            node source{ cwd, path, o.follow_links };
            if (!source.empty()) sources.push_back(std::move(source));
        }

        auto&& destination = args["DESTINATION"];
        auto&& target_dir = args["--target"];

        if (target_dir)
        {
            // DESTINATION will capture the last positional parameter,
            // but if --target was specified that value belongs in SOURCES
            if (destination)
            {
                node source{ cwd, destination.value(), o.follow_links };
                if (!source.empty()) sources.push_back(std::move(source));
            }

            target = node{ cwd, target_dir.value(), io::follow_links };
            if (target.empty()) throw pgm::invalid_argument{"target path"};
        }
        else if (destination)
        {
            target = node{ cwd, destination.value(), io::follow_links };
            if (target.empty()) throw pgm::invalid_argument{"destination path"};
        }
        else throw pgm::missing_argument{"neither DESTINATION nor --target was specified"};

        ////////////////////
        io::set_signal_callback([](int signal) { exit_signal = signal; exit_ = true; });

        std::error_code ec;
        auto max = io::max_open_file_limit(ec);
        if (!ec) io::set_open_file_limit(max, ec);

        semaphore.emplace(max / 5); // 4 desc per task @ 80% capacity

        std::future<void> progress_task;
        if (o.progress) progress_task = std::async(std::launch::async, []
        {
            while (!exiting())
            {
                std::this_thread::sleep_for(100ms);
                p.progress();
            }
        });

        copy_sources(sources, target);
        pool->join();

        if (!exit_.exchange(true)) // don't process on Ctrl+C
        {
            process_hardlinks();
            process_dirs();
        }

        if (auto signal = exit_signal.exchange(0))
        {
            p.print_info(std::format("exiting - received signal {}", signal));
            code = interrupted;
        }
        else
        {
            if (failed) code = copy_failed;
            else if (attrs_failed) code = partial_failed;

            if (attrs_failed) p.print_warn("some attrs could not be preserved");
        }

        if (o.progress)
        {
            progress_task.wait();
            p.progress(final);
        }
    }

    return code;
}
catch (const io::exception& e)
{
    p.print_error("access", e.path1(), e.code());
    return invalid_argument;
}
catch (const std::exception& e)
{
    p.print_error(e.what());
    return invalid_argument;
};
