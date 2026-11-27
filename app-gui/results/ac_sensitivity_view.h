/**
 * @file    ac_sensitivity_view.h
 * @brief   What the editor and the Output window show of an AC sensitivity result: which frequency to read, the parts
 *          that matter most, the heat level of each part, the curves and the rows and texts of the table.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the overlays and the tab. The
 *          influence of a part at a frequency is the relative change of the complex response for a relative change of
 *          its value, $|d \ln H / d \ln p|$, which puts the magnitude and the phase in one number: a part that only
 *          moves the phase still counts.
 */

#ifndef IMCSIM_AC_SENSITIVITY_VIEW_H
#define IMCSIM_AC_SENSITIVITY_VIEW_H

#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/// The plot starts with this many of the most influential parts checked, so it shows what matters without a wall of
/// curves.
inline constexpr std::size_t PRESELECTED_PARTS = 5;

/// The heat scale is logarithmic and shows this many decades below the largest influence.
inline constexpr double AC_SENSITIVITY_DECADES = 3.0;

/**
 * @struct  ACSensitivityRow
 * @brief   One line of the AC sensitivity table at one frequency, written for reading.
 */
struct ACSensitivityRow {
    /// The part as the schematic names it, such as "R1", "VCC 12V", or "X2.R1" for the element inside a block.
    std::string Part;
    /// The name of the part in the circuit, which a click on the row selects.
    std::string PartName;
    /// Such as "4.7kOhm".
    std::string Value;
    /// Change of the gain for a 1 % higher value, such as "-0.0434 dB", or "-" where the output is too small.
    std::string Magnitude;
    /// Change of the phase for a 1 % higher value, such as "-0.286 deg", or "-".
    std::string Phase;
    /// Worst-case gain and phase change at the part's tolerance, such as "±0.217 dB / ±1.43 deg (5%)", or "-".
    std::string Tolerance;
    /// How much of the response a 1 % change of the part moves, as a share of the part that moves it most, from 0 to 1.
    double Share = 0.0;
};

/**
 * @struct  ACSensitivityCurve
 * @brief   The two figures of one part at every frequency, with a gap where the output was too small to have one.
 */
struct ACSensitivityCurve {
    /// Decibels per 1 %, not a number where there is none.
    std::vector<double> Magnitude;
    /// Degrees per 1 %, not a number where there is none.
    std::vector<double> Phase;
};

// Frequency
std::size_t FindCornerFrequencyIndex(const Core::ACSensitivity &sensitivity);
std::size_t FindNearestFrequencyIndex(const Core::ACSensitivity &sensitivity, double frequency);

// Influence
double GetInfluence(const Core::ACPartSensitivity &part, std::size_t frequency_index);
double GetLargestInfluence(const Core::ACSensitivity &sensitivity, std::size_t frequency_index);
std::vector<std::size_t> FindMostInfluential(const Core::ACSensitivity &sensitivity, std::size_t count);

// Heat colors
std::optional<float>
FindACSensitivityLevel(const Core::ACSensitivity &sensitivity, std::string_view part_name, std::size_t frequency_index);

// Table and curves
std::vector<ACSensitivityRow> MakeACSensitivityRows(const Core::ACSensitivity &sensitivity,
                                                    std::size_t frequency_index);
std::vector<ACSensitivityCurve> MakeACSensitivityCurves(const Core::ACSensitivity &sensitivity);

// Texts
std::string FormatDecibelsPerPercent(const std::optional<double> &decibels);
std::string FormatDegreesPerPercent(const std::optional<double> &degrees);
std::optional<std::string> FormatACWorstCaseSummary(const Core::ACSensitivity &sensitivity,
                                                    std::size_t frequency_index);

} // namespace GUI

#endif // IMCSIM_AC_SENSITIVITY_VIEW_H
