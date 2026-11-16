/**
 * @file    port_matrix_view.h
 * @brief   What the Output window shows of the impedance and admittance matrices of the ports, beside the S matrix: the
 *          curves, the gaps where a matrix does not exist, the readings and the figures for the export.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the S-parameters tab. A frequency at
 *          which the circuit has no finite matrix reads as not a number in the lists, so a plot breaks its line there
 *          instead of drawing across it.
 */

#ifndef IMCSIM_PORT_MATRIX_VIEW_H
#define IMCSIM_PORT_MATRIX_VIEW_H

#include "plots/plot_export.h"
#include "plots/s_parameter_view.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    SParameterQuantity
 * @brief   Which matrix of the ports the tab shows: scattering, impedance or admittance.
 */
enum class SParameterQuantity {
    S,
    Z,
    Y
};

// Quantity
char GetQuantityLetter(SParameterQuantity quantity);
std::string GetQuantityUnit(SParameterQuantity quantity);
bool IsQuantityAvailable(const Core::SParameters &result, SParameterQuantity quantity);
std::string DescribeUnavailableQuantity(SParameterQuantity quantity);

// Entries
std::string GetMatrixEntryName(const SParameterEntry &entry, SParameterQuantity quantity);
std::optional<std::complex<double>> ReadMatrixValue(const Core::SParameters &result, std::size_t frequency,
                                                    const SParameterEntry &entry, SParameterQuantity quantity);

// Absent frequencies
std::size_t CountAbsentFrequencies(const Core::SParameters &result, SParameterQuantity quantity);
std::string DescribeAbsentFrequencies(const Core::SParameters &result, SParameterQuantity quantity);

// Curves
std::vector<double> ListMatrixMagnitudes(const Core::SParameters &result, const SParameterEntry &entry,
                                         SParameterQuantity quantity);
std::vector<double> ListMatrixAngles(const Core::SParameters &result, const SParameterEntry &entry,
                                     SParameterQuantity quantity);

// Readings
std::vector<std::string> DescribeMatrixReading(const Core::SParameters &result, std::size_t frequency,
                                               const SParameterEntry &entry, SParameterQuantity quantity);

// Figures
ExportFigure BuildMatrixMagnitudePhaseFigure(const Core::SParameters &result,
                                             const std::vector<SParameterEntry> &entries,
                                             const std::optional<ExportRange> &view, SParameterQuantity quantity);
ExportFigure BuildMatrixTableFigure(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                    SParameterQuantity quantity);

} // namespace GUI

#endif // IMCSIM_PORT_MATRIX_VIEW_H
