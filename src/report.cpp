////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "state.hpp"

#include <array>
#include <format>
#include <print>
#include <thread>

using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
auto human(long bytes)
{
    constexpr auto units = std::array{ "B", "KiB", "MiB", "GiB", "TiB" };
    
    auto n = 0;
    auto dbl_bytes = static_cast<double>(bytes);
    for (; dbl_bytes >= 1024.0 && n < units.size() - 1; ++n) dbl_bytes /= 1024.0;

    return std::format("{:.{}f}{}", dbl_bytes, n ? 2 : 0, units[n]);
}

void report_one(state& state, bool overwrite = true)
{
    if (overwrite) std::print("\033[{}F\033[K", 1);

    auto errors = state.drain_errors();
    for (auto&& error : errors) print("{}\n", error);

    auto files_total  = state.files_total.load(std::memory_order_relaxed);
    auto files_copied = state.files_copied.load(std::memory_order_relaxed);
    auto bytes_total  = state.bytes_total.load(std::memory_order_relaxed);
    auto bytes_copied = state.bytes_copied.load(std::memory_order_relaxed);

    auto percent_copied = bytes_total ? (100 * bytes_copied / bytes_total) : 0;

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

void report(state& state)
{
    bool overwrite = false;
    do
    {
        std::this_thread::sleep_for(100ms);

        report_one(state, overwrite);
        overwrite = true;
    }
    while (!state.quit.load(std::memory_order_relaxed));
}
