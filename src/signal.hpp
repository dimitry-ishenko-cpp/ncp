////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <asio.hpp>
#include <initializer_list>
#include <thread>

////////////////////////////////////////////////////////////////////////////////
class signal_set
{
    asio::io_context io_;
    asio::signal_set sigset_;

    std::jthread thread_;

public:
    signal_set(std::initializer_list<int> signals, auto&& fn) : sigset_{io_}
    {
        for (auto signal : signals) sigset_.add(signal);

        sigset_.async_wait([fn = std::forward<decltype(fn)>(fn)](auto&& ec, int signal){
            if (!ec) fn(signal);
        });
        thread_ = std::jthread{[this]{ io_.run(); }};
    }

    signal_set(auto... signals, auto&& fn) : signal_set{{signals...}, fn} { }

    ~signal_set() { sigset_.cancel(); }
};
