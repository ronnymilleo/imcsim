/**
 * @file    text_case_tests.cpp
 * @brief   Unit tests for changing and comparing the case of ASCII text, as SPICE reads names, keys and words.
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

TEST_CASE("Names that differ only in case are one name, and others keep their order", "[text_case]") {
    const Nodalys::CaseInsensitiveLess less;
    CHECK_FALSE(less("Vin", "VIN"));
    CHECK_FALSE(less("VIN", "Vin"));
    CHECK(less("v1", "V2"));
    CHECK(less("R", "r1"));
    CHECK_FALSE(less("V2", "v1"));
}
