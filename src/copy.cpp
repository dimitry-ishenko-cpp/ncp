////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "options.hpp"
#include "state.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
void copy_file(const options& options, state& state, const fs::path& source, const fs::path& target)
{
    if (state.quit.load(std::memory_order_relaxed)) return;

    std::error_code ec;
    fs::copy(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) { state.add_error(ec, source, target); return; }

    auto size = fs::file_size(source, ec);
    if (ec) { state.add_error(ec, source); return; }

    state.files_total.fetch_add(1, std::memory_order_relaxed);
    state.bytes_total.fetch_add(size, std::memory_order_relaxed);
}
