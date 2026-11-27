/**
 * @file    test_printers.h
 * @brief   Teaches Catch2 how to print editor and simulation types, so failed checks show the values instead of "{?}".
 */

#ifndef IMCSIM_TEST_PRINTERS_H
#define IMCSIM_TEST_PRINTERS_H

#include "base/helpers.h"
#include "simulation/live_change.h"
#include "ui_elements/ui_wire.h"
#include <catch2/catch_tostring.hpp>
#include <format>
#include <string>

/**
 * @struct  Catch::StringMaker<GUI::GridPoint>
 * @brief   Formats a schematic grid point for Catch2 failure messages.
 */
template <>
struct Catch::StringMaker<GUI::GridPoint> {
    /**
     * @brief   Formats a grid point as `(X, Y)`.
     * @param[in] point  Grid point to format.
     * @return  Coordinate text.
     */
    static std::string convert(const GUI::GridPoint &point) { return std::format("({}, {})", point.X, point.Y); }
};

/**
 * @struct  Catch::StringMaker<GUI::UIWire>
 * @brief   Formats a schematic wire segment for Catch2 failure messages.
 */
template <>
struct Catch::StringMaker<GUI::UIWire> {
    /**
     * @brief   Formats a wire segment by its start and end grid points.
     * @param[in] wire  Wire segment to format.
     * @return  Endpoint range text.
     */
    static std::string convert(const GUI::UIWire &wire) {
        return std::format("({}, {})-({}, {})", wire.GetStart().X, wire.GetStart().Y, wire.GetEnd().X, wire.GetEnd().Y);
    }
};

/**
 * @struct  Catch::StringMaker<Core::LiveChange>
 * @brief   Formats a live simulation parameter change for Catch2 failure messages.
 */
template <>
struct Catch::StringMaker<Core::LiveChange> {
    /**
     * @brief   Formats a live change with its element name, quantity, and value.
     * @param[in] change  Live change to format.
     * @return  Description text.
     */
    static std::string convert(const Core::LiveChange &change) {
        return std::format("{} {} = {}",
                           change.Element,
                           change.Quantity == Core::LiveQuantity::RESISTANCE ? "resistance" : "dc",
                           change.Value);
    }
};

#endif // IMCSIM_TEST_PRINTERS_H
