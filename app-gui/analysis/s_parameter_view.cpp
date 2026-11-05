/**
 * @file    s_parameter_view.cpp
 * @brief   What the Output window shows of an S-parameter result: which entries of the matrix are plotted, the readings
 *          under the cursor, and the figures of the plots for the export.
 */

#include "analysis/s_parameter_view.h"

#include "simulation/s_parameters.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

/// Colors of the entries in the order they are listed, which keep their hue in the plots and the export.
constexpr auto ENTRY_COLORS = std::to_array<ExportColor>({{86, 156, 214},
                                                          {230, 126, 80},
                                                          {120, 190, 100},
                                                          {200, 120, 200},
                                                          {230, 200, 90},
                                                          {90, 200, 200},
                                                          {220, 100, 120},
                                                          {160, 160, 160}});

/// Digits of a reading, enough to tell 50 ohm from 51.
constexpr int READING_DIGITS = 4;

std::size_t IndexOf(const int port_count, const int row, const int column) {
    return static_cast<std::size_t>((row - 1) * port_count + (column - 1));
}

} // namespace

/**
 * @brief   Shows S11 and S21 again for a result with this many ports.
 * @param[in] port_count  Ports of the result.
 */
void SParameterSelection::Reset(const int port_count) {
    m_PortCount = port_count;
    m_Shown.assign(static_cast<std::size_t>(port_count * port_count), false);
    if (port_count >= 1) {
        SetShown(1, 1, true);
    }
    if (port_count >= 2) {
        SetShown(2, 1, true);
    }
}

/**
 * @brief   Shows the reflections S11, S22... of a result with this many ports, and no transmission.
 * @param[in] port_count  Ports of the result.
 */
void SParameterSelection::ResetToReflections(const int port_count) {
    m_PortCount = port_count;
    m_Shown.assign(static_cast<std::size_t>(port_count * port_count), false);
    for (int port = 1; port <= port_count; ++port) {
        SetShown(port, port, true);
    }
}

/**
 * @brief   Returns the number of ports the selection covers.
 * @return  The ports of the result it was reset for; 0 before the first reset.
 */
int SParameterSelection::GetPortCount() const {
    return m_PortCount;
}

/**
 * @brief   Tells whether an entry is plotted.
 * @param[in] row     Row of the entry, from 1.
 * @param[in] column  Column of the entry, from 1.
 * @return  True when it is shown; false when it is not or lies outside the matrix.
 */
bool SParameterSelection::IsShown(const int row, const int column) const {
    if (row < 1 || column < 1 || row > m_PortCount || column > m_PortCount) {
        return false;
    }
    return m_Shown[IndexOf(m_PortCount, row, column)];
}

/**
 * @brief   Shows or hides an entry.
 * @param[in] row       Row of the entry, from 1.
 * @param[in] column    Column of the entry, from 1.
 * @param[in] is_shown  Whether to plot it; an entry outside the matrix is ignored.
 */
void SParameterSelection::SetShown(const int row, const int column, const bool is_shown) {
    if (row < 1 || column < 1 || row > m_PortCount || column > m_PortCount) {
        return;
    }
    m_Shown[IndexOf(m_PortCount, row, column)] = is_shown;
}

/**
 * @brief   Lists the entries that are plotted.
 * @return  The shown entries, row by row.
 */
std::vector<SParameterEntry> SParameterSelection::ListShown() const {
    std::vector<SParameterEntry> entries;
    for (int row = 1; row <= m_PortCount; ++row) {
        for (int column = 1; column <= m_PortCount; ++column) {
            if (IsShown(row, column)) {
                entries.push_back({row, column});
            }
        }
    }
    return entries;
}

/**
 * @brief   Names an entry as a datasheet does.
 * @param[in] entry  Entry of the matrix.
 * @return  "S21" for one-digit ports, and "S1,10" when a port number needs two digits.
 */
std::string GetEntryName(const SParameterEntry &entry) {
    if (entry.Row < 10 && entry.Column < 10) {
        return std::format("S{}{}", entry.Row, entry.Column);
    }
    return std::format("S{},{}", entry.Row, entry.Column);
}

/**
 * @brief   Picks the color of the n-th plotted entry.
 * @param[in] index  Position in the list of shown entries.
 * @return  A color of the palette, which repeats after eight.
 */
ExportColor GetEntryColor(const std::size_t index) {
    return ENTRY_COLORS[index % ENTRY_COLORS.size()];
}

/**
 * @brief   Reads one entry at one frequency, with what its kind can tell.
 * @param[in] result     S matrix of the sweep.
 * @param[in] frequency  Index of the frequency.
 * @param[in] entry      Entry to read; it must lie inside the matrix.
 * @return  The value in decibels and degrees; for a reflection also the impedance, the standing wave ratio and the
 *          return loss.
 */
SParameterReading ReadEntry(const Core::SParameters &result, const std::size_t frequency,
                            const SParameterEntry &entry) {
    SParameterReading reading;
    reading.Entry = entry;
    reading.FrequencyHertz = result.Frequencies[frequency];
    reading.Value = result.Get(frequency, entry.Row, entry.Column);
    reading.MagnitudeDecibels = Core::ToDecibels(reading.Value);
    reading.AngleDegrees = Core::ToDegrees(reading.Value);
    reading.IsReflection = entry.Row == entry.Column;
    if (reading.IsReflection) {
        reading.ReferenceOhms = result.ReferenceOhms[static_cast<std::size_t>(entry.Row - 1)];
        reading.Impedance = Core::ToImpedance(reading.Value, reading.ReferenceOhms);
        reading.Vswr = Core::ComputeVswr(reading.Value);
        reading.ReturnLossDecibels = Core::ComputeReturnLossDecibels(reading.Value);
    }
    return reading;
}

/**
 * @brief   Writes a complex impedance as the datasheets do.
 * @param[in] impedance  Impedance in ohms.
 * @return  Such as "50 + j25" or "12.5 - j3.2".
 */
std::string FormatImpedance(const std::complex<double> impedance) {
    const double reactance = impedance.imag();
    return std::format("{:.{}g} {} j{:.{}g}", impedance.real(), READING_DIGITS, reactance < 0.0 ? "-" : "+",
                       std::abs(reactance), READING_DIGITS);
}

/**
 * @brief   Words a reading for a tooltip, one line each.
 * @param[in] reading  What is under the cursor.
 * @return  The entry and frequency, the magnitude and angle, and for a reflection the impedance (absolute and
 *          normalized), the standing wave ratio and the return loss; those without a value read "infinite".
 */
std::vector<std::string> DescribeReading(const SParameterReading &reading) {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} at {}Hz", GetEntryName(reading.Entry), Core::FormatValue(reading.FrequencyHertz)));
    lines.push_back(std::format("{:.{}g} dB at {:.{}g} deg", reading.MagnitudeDecibels, READING_DIGITS,
                                reading.AngleDegrees, READING_DIGITS));
    if (!reading.IsReflection) {
        return lines;
    }
    if (reading.Impedance) {
        lines.push_back(std::format("Z = {} ohm", FormatImpedance(*reading.Impedance)));
        lines.push_back(std::format("z = {} (normalized to {} ohm)",
                                    FormatImpedance(*reading.Impedance / reading.ReferenceOhms),
                                    Core::FormatValue(reading.ReferenceOhms)));
    } else {
        lines.emplace_back("Z = infinite (open circuit)");
    }
    lines.push_back(reading.Vswr ? std::format("VSWR {:.{}g}", *reading.Vswr, READING_DIGITS) : "VSWR infinite");
    lines.push_back(reading.ReturnLossDecibels
                        ? std::format("Return loss {:.{}g} dB", *reading.ReturnLossDecibels, READING_DIGITS)
                        : "Return loss infinite");
    return lines;
}

/**
 * @brief   Finds the frequency of the sweep nearest to one on the plot, by distance on a logarithmic axis as the plot
 *          draws it.
 * @param[in] frequencies  Frequencies of the sweep, ascending and positive.
 * @param[in] frequency    Frequency under the cursor.
 * @return  Index of the nearest one; 0 for an empty list.
 */
std::size_t FindNearestFrequency(const std::vector<double> &frequencies, const double frequency) {
    std::size_t nearest = 0;
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < frequencies.size(); ++index) {
        const double distance = std::abs(std::log(frequencies[index] / std::max(frequency, 1e-300)));
        if (distance < smallest) {
            smallest = distance;
            nearest = index;
        }
    }
    return nearest;
}

/**
 * @brief   Finds the point of a curve on the Smith chart nearest to the cursor.
 * @param[in] values  The curve, one reflection per frequency.
 * @param[in] point   Cursor position on the plane of the reflection coefficient.
 * @return  Index of the nearest point; 0 for an empty curve.
 */
std::size_t FindNearestReflection(const std::vector<std::complex<double>> &values, const std::complex<double> point) {
    std::size_t nearest = 0;
    double smallest = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < values.size(); ++index) {
        const double distance = std::abs(values[index] - point);
        if (distance < smallest) {
            smallest = distance;
            nearest = index;
        }
    }
    return nearest;
}

/**
 * @brief   Lists the magnitude of an entry at every frequency.
 * @param[in] result  S matrix of the sweep.
 * @param[in] entry   Entry to list; it must lie inside the matrix.
 * @return  Decibels, one per frequency.
 */
std::vector<double> ListMagnitudesDecibels(const Core::SParameters &result, const SParameterEntry &entry) {
    std::vector<double> values;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        values.push_back(Core::ToDecibels(result.Get(frequency, entry.Row, entry.Column)));
    }
    return values;
}

/**
 * @brief   Lists the angle of an entry at every frequency.
 * @param[in] result  S matrix of the sweep.
 * @param[in] entry   Entry to list; it must lie inside the matrix.
 * @return  Degrees from -180 to 180, one per frequency.
 */
std::vector<double> ListAnglesDegrees(const Core::SParameters &result, const SParameterEntry &entry) {
    std::vector<double> values;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        values.push_back(Core::ToDegrees(result.Get(frequency, entry.Row, entry.Column)));
    }
    return values;
}

/**
 * @brief   Lists an entry at every frequency.
 * @param[in] result  S matrix of the sweep.
 * @param[in] entry   Entry to list; it must lie inside the matrix.
 * @return  The complex values, one per frequency.
 */
std::vector<std::complex<double>> ListValues(const Core::SParameters &result, const SParameterEntry &entry) {
    std::vector<std::complex<double>> values;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        values.push_back(result.Get(frequency, entry.Row, entry.Column));
    }
    return values;
}

/**
 * @brief   Builds the figure of the magnitude and phase plots of the shown entries.
 * @param[in] result   S matrix of the sweep.
 * @param[in] entries  Entries to draw, which must lie inside the matrix.
 * @param[in] view     Frequencies the plots show, or none for the whole sweep.
 * @return  A magnitude panel in decibels over a phase panel in degrees, on a logarithmic frequency axis.
 */
ExportFigure BuildMagnitudePhaseFigure(const Core::SParameters &result, const std::vector<SParameterEntry> &entries,
                                       const std::optional<ExportRange> &view) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = result.Frequencies;
    figure.XRange = view;
    ExportPanel magnitude{.Title = "Magnitude",
                          .YAxes = {{.Name = "Magnitude (dB)", .Unit = "dB", .UsesPrefixes = false}}};
    ExportPanel phase{.Title = "Phase", .YAxes = {{.Name = "Phase (deg)", .Unit = "deg", .UsesPrefixes = false}}};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::string name = GetEntryName(entries[index]);
        const ExportColor color = GetEntryColor(index);
        magnitude.Series.push_back({.Name = name,
                                    .LegendLabel = name,
                                    .Values = ListMagnitudesDecibels(result, entries[index]),
                                    .Color = color});
        phase.Series.push_back(
            {.Name = name, .LegendLabel = name, .Values = ListAnglesDegrees(result, entries[index]), .Color = color});
    }
    figure.Panels.push_back(std::move(magnitude));
    figure.Panels.push_back(std::move(phase));
    return figure;
}

/**
 * @brief   Builds the figure of the Smith chart of the shown entries.
 * @param[in] result   S matrix of the sweep.
 * @param[in] entries  Entries to draw, which must lie inside the matrix.
 * @return  A figure whose Smith chart holds one curve per entry; its CSV lists the real and imaginary parts.
 */
ExportFigure BuildSmithFigure(const Core::SParameters &result, const std::vector<SParameterEntry> &entries) {
    ExportFigure figure;
    ExportSmith smith{.Title = "Smith chart"};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        smith.Traces.push_back({.Name = GetEntryName(entries[index]),
                                .Color = GetEntryColor(index),
                                .Frequencies = result.Frequencies,
                                .Values = ListValues(result, entries[index])});
    }
    figure.Smith = std::move(smith);
    return figure;
}

} // namespace GUI
