/**
 * @file    displayed_state_tests.cpp
 * @brief   Tests for which result the schematic shows and when the heat scale of a transient is worked out again.
 */

#include "overlays/displayed_state.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>

TEST_CASE("The schematic shows a transient unless an operating point was run after it", "[displayed_state]") {
    using GUI::ShownResult;
    CHECK(GUI::ChooseShownResult(false, false, std::nullopt) == ShownResult::NONE);
    CHECK(GUI::ChooseShownResult(true, false, GUI::Analysis::TRANSIENT) == ShownResult::OPERATING_POINT);
    CHECK(GUI::ChooseShownResult(false, true, GUI::Analysis::OPERATING_POINT) == ShownResult::TRANSIENT);
    CHECK(GUI::ChooseShownResult(true, true, GUI::Analysis::TRANSIENT) == ShownResult::TRANSIENT);
    CHECK(GUI::ChooseShownResult(true, true, GUI::Analysis::OPERATING_POINT) == ShownResult::OPERATING_POINT);
    // Before any run of this session, as when a file is opened with both results, the transient wins
    CHECK(GUI::ChooseShownResult(true, true, std::nullopt) == ShownResult::TRANSIENT);
}

TEST_CASE("The heat scale of a transient is worked out for new data or a new window, a few times a second while it "
          "runs",
          "[displayed_state]") {
    const GUI::ScaleKey key = {.DataVersion = 3, .Window = {.First = 0, .Count = 100}};
    GUI::TransientScaleCache cache;
    // The first scale is due at once, even while the run goes on
    CHECK(GUI::IsScaleDue(cache, key, false, 0.1));

    cache.Key = key;
    cache.LastTime = 10.0;
    CHECK_FALSE(GUI::IsScaleDue(cache, key, false, 20.0));
    CHECK_FALSE(GUI::IsScaleDue(cache, key, true, 20.0));

    const GUI::ScaleKey newer = {.DataVersion = 4, .Window = key.Window};
    CHECK_FALSE(GUI::IsScaleDue(cache, newer, false, 10.2));
    CHECK(GUI::IsScaleDue(cache, newer, false, 10.5));
    // A finished run gets its final scale at once
    CHECK(GUI::IsScaleDue(cache, newer, true, 10.2));

    const GUI::ScaleKey moved = {.DataVersion = 3, .Window = {.First = 50, .Count = 100}};
    CHECK(GUI::IsScaleDue(cache, moved, true, 10.2));
}
