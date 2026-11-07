/**
 * @file    dock_layout_tests.cpp
 * @brief   Tests for keeping the proportions of the docked windows when the main window changes size.
 */

#include "windows/dock_layout.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Every node of a dock tree keeps its share when the main window grows", "[dock_layout]") {
    // The default layout built in a 941 px window: schematic and Output beside a narrow side column
    ImGuiDockNode root(1);
    ImGuiDockNode pair(2);
    ImGuiDockNode side(3);
    ImGuiDockNode editor(4);
    ImGuiDockNode output(5);
    root.ChildNodes[0] = &pair;
    root.ChildNodes[1] = &side;
    pair.ChildNodes[0] = &editor;
    pair.ChildNodes[1] = &output;
    root.SizeRef = {941.0f, 1000.0f};
    pair.SizeRef = {734.0f, 1000.0f};
    side.SizeRef = {207.0f, 1000.0f};
    editor.SizeRef = {367.0f, 1000.0f};
    output.SizeRef = {367.0f, 1000.0f};

    // The window manager then makes it 1882 px wide and 500 px tall
    GUI::ScaleDockLayout(root, {2.0f, 0.5f});
    CHECK(root.SizeRef.x == 1882.0f);
    CHECK(editor.SizeRef.x == 734.0f);
    CHECK(output.SizeRef.x == 734.0f);
    CHECK(side.SizeRef.x == 414.0f);
    CHECK(output.SizeRef.y == 500.0f);
}
