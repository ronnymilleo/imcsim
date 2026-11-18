/**
 * @file    spice_text_tests.cpp
 * @brief   Tests for splitting and comparing the text of SPICE files, which ignore case and may have DOS line ends.
 */

#include "spice/spice_text.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Changing the case of a text touches the ASCII letters only", "[spice_text]") {
    CHECK(Core::ToLower("Vout_2N3904") == "vout_2n3904");
    CHECK(Core::ToUpper("params: gain=2") == "PARAMS: GAIN=2");
    CHECK(Core::ToLower("").empty());
    // Bytes above 127 belong to UTF-8 sequences, such as the micro sign, and must come out unchanged
    CHECK(Core::ToLower("4.7\xC2\xB5"
                        "F") == "4.7\xC2\xB5"
                                "f");
    CHECK(Core::ToUpper("4.7\xC2\xB5"
                        "f") == "4.7\xC2\xB5"
                                "F");
}
