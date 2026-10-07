/**
 * @file    test_printers.h
 * @brief   Teaches Catch2 how to print editor types, so failed checks show the values instead of "{?}".
 */

#ifndef IMCSIM_TEST_PRINTERS_H
#define IMCSIM_TEST_PRINTERS_H

#include "helpers.h"
#include "ui_elements/ui_wire.h"
#include <catch2/catch_tostring.hpp>
#include <format>
#include <string>

// Catch2 looks for these specializations in its own namespace
template <> struct Catch::StringMaker<GUI::GridPoint> {
    static std::string convert(const GUI::GridPoint &point) { return std::format("({}, {})", point.X, point.Y); }
};

template <> struct Catch::StringMaker<GUI::UIWire> {
    static std::string convert(const GUI::UIWire &wire) {
        return std::format("({}, {})-({}, {})", wire.GetStart().X, wire.GetStart().Y, wire.GetEnd().X, wire.GetEnd().Y);
    }
};

#endif // IMCSIM_TEST_PRINTERS_H
