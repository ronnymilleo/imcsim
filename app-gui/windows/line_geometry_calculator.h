/**
 * @file    line_geometry_calculator.h
 * @brief   The popup of the transmission line properties that works out Z0 and the losses from a real line's geometry.
 */

#ifndef IMCSIM_LINE_GEOMETRY_CALCULATOR_H
#define IMCSIM_LINE_GEOMETRY_CALCULATOR_H

#include "base/value_field.h"
#include "circuit/line_geometry.h"
#include <array>
#include <optional>

namespace GUI {

/**
 * @struct  LineGeometryCalculator
 * @brief   The geometry being entered and the texts of its fields, kept between openings so the last line comes back.
 */
struct LineGeometryCalculator {
    /// Most dimensions a cross-section has: the width, height and thickness of a microstrip.
    static constexpr std::size_t MAX_DIMENSIONS = 3;

    Core::LineGeometry Geometry;
    /// One per dimension of the cross-section on show, in the order it lists them.
    std::array<ValueField, MAX_DIMENSIONS> Dimensions;
    ValueField Permittivity;
    ValueField LossTangent;
    ValueField Conductivity;
    ValueField Length;
    ValueField ReferenceFrequency;
};

void OpenLineGeometryCalculator(LineGeometryCalculator &calculator);
std::optional<Core::LineElectrical> DrawLineGeometryCalculator(LineGeometryCalculator &calculator);

} // namespace GUI

#endif // IMCSIM_LINE_GEOMETRY_CALCULATOR_H
