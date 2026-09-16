////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "io/misc.hpp"
#include "printer.hpp"

#include <array>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
namespace
{

auto format_bytes(io::file_size bytes)
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

}

////////////////////////////////////////////////////////////////////////////////
std::mutex printer::mutex{};

void printer::progress(report report)
{
    auto now = clock::now();

    auto ft = files_total.load(std::memory_order_relaxed);
    auto fc = files_copied.load(std::memory_order_relaxed);
    auto bt = bytes_total.load(std::memory_order_relaxed);
    auto bc = bytes_copied.load(std::memory_order_relaxed);

    auto pct = bt ? (100.0 * bc / bt) : 0;
    if (report == final) done.force(pct); else done = pct;

    if (auto delta = std::chrono::duration<double>{now - tick}.count())
    {
        speed = (bc - prev_bytes_copied) / delta;
        tick = now;
        prev_bytes_copied = bc;
    }

    elapse = std::chrono::duration_cast<seconds>(now - start_time);
    remain = seconds{ speed.value_or() ? static_cast<io::file_size>((bt - bc) / *speed) : 0 };

    ////////////////////
    constexpr auto min_bar_width = 10, max_bar_width = 30;
    constexpr auto b_x = 2; // "●" takes up 3 chars

    auto width = io::term_width();

    auto metric = std::format(" {}/{} ● {}/{}", fc, ft, format_bytes(bc), format_bytes(bt));
    if (width > metric.size() - b_x)
    {
        width -= metric.size() - b_x;

        std::string time = report == final
            ? std::format(" ● {}", format_time(elapse))
            : std::format(" ● {} ETA {}", format_time(elapse), format_time(remain));

        if (width > time.size() - b_x)
        {
            width -= time.size() - b_x;

            auto spd = std::format(" ● {}/s", format_bytes(*speed));
            if (width > spd.size() - b_x) { width -= spd.size() - b_x; metric += spd; }

            metric += time;
        }
    }
    else
    {
        metric = report == final
            ? std::format(" ● {}", format_time(elapse))
            : std::format(" ● {} ETA {}", format_time(elapse), format_time(remain));
        if (width > metric.size()) width -= metric.size(); else metric.clear();
    }

    auto bar = std::format(" {:>3.0f}%", *done);
    if (width > bar.size())
    {
        width -= bar.size();

        if (width > min_bar_width)
        {
            if (width > max_bar_width) width = max_bar_width;
            bar += " "; width -= 2;

            int len = *done * width / 100;
            for (auto n = 0; n < len; ++n) bar += "|";
            for (auto n = len; n < width; ++n) bar += ".";
        }
    }
    else bar.clear();

    print(replace, "{}{}\n", bar, metric);
}
