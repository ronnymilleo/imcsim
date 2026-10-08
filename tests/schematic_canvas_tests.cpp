/**
 * @file    schematic_canvas_tests.cpp
 * @brief   Tests for the SVG writer and the SVG canvas that exported schematics are drawn on.
 */

#include "schematic_canvas.h"
#include "svg_writer.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>

namespace {

using Catch::Matchers::ContainsSubstring;

} // namespace

TEST_CASE("SVG documents show the area given, over an optional background", "[svg]") {
    GUI::SvgWriter svg;
    svg.Line({0.0, 0.0}, {10.0, 0.0}, {0, 0, 0}, 2.0);
    const std::string document = svg.Finish({-5.5, 2.0}, 100.0, 50.0, GUI::ExportColor{255, 255, 255});
    CHECK_THAT(document, ContainsSubstring("width=\"100\" height=\"50\" viewBox=\"-5.5 2 100 50\""));
    CHECK_THAT(document, ContainsSubstring("<rect x=\"-5.5\" y=\"2\" width=\"100\" height=\"50\" fill=\"#ffffff\"/>"));
    CHECK_THAT(document, ContainsSubstring("<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\""));
    CHECK_THAT(svg.Finish({0.0, 0.0}, 10.0, 10.0), !ContainsSubstring("<rect"));
}

TEST_CASE("Closed outlines become unfilled polygons", "[svg]") {
    GUI::SvgWriter svg;
    svg.Polyline({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}}, {0, 0, 0}, 1.0, "", true);
    svg.Polygon({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}}, {10, 20, 30});
    const std::string document = svg.Finish({0.0, 0.0}, 10.0, 10.0);
    CHECK_THAT(document, ContainsSubstring("<polygon fill=\"none\" stroke=\"#000000\""));
    CHECK_THAT(document, ContainsSubstring("<polygon fill=\"#0a141e\" points=\"0,0 1,0 1,1\"/>"));
}

TEST_CASE("Coordinates have two decimals at most and no negative zero", "[svg]") {
    CHECK(GUI::FormatCoordinate(1.0) == "1");
    CHECK(GUI::FormatCoordinate(1.256) == "1.26");
    CHECK(GUI::FormatCoordinate(-0.001) == "0");
}

TEST_CASE("Print darkens only colors too light for white paper", "[svg]") {
    const GUI::ExportColor dark = {30, 30, 30};
    const GUI::ExportColor darkened = GUI::AdjustColorForPrint({240, 200, 90});
    CHECK(GUI::AdjustColorForPrint(dark).Red == 30);
    CHECK(darkened.Red < 240);
    CHECK(darkened.Green < 200);
}

TEST_CASE("The SVG canvas draws the ImDrawList calls and tracks the area they cover", "[svg]") {
    GUI::SvgWriter svg;
    GUI::SvgCanvas canvas(svg, false);
    canvas.AddLine({10.0f, 20.0f}, {50.0f, 20.0f}, IM_COL32(220, 220, 220, 255), 2.0f);
    canvas.AddCircleFilled({100.0f, 60.0f}, 4.0f, IM_COL32(230, 120, 100, 255));
    const ImVec2 rectangle[4] = {{0.0f, 0.0f}, {5.0f, 0.0f}, {5.0f, 5.0f}, {0.0f, 5.0f}};
    canvas.AddPolyline(rectangle, 4, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 2.0f);

    CHECK(canvas.GetMin().x == -2.0f);
    CHECK(canvas.GetMin().y == -2.0f);
    CHECK(canvas.GetMax().x == 104.0f);
    CHECK(canvas.GetMax().y == 64.0f);
    const std::string document = svg.Finish({0.0, 0.0}, 10.0, 10.0);
    CHECK_THAT(document, ContainsSubstring("stroke=\"#dcdcdc\""));
    CHECK_THAT(document, ContainsSubstring("<circle cx=\"100\" cy=\"60\" r=\"4\" fill=\"#e67864\"/>"));
    CHECK_THAT(document, ContainsSubstring("<polygon fill=\"none\""));
}

TEST_CASE("The SVG canvas darkens light colors for print", "[svg]") {
    GUI::SvgWriter svg;
    GUI::SvgCanvas canvas(svg, true);
    canvas.AddCircleFilled({0.0f, 0.0f}, 4.0f, IM_COL32(240, 200, 90, 255));
    CHECK_THAT(svg.Finish({0.0, 0.0}, 10.0, 10.0), !ContainsSubstring("#f0c85a"));
}
