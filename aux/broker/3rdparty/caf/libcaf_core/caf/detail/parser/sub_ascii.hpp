
/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <limits>

#include "caf/detail/parser/ascii_to_int.hpp"
#include "caf/detail/type_traits.hpp"

namespace caf {
namespace detail {
namespace parser {

// Subtracs integers when parsing negative integers.
// @returns `false` on an underflow, otherwise `true`.
// @pre `isdigit(c) || (Base == 16 && isxdigit(c))`
template <int Base, class T>
bool sub_ascii(T& x, char c, enable_if_tt<std::is_integral<T>, int> u = 0) {
  CAF_IGNORE_UNUSED(u);
  if (x < (std::numeric_limits<T>::min() / Base))
    return false;
  x = static_cast<T>(x * Base);
  ascii_to_int<Base, T> f;
  auto y = f(c);
  if (x < (std::numeric_limits<T>::min() + y))
    return false;
  x = static_cast<T>(x - y);
  return true;
}

template <int Base, class T>
bool sub_ascii(T& x, char c,
               enable_if_tt<std::is_floating_point<T>, int> u = 0) {
  CAF_IGNORE_UNUSED(u);
  ascii_to_int<Base, T> f;
  x = static_cast<T>((x * Base) - f(c));
  return true;
}

} // namespace parser
} // namespace detail
} // namespace caf
