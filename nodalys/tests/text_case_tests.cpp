/**
 * @file    text_case_tests.cpp
 * @brief   Unit tests for changing the case of ASCII text, as SPICE reads names, keys and words without regard to case.
 */

#include <catch2/catch_test_macros.hpp>

#include "text/text_case.h"

TEST_CASE("Changing the case of a text touches the ASCII letters only", "[text_case]") {
    CHECK(Nodalys::ToLower("V(Out)") == "v(out)");
    CHECK(Nodalys::ToUpper(".model D1n4148 d") == ".MODEL D1N4148 D");
    CHECK(Nodalys::ToUpper("").empty());
    // Bytes above 127 belong to UTF-8 sequences, such as the micro sign, and must come out unchanged
    CHECK(Nodalys::ToLower("4.7\xC2\xB5"
                           "F")
          == "4.7\xC2\xB5"
             "f");
    CHECK(Nodalys::ToUpper("4.7\xC2\xB5"
                           "f")
          == "4.7\xC2\xB5"
             "F");
}
