////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>

template <typename V>
class smooth
{
    std::optional<V> value_;
    double alpha_;

public:
    constexpr smooth(double alpha = .5) : alpha_{alpha} { }
    constexpr smooth(V value, double alpha) : value_{value}, alpha_{alpha} { }

    constexpr smooth& operator=(V target) noexcept
    {
        if (value_) *value_ += static_cast<V>((target - *value_) * alpha_);
        else value_ = target;
        return *this;
    }
    constexpr smooth& force(V target) noexcept { value_ = target; return *this; }

    constexpr explicit operator bool() const noexcept { return !!value_; }
    constexpr V operator*() const noexcept { return *value_; }
    constexpr V value() const { return value_.value(); }
    constexpr V value_or(V other = V{}) const noexcept { return value_.value_or(other); }
};
