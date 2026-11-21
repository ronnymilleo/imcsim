/**
 * @file    free_name_tests.cpp
 * @brief   Tests for finding a name nothing has yet, from a base name and numbered suffixes.
 */

#include "schematic/free_name.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace {

/**
 * @brief   Tells whether a name is one of some names, as a list of blocks or models would.
 */
auto IsAmong(const std::vector<std::string> &names) {
    return [&names](const std::string_view name) { return std::ranges::contains(names, name); };
}

} // namespace

TEST_CASE("A free name is the base name itself when nothing has it", "[free_name]") {
    const std::vector<std::string> names = {"other"};
    CHECK(GUI::FindFreeName("block", IsAmong(names)) == "block");
}

TEST_CASE("A taken name gets the first suffix that is free, counting from 2", "[free_name]") {
    const std::vector<std::string> one = {"block"};
    CHECK(GUI::FindFreeName("block", IsAmong(one)) == "block_2");

    // The numbering goes on from the base name, never from a suffixed one such as "block_2_2"
    const std::vector<std::string> three = {"block", "block_2", "block_3"};
    CHECK(GUI::FindFreeName("block", IsAmong(three)) == "block_4");
}

TEST_CASE("A free name fills the first gap in the suffixes", "[free_name]") {
    const std::vector<std::string> names = {"1N4148", "1N4148_3"};
    CHECK(GUI::FindFreeName("1N4148", IsAmong(names)) == "1N4148_2");
}
