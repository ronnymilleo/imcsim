/**
 * @file    ac_sensitivity.h
 * @brief   Turns the figures a solver reports for each element into the AC sensitivity of each part.
 * @details Independent of the solver: it takes the figures by element name, so any backend that reports them, and the
 *          tests with made-up numbers, share the rules for potentiometers, blocks and which parts are listed.
 */

#ifndef IMCSIM_AC_SENSITIVITY_H
#define IMCSIM_AC_SENSITIVITY_H

#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  ACElementFigures
 * @brief   What a solver reports of one element: its value and how the response changes for a 1 % higher value.
 */
struct ACElementFigures {
    double Value = 0.0; ///< Value of the element: ohms, farads, henries, volts, amperes or the gain.
    std::vector<std::optional<double>> MagnitudeDecibelsPerPercent;
    std::vector<std::optional<double>> PhaseDegreesPerPercent;
};

/// Figures by lower case element name, as the netlist writes it: "r1", "x2.r1" for the resistor R1 inside the block X2
/// and "r-rv1a" for a half of a potentiometer.
using ACElementFiguresByName = std::map<std::string, ACElementFigures>;

// Results
ACSensitivity BuildACSensitivity(const Circuit &circuit, ACSensitivity header, const ACElementFiguresByName &elements);

} // namespace Core

#endif // IMCSIM_AC_SENSITIVITY_H
