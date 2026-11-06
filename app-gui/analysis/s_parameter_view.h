/**
 * @file    s_parameter_view.h
 * @brief   What the Output window shows of an S-parameter result: which entries of the matrix are plotted, the readings
 *          under the cursor, and the figures of the plots for the export.
 * @details Pure functions with no ImGui, so the tests run them; the drawing is in the S-parameters tab.
 */

#ifndef IMCSIM_S_PARAMETER_VIEW_H
#define IMCSIM_S_PARAMETER_VIEW_H

#include "plots/plot_export.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SParameterEntry
 * @brief   One entry of the matrix, S(Row, Column), with both indexes from 1.
 */
struct SParameterEntry {
    int Row = 1;
    int Column = 1;

    bool operator==(const SParameterEntry &) const = default;
};

/**
 * @class   SParameterSelection
 * @brief   Which entries of the S matrix the plots show, as a matrix of checkboxes.
 * @details A new result with another number of ports starts again from S11 and S21, the reflection and the gain of the
 *          usual two-port; the Smith chart is for reflections, so its selection starts from S11, S22... instead.
 */
class SParameterSelection {
public:
    void Reset(int port_count);
    void ResetToReflections(int port_count);
    int GetPortCount() const;
    bool IsShown(int row, int column) const;
    void SetShown(int row, int column, bool is_shown);
    std::vector<SParameterEntry> ListShown() const;

private:
    int m_PortCount = 0;
    /// Row by row, one flag per entry.
    std::vector<bool> m_Shown;
};

/**
 * @struct  SParameterReading
 * @brief   The values under the cursor for one entry at one frequency.
 * @details A reflection (S11, S22...) also tells the impedance the port sees and how well it is matched; a transmission
 *          has only its magnitude and angle.
 */
struct SParameterReading {
    SParameterEntry Entry;
    double FrequencyHertz = 0.0;
    std::complex<double> Value;
    double MagnitudeDecibels = 0.0;
    double AngleDegrees = 0.0;
    bool IsReflection = false;
    double ReferenceOhms = 0.0;
    /// None for an open circuit, whose impedance is infinite.
    std::optional<std::complex<double>> Impedance;
    /// None when the port reflects everything or nothing is reflected, where the ratio is infinite or the loss too.
    std::optional<double> Vswr;
    std::optional<double> ReturnLossDecibels;
};

// Entries
std::string GetEntryName(const SParameterEntry &entry);
ExportColor GetEntryColor(std::size_t index);

// Readings
SParameterReading ReadEntry(const Core::SParameters &result, std::size_t frequency, const SParameterEntry &entry);
std::string FormatImpedance(std::complex<double> impedance);
std::vector<std::string> DescribeReading(const SParameterReading &reading);
std::size_t FindNearestFrequency(const std::vector<double> &frequencies, double frequency);
std::size_t FindNearestReflection(const std::vector<std::complex<double>> &values, std::complex<double> point);

// Curves
std::vector<double> ListMagnitudesDecibels(const Core::SParameters &result, const SParameterEntry &entry);
std::vector<double> ListAnglesDegrees(const Core::SParameters &result, const SParameterEntry &entry);
std::vector<std::complex<double>> ListValues(const Core::SParameters &result, const SParameterEntry &entry);

// Figures
ExportFigure BuildMagnitudePhaseFigure(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                       const std::optional<ExportRange> &view);
ExportFigure BuildSmithFigure(const Core::SParameters &result, const std::vector<SParameterEntry> &entries);

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_VIEW_H
