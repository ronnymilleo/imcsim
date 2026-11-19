/**
 * @file    node_index_tests.cpp
 * @brief   Tests for the check that a node number has a trace in a result.
 */

#include "results/node_index.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("A node inside the result is its own index, ground included", "[node_index]") {
    CHECK(GUI::FindNodeIndex(0, 3) == 0);
    CHECK(GUI::FindNodeIndex(2, 3) == 2);
}

TEST_CASE("A node beyond the result, a negative one or none has no index", "[node_index]") {
    CHECK_FALSE(GUI::FindNodeIndex(3, 3));
    CHECK_FALSE(GUI::FindNodeIndex(-1, 3));
    CHECK_FALSE(GUI::FindNodeIndex(std::nullopt, 3));
    CHECK_FALSE(GUI::FindNodeIndex(0, 0));
}
