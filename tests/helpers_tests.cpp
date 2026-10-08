/**
 * @file    helpers_tests.cpp
 * @brief   Tests for grid points, rotations, snapping and the world/screen transform.
 */

#include "helpers.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

TEST_CASE("GridPoint adds and subtracts component-wise", "[helpers]") {
    CHECK(GUI::GridPoint{1, 2} + GUI::GridPoint{3, -4} == GUI::GridPoint{4, -2});
    CHECK(GUI::GridPoint{1, 2} - GUI::GridPoint{3, -4} == GUI::GridPoint{-2, 6});
}

TEST_CASE("NextRotation turns clockwise and wraps around", "[helpers]") {
    CHECK(GUI::NextRotation(GUI::Rotation::R0) == GUI::Rotation::R90);
    CHECK(GUI::NextRotation(GUI::Rotation::R90) == GUI::Rotation::R180);
    CHECK(GUI::NextRotation(GUI::Rotation::R180) == GUI::Rotation::R270);
    CHECK(GUI::NextRotation(GUI::Rotation::R270) == GUI::Rotation::R0);
}

TEST_CASE("Rotate turns clockwise on screen, where Y grows downward", "[helpers]") {
    CHECK(GUI::Rotate(GUI::GridPoint{1, 0}, GUI::Rotation::R90) == GUI::GridPoint{0, 1});
    CHECK(GUI::Rotate(GUI::GridPoint{1, 0}, GUI::Rotation::R180) == GUI::GridPoint{-1, 0});
    CHECK(GUI::Rotate(GUI::GridPoint{1, 0}, GUI::Rotation::R270) == GUI::GridPoint{0, -1});
}

TEST_CASE("Rotate gives the same result for grid points and floating points", "[helpers]") {
    const GUI::Rotation rotation =
        GENERATE(GUI::Rotation::R0, GUI::Rotation::R90, GUI::Rotation::R180, GUI::Rotation::R270);
    const GUI::GridPoint rotated = GUI::Rotate(GUI::GridPoint{3, -1}, rotation);
    const ImVec2 rotated_vector = GUI::Rotate(ImVec2{3.0f, -1.0f}, rotation);
    CHECK(GUI::ToVec2(rotated).x == rotated_vector.x);
    CHECK(GUI::ToVec2(rotated).y == rotated_vector.y);
}

TEST_CASE("InverseRotation undoes Rotate", "[helpers]") {
    const GUI::Rotation rotation =
        GENERATE(GUI::Rotation::R0, GUI::Rotation::R90, GUI::Rotation::R180, GUI::Rotation::R270);
    const GUI::GridPoint point{3, -1};
    CHECK(GUI::Rotate(GUI::Rotate(point, rotation), GUI::InverseRotation(rotation)) == point);
}

TEST_CASE("Snap rounds to the nearest grid point", "[helpers]") {
    CHECK(GUI::Snap({1.4f, -2.6f}) == GUI::GridPoint{1, -3});
    CHECK(GUI::Snap({-0.2f, 0.7f}) == GUI::GridPoint{0, 1});
}

TEST_CASE("ViewTransform converts between world and screen", "[helpers]") {
    const GUI::ViewTransform view({100.0f, 50.0f}, {10.0f, -20.0f}, 20.0f);
    const ImVec2 screen = view.ToScreen({2.0f, 3.0f});
    CHECK_THAT(screen.x, Catch::Matchers::WithinAbs(150.0, 1e-4));
    CHECK_THAT(screen.y, Catch::Matchers::WithinAbs(90.0, 1e-4));

    const ImVec2 world = view.ToWorld(screen);
    CHECK_THAT(world.x, Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK_THAT(world.y, Catch::Matchers::WithinAbs(3.0, 1e-4));
}

TEST_CASE("Rotations convert to degrees and back", "[helpers]") {
    const GUI::Rotation rotation =
        GENERATE(GUI::Rotation::R0, GUI::Rotation::R90, GUI::Rotation::R180, GUI::Rotation::R270);
    CHECK(GUI::RotationFromDegrees(GUI::ToDegrees(rotation)) == rotation);
    CHECK(GUI::ToDegrees(GUI::Rotation::R90) == 90);
    CHECK_FALSE(GUI::RotationFromDegrees(45));
    CHECK_FALSE(GUI::RotationFromDegrees(360));
}

TEST_CASE("FramePoints centers the points and fits them with a margin", "[helpers]") {
    // 20 by 10 grid units plus a 2 unit margin on each side fit a 480 by 480 canvas at 20 pixels per unit
    const std::vector<GUI::GridPoint> points = {{0, 0}, {20, 10}, {5, 3}};
    const GUI::ViewFrame frame = GUI::FramePoints(points, {480.0f, 480.0f}, 4.0f, 100.0f);
    CHECK_THAT(frame.Zoom, Catch::Matchers::WithinAbs(20.0, 1e-4));
    const GUI::ViewTransform view({0.0f, 0.0f}, frame.Pan, frame.Zoom);
    const ImVec2 center = view.ToScreen({10.0f, 5.0f});
    CHECK_THAT(center.x, Catch::Matchers::WithinAbs(240.0, 1e-3));
    CHECK_THAT(center.y, Catch::Matchers::WithinAbs(240.0, 1e-3));
}

TEST_CASE("FramePoints keeps the zoom between its limits", "[helpers]") {
    const std::vector<GUI::GridPoint> small = {{3, 3}};
    CHECK(GUI::FramePoints(small, {800.0f, 600.0f}, 4.0f, 20.0f).Zoom == 20.0f);
    const std::vector<GUI::GridPoint> huge = {{0, 0}, {10000, 0}};
    CHECK(GUI::FramePoints(huge, {800.0f, 600.0f}, 4.0f, 20.0f).Zoom == 4.0f);
    CHECK(GUI::FramePoints({}, {800.0f, 600.0f}, 4.0f, 20.0f).Zoom == 20.0f);
}
