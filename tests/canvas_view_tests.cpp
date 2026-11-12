/**
 * @file    canvas_view_tests.cpp
 * @brief   Tests for the pan and zoom of the schematic canvas.
 */

#include "schematic/canvas_view.h"

#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

/// Screen origin of the canvas in these tests; any point works, since only differences matter.
constexpr ImVec2 ORIGIN = {10.0f, 20.0f};

/**
 * @brief   Tells which world point sits at the middle of a canvas.
 */
ImVec2 CenterOf(const GUI::CanvasView &view, const ImVec2 canvas_size) {
    return view.GetTransform(ORIGIN).ToWorld({ORIGIN.x + canvas_size.x / 2.0f, ORIGIN.y + canvas_size.y / 2.0f});
}

} // namespace

TEST_CASE("The point at the middle of the canvas stays there when the canvas changes size", "[canvas_view]") {
    GUI::CanvasView view;
    const ImVec2 full_size = {1200.0f, 800.0f};
    view.FollowCanvasSize(full_size);
    const ImVec2 center = CenterOf(view, full_size);

    // Output opening beside the schematic halves its width
    const ImVec2 half_size = {600.0f, 800.0f};
    view.FollowCanvasSize(half_size);
    CHECK_THAT(CenterOf(view, half_size).x, Catch::Matchers::WithinAbs(center.x, 1e-4));
    CHECK_THAT(CenterOf(view, half_size).y, Catch::Matchers::WithinAbs(center.y, 1e-4));

    view.FollowCanvasSize(full_size);
    CHECK_THAT(CenterOf(view, full_size).x, Catch::Matchers::WithinAbs(center.x, 1e-4));
}

TEST_CASE("The first canvas size leaves the view where it was", "[canvas_view]") {
    GUI::CanvasView view;
    const ImVec2 before = view.GetTransform(ORIGIN).ToWorld(ORIGIN);
    view.FollowCanvasSize({1200.0f, 800.0f});
    const ImVec2 after = view.GetTransform(ORIGIN).ToWorld(ORIGIN);
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
}
