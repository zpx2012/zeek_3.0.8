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

#include "caf/config.hpp"

#define CAF_SUITE ipv4_address
#include "caf/test/dsl.hpp"

#include "caf/ipv4_address.hpp"

#include "caf/detail/network_order.hpp"

using caf::detail::to_network_order;

using namespace caf;

namespace {

const auto addr = make_ipv4_address;

} // namespace <anonymous>

CAF_TEST(constructing) {
  auto localhost = addr(127, 0, 0, 1);
  CAF_CHECK_EQUAL(localhost.bits(), to_network_order(0x7F000001u));
  ipv4_address zero;
  CAF_CHECK_EQUAL(zero.bits(), 0u);
}

CAF_TEST(to string) {
  CAF_CHECK_EQUAL(to_string(addr(255, 255, 255, 255)), "255.255.255.255");
}

CAF_TEST(from string - valid inputs) {
  auto from_string = [](string_view str) -> expected<ipv4_address> {
    ipv4_address result;
    if (auto err = parse(str, result))
      return err;
    return result;
  };
  CAF_CHECK_EQUAL(from_string("136.12.12.12"), addr(136, 12, 12, 12));
  CAF_CHECK_EQUAL(from_string("255.255.255.255"), addr(255, 255, 255, 255));
}

CAF_TEST(from string - invalid inputs) {
  auto should_fail = [](string_view str) {
    ipv4_address result;
    auto err = parse(str, result);
    if (!err)
      CAF_ERROR("error while parsing "
                << str << ", expected an error but got: " << to_string(result));
  };
  should_fail("256.12.12.12");
  should_fail("1136.12.12.12");
  should_fail("1137.12.12.12");
  should_fail("1279.12.12.12");
  should_fail("1280.12.12.12");
}

CAF_TEST(properties) {
  CAF_CHECK_EQUAL(addr(127, 0, 0, 1).is_loopback(), true);
  CAF_CHECK_EQUAL(addr(127, 0, 0, 254).is_loopback(), true);
  CAF_CHECK_EQUAL(addr(127, 0, 1, 1).is_loopback(), true);
  CAF_CHECK_EQUAL(addr(128, 0, 0, 1).is_loopback(), false);
  CAF_CHECK_EQUAL(addr(224, 0, 0, 1).is_multicast(), true);
  CAF_CHECK_EQUAL(addr(224, 0, 0, 254).is_multicast(), true);
  CAF_CHECK_EQUAL(addr(224, 0, 1, 1).is_multicast(), true);
  CAF_CHECK_EQUAL(addr(225, 0, 0, 1).is_multicast(), false);
}

CAF_TEST(network addresses) {
  auto all1 = addr(255, 255, 255, 255);
  CAF_CHECK_EQUAL(all1.network_address(0), addr(0x00, 0x00, 0x00, 0x00));
  CAF_CHECK_EQUAL(all1.network_address(7), addr(0xFE, 0x00, 0x00, 0x00));
  CAF_CHECK_EQUAL(all1.network_address(8), addr(0xFF, 0x00, 0x00, 0x00));
  CAF_CHECK_EQUAL(all1.network_address(9), addr(0xFF, 0x80, 0x00, 0x00));
  CAF_CHECK_EQUAL(all1.network_address(31), addr(0xFF, 0xFF, 0xFF, 0xFE));
  CAF_CHECK_EQUAL(all1.network_address(32), addr(0xFF, 0xFF, 0xFF, 0xFF));
  CAF_CHECK_EQUAL(all1.network_address(33), addr(0xFF, 0xFF, 0xFF, 0xFF));
}

CAF_TEST(operators) {
  CAF_CHECK_EQUAL(addr(16, 0, 0, 8) & addr(255, 2, 4, 6), addr(16, 0, 0, 0));
  CAF_CHECK_EQUAL(addr(16, 0, 0, 8) | addr(255, 2, 4, 6), addr(255, 2, 4, 14));
  CAF_CHECK_EQUAL(addr(16, 0, 0, 8) ^ addr(255, 2, 4, 6), addr(239, 2, 4, 14));
}

