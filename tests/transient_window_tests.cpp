/**
 * @file    transient_window_tests.cpp
 * @brief   Tests for the window of points that the plot shows and the schematic plays.
 */

#include "transient_window.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>

namespace {

// A transient with the given number of points, complete or still being made
Core::Transient TransientOf(const std::size_t point_count, const bool is_complete = true) {
    Core::Transient transient;
    transient.Times.assign(point_count, 0.0);
    transient.IsComplete = is_complete;
    return transient;
}

// A transient with points every step from 0, complete or still being made
Core::Transient EvenTransient(const std::size_t point_count, const double step, const bool is_complete = true) {
    Core::Transient transient;
    for (std::size_t index = 0; index < point_count; ++index) {
        transient.Times.push_back(static_cast<double>(index) * step);
    }
    transient.IsComplete = is_complete;
    return transient;
}

} // namespace

TEST_CASE("Without a transient there is no window", "[transient_window]") {
    GUI::TransientWindow window;
    const GUI::PointWindow shown = window.Update(nullptr, 1, false);
    CHECK(shown.Count == 0);
    CHECK(window.GetTotalPoints() == 0);
    CHECK_FALSE(window.IsLive());
}

TEST_CASE("A short transient is shown whole, and a long one by a window of the size chosen", "[transient_window]") {
    GUI::TransientWindow window;
    const Core::Transient short_run = TransientOf(500);
    CHECK(window.Update(&short_run, 1, false).Count == 500);

    const Core::Transient long_run = TransientOf(50'000);
    const GUI::PointWindow first = window.Update(&long_run, 2, false);
    CHECK(first.First == 0);
    CHECK(first.Count == static_cast<std::size_t>(GUI::DEFAULT_WINDOW_POINTS));
    CHECK(window.GetTotalPoints() == 50'000);

    window.SetPoints(5000);
    CHECK(window.Update(&long_run, 2, false).Count == 5000);
    // A size outside the limits is brought inside them
    window.SetPoints(10);
    CHECK(window.Update(&long_run, 2, false).Count == static_cast<std::size_t>(GUI::MIN_WINDOW_POINTS));
    window.SetPoints(1'000'000);
    CHECK(window.Update(&long_run, 2, false).Count == static_cast<std::size_t>(GUI::MAX_WINDOW_POINTS));
}

TEST_CASE("The window the user moves applies at the next update and stops following the run", "[transient_window]") {
    GUI::TransientWindow window;
    const Core::Transient run = TransientOf(50'000);
    window.Update(&run, 1, false);

    window.SetFirst(12'345);
    // The frame in progress keeps the window it started with
    CHECK(window.GetWindow().First == 0);
    CHECK(window.Update(&run, 1, false).First == 12'345);
    CHECK_FALSE(window.DoesFollow());

    // Past the end it stays inside the run
    window.SetFirst(1'000'000);
    CHECK(window.Update(&run, 1, false).First == 50'000 - static_cast<std::size_t>(GUI::DEFAULT_WINDOW_POINTS));
}

TEST_CASE("A window that follows a run in progress is on its newest points", "[transient_window]") {
    constexpr std::size_t SIZE = GUI::DEFAULT_WINDOW_POINTS;
    GUI::TransientWindow window;
    const Core::Transient first_chunk = TransientOf(10'000, false);
    CHECK(window.Update(&first_chunk, 1, true).First == 10'000 - SIZE);
    CHECK(window.IsLive());

    const Core::Transient more = TransientOf(30'000, false);
    CHECK(window.Update(&more, 1, true).First == 30'000 - SIZE);

    // Without following, the window stays where it is while the run grows
    window.SetFollow(false);
    const Core::Transient even_more = TransientOf(40'000, false);
    CHECK(window.Update(&even_more, 1, true).First == 30'000 - SIZE);
    CHECK_FALSE(window.IsLive());
}

TEST_CASE("The whole run that replaces the chunks keeps the place of the window, and a new run starts over",
          "[transient_window]") {
    constexpr std::size_t SIZE = GUI::DEFAULT_WINDOW_POINTS;
    GUI::TransientWindow window;
    const Core::Transient chunks = TransientOf(30'000, false);
    window.Update(&chunks, 1, true);

    // The run ends: the whole result is a new version, but the window was on the newest points and stays there
    const Core::Transient whole = TransientOf(100'000);
    CHECK(window.Update(&whole, 2, false).First == 30'000 - SIZE);
    CHECK_FALSE(window.IsLive());

    // The user looks elsewhere; a run after it starts at its beginning, following again
    window.SetFirst(60'000);
    window.Update(&whole, 2, false);
    const Core::Transient next_chunks = TransientOf(2 * SIZE, false);
    CHECK(window.Update(&next_chunks, 3, true).First == SIZE);
    CHECK(window.DoesFollow());
}

TEST_CASE("A window with a span holds the points of that stretch of time, wherever it is", "[transient_window]") {
    GUI::TransientWindow window;
    window.SetSpan(1e-3);
    CHECK(window.GetSpan() == 1e-3);
    // A point every 1 us, so 1 ms holds a thousand intervals
    const Core::Transient growing = EvenTransient(5000, 1e-6, false);
    const GUI::PointWindow followed = window.Update(&growing, 1, true);
    CHECK(followed.First + followed.Count == 5000);
    CHECK(growing.Times[followed.First] == Catch::Approx(growing.Times.back() - 1e-3).margin(1e-9));

    // Moved back, it holds the same stretch from where it starts
    window.SetFirst(100);
    const GUI::PointWindow moved = window.Update(&growing, 1, true);
    CHECK(moved.First == 100);
    CHECK(growing.Times[moved.First + moved.Count - 1] == Catch::Approx(growing.Times[100] + 1e-3).margin(1e-9));

    // Without the span it goes back to the number of points
    window.SetSpan(std::nullopt);
    CHECK(window.Update(&growing, 1, true).Count == static_cast<std::size_t>(GUI::DEFAULT_WINDOW_POINTS));
}

TEST_CASE("A span with more points than the largest window shows its newest ones", "[transient_window]") {
    GUI::TransientWindow window;
    window.SetSpan(1.0);
    const Core::Transient dense = EvenTransient(50'000, 1e-6, false);
    const GUI::PointWindow shown = window.Update(&dense, 1, true);
    CHECK(shown.Count == static_cast<std::size_t>(GUI::MAX_WINDOW_POINTS));
    CHECK(shown.First + shown.Count == 50'000);
}
