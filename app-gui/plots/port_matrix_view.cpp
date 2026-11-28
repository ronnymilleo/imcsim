/**
 * @file    port_matrix_view.cpp
 * @brief   What the Output window shows of the impedance and admittance matrices of the ports, beside the S matrix: the
 *          curves, the gaps where a matrix does not exist, the readings and the figures for the export.
 */

#include "plots/port_matrix_view.h"

#include "simulation/s_parameters.h"
#include "spice/spice_value.h"
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace GUI {

namespace {

/// Digits of a reading, enough to tell 50 ohm from 51.
constexpr int READING_DIGITS = 4;

/// Share of the decades a logarithmic axis spans that it leaves free above and below, and the least it leaves.
constexpr double DECADE_PADDING_SHARE = 0.1;
constexpr double MIN_PADDING_DECADES = 0.5;

constexpr double MISSING = std::numeric_limits<double>::quiet_NaN();

/**
 * @brief   Tells whether a frequency has its matrix, by looking at the first entry of it.
 */
bool HasMatrixAt(const Core::SParameters &result, const std::size_t frequency, const SParameterQuantity quantity) {
    return ReadMatrixValue(result, frequency, {1, 1}, quantity).has_value();
}

/**
 * @brief   Heads the figure of a matrix: its frequency axis, shared by every panel.
 */
ExportFigure MakeFigure(const Core::SParameters &result) {
    ExportFigure figure;
    figure.XAxis = {.Name = "Frequency", .Unit = "Hz"};
    figure.XLogarithmic = true;
    figure.Xs = result.Frequencies;
    return figure;
}

} // namespace

/**
 * @brief   Names a quantity by the letter of its matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  'S', 'Z' or 'Y'.
 */
char GetQuantityLetter(const SParameterQuantity quantity) {
    switch (quantity) {
    case SParameterQuantity::Z:
        return 'Z';
    case SParameterQuantity::Y:
        return 'Y';
    case SParameterQuantity::S:
    default:
        return 'S';
    }
}

/**
 * @brief   Gives the unit that the entries of a matrix are measured in.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  "Ohm" for Z, "S" (siemens) for Y, and "dB" for the magnitude of S.
 */
std::string GetQuantityUnit(const SParameterQuantity quantity) {
    switch (quantity) {
    case SParameterQuantity::Z:
        return "Ohm";
    case SParameterQuantity::Y:
        return "S";
    case SParameterQuantity::S:
    default:
        return "dB";
    }
}

/**
 * @brief   Tells whether a result carries a matrix.
 * @param[in] result    Result of the sweep.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  True for S always; for Z and Y, true when the backend gave the matrices, whether or not a frequency has one.
 */
bool IsQuantityAvailable(const Core::SParameters &result, const SParameterQuantity quantity) {
    return quantity == SParameterQuantity::S || result.HasPortMatrices();
}

/**
 * @brief   Words why Z or Y is not offered for a result.
 * @param[in] quantity  Impedance or admittance.
 * @return  One sentence for a tooltip: the engine in use does not give the matrix and the Nodalys engine does.
 */
std::string DescribeUnavailableQuantity(const SParameterQuantity quantity) {
    return std::format("The engine in use does not give the {} matrix of the ports; the Nodalys engine does",
                       quantity == SParameterQuantity::Y ? "admittance (Y)" : "impedance (Z)");
}

/**
 * @brief   Names an entry of a matrix as a datasheet does.
 * @param[in] entry     Entry of the matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  "Z21" for one-digit ports, and "Z1,10" when a port number needs two digits.
 */
std::string GetMatrixEntryName(const SParameterEntry &entry, const SParameterQuantity quantity) {
    if (entry.Row < 10 && entry.Column < 10) {
        return std::format("{}{}{}", GetQuantityLetter(quantity), entry.Row, entry.Column);
    }
    return std::format("{}{},{}", GetQuantityLetter(quantity), entry.Row, entry.Column);
}

/**
 * @brief   Reads one entry of a matrix at one frequency.
 * @param[in] result     Result of the sweep.
 * @param[in] frequency  Index of the frequency.
 * @param[in] entry      Entry to read; it must lie inside the matrix.
 * @param[in] quantity   Scattering, impedance or admittance.
 * @return  The value; none for Z and Y where the circuit has no finite matrix, or the backend gave none.
 */
std::optional<std::complex<double>> ReadMatrixValue(const Core::SParameters &result,
                                                    const std::size_t frequency,
                                                    const SParameterEntry &entry,
                                                    const SParameterQuantity quantity) {
    switch (quantity) {
    case SParameterQuantity::Z:
        return result.GetImpedance(frequency, entry.Row, entry.Column);
    case SParameterQuantity::Y:
        return result.GetAdmittance(frequency, entry.Row, entry.Column);
    case SParameterQuantity::S:
    default:
        return result.Get(frequency, entry.Row, entry.Column);
    }
}

/**
 * @brief   Counts the frequencies at which a matrix does not exist.
 * @param[in] result    Result of the sweep.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  The number of frequencies without it; 0 for S, and for Z and Y when the backend gave none, since nothing is
 *          then drawn in its place either.
 */
std::size_t CountAbsentFrequencies(const Core::SParameters &result, const SParameterQuantity quantity) {
    if (!IsQuantityAvailable(result, quantity) || quantity == SParameterQuantity::S) {
        return 0;
    }
    std::size_t absent = 0;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        if (!HasMatrixAt(result, frequency, quantity)) {
            ++absent;
        }
    }
    return absent;
}

/**
 * @brief   Tells whether a matrix exists at no frequency of the sweep, so nothing of it can be drawn.
 * @param[in] result    Result of the sweep.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  True for Z or Y missing at every frequency; false for S, which always exists, and for an empty sweep.
 */
bool AreAllFrequenciesAbsent(const Core::SParameters &result, const SParameterQuantity quantity) {
    return !result.Frequencies.empty() && CountAbsentFrequencies(result, quantity) == result.Frequencies.size();
}

/**
 * @brief   Words the frequencies at which a matrix is missing.
 * @param[in] result    Result of the sweep.
 * @param[in] quantity  Impedance or admittance.
 * @return  When it exists nowhere, the sentence that names the usual cause, which the plot area shows in place of the
 *          plots; when it is missing at some frequencies, a line that tells the curves have gaps; empty when it exists
 *          at all of them.
 */
std::string DescribeAbsentFrequencies(const Core::SParameters &result, const SParameterQuantity quantity) {
    const std::size_t absent = CountAbsentFrequencies(result, quantity);
    if (absent == 0) {
        return "";
    }
    const bool is_impedance = quantity == SParameterQuantity::Z;
    if (AreAllFrequenciesAbsent(result, quantity)) {
        return is_impedance ? "This circuit has no finite impedance matrix at any frequency of the sweep, as when the "
                              "ports are joined by a series element and nothing else goes to ground: leaving one port "
                              "open then leaves the other one's voltage undefined."
                            : "This circuit has no finite admittance matrix at any frequency of the sweep, as when "
                              "the ports are one wire with only elements to ground: shorting one port then forces the "
                              "voltage of the other.";
    }
    return std::format("{} has no finite value at {} of {} frequencies, so its curves have gaps there.",
                       is_impedance ? "Z" : "Y",
                       absent,
                       result.Frequencies.size());
}

/**
 * @brief   Lists the complex value of a matrix entry at every frequency.
 * @param[in] result    Result of the sweep.
 * @param[in] entry     Entry to list; it must lie inside the matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  Complex entry per frequency, with NaN real and imaginary parts where Z or Y is missing.
 */
std::vector<std::complex<double>>
ListMatrixValues(const Core::SParameters &result, const SParameterEntry &entry, const SParameterQuantity quantity) {
    if (quantity == SParameterQuantity::S) {
        return ListValues(result, entry);
    }
    std::vector<std::complex<double>> values;
    values.reserve(result.Frequencies.size());
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        const std::optional<std::complex<double>> value = ReadMatrixValue(result, frequency, entry, quantity);
        values.push_back(value.value_or(std::complex<double>{MISSING, MISSING}));
    }
    return values;
}

/**
 * @brief   Lists the magnitude of an entry at every frequency.
 * @param[in] result    Result of the sweep.
 * @param[in] entry     Entry to list; it must lie inside the matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  Decibels for S; ohms or siemens for Z and Y, which a logarithmic axis plots, so a zero or a missing matrix
 *          reads as not a number, where the curve breaks.
 */
std::vector<double>
ListMatrixMagnitudes(const Core::SParameters &result, const SParameterEntry &entry, const SParameterQuantity quantity) {
    if (quantity == SParameterQuantity::S) {
        return ListMagnitudesDecibels(result, entry);
    }
    std::vector<double> values;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        const std::optional<std::complex<double>> value = ReadMatrixValue(result, frequency, entry, quantity);
        const double magnitude = value ? std::abs(*value) : MISSING;
        values.push_back(magnitude > 0.0 && std::isfinite(magnitude) ? magnitude : MISSING);
    }
    return values;
}

/**
 * @brief   Lists the angle of an entry at every frequency.
 * @param[in] result    Result of the sweep.
 * @param[in] entry     Entry to list; it must lie inside the matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  Degrees from -180 to 180; not a number where a matrix is missing.
 */
std::vector<double>
ListMatrixAngles(const Core::SParameters &result, const SParameterEntry &entry, const SParameterQuantity quantity) {
    if (quantity == SParameterQuantity::S) {
        return ListAnglesDegrees(result, entry);
    }
    std::vector<double> values;
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        const std::optional<std::complex<double>> value = ReadMatrixValue(result, frequency, entry, quantity);
        values.push_back(value ? Core::ToDegrees(*value) : MISSING);
    }
    return values;
}

/**
 * @brief   Works out the range a logarithmic axis fits to.
 * @param[in] traces  The magnitudes drawn; zeros and not-a-number gaps are left out.
 * @return  From a tenth of the decades the finite positive values span, and at least half a decade, below the lowest to
 *          as much above the highest. One constant value, or the range when no value is left, gets half a decade each
 *          side of it, or 1 to 10. The padding is in decades, since the linear one of ImPlot would cross zero, which a
 *          logarithmic axis cannot show.
 */
ExportRange FindDecadeRange(const std::vector<std::vector<double>> &traces) {
    double lowest = std::numeric_limits<double>::infinity();
    double highest = 0.0;
    for (const std::vector<double> &trace : traces) {
        for (const double value : trace) {
            if (value > 0.0 && std::isfinite(value)) {
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
        }
    }
    if (highest <= 0.0) {
        return {.From = 1.0, .To = 10.0};
    }
    const double padding_decades = std::max(std::log10(highest / lowest) * DECADE_PADDING_SHARE, MIN_PADDING_DECADES);
    return {.From = lowest / std::pow(10.0, padding_decades), .To = highest * std::pow(10.0, padding_decades)};
}

/**
 * @brief   Words the reading of an entry of Z or Y under the cursor, one line each.
 * @param[in] result     Result of the sweep.
 * @param[in] frequency  Index of the frequency.
 * @param[in] entry      Entry to read.
 * @param[in] quantity   Scattering, impedance or admittance; S gives the reading of its figure.
 * @return  The entry and frequency, the magnitude and angle, and the real and imaginary parts; or that the matrix has
 *          no finite value at that frequency.
 */
std::vector<std::string> DescribeMatrixReading(const Core::SParameters &result,
                                               const std::size_t frequency,
                                               const SParameterEntry &entry,
                                               const SParameterQuantity quantity) {
    if (quantity == SParameterQuantity::S) {
        return DescribeReading(ReadEntry(result, frequency, entry));
    }
    const std::string name = GetMatrixEntryName(entry, quantity);
    const std::string unit = GetQuantityUnit(quantity);
    std::vector<std::string> lines;
    lines.push_back(std::format("{} at {}Hz", name, Core::FormatValue(result.Frequencies[frequency])));
    const std::optional<std::complex<double>> value = ReadMatrixValue(result, frequency, entry, quantity);
    if (!value) {
        lines.push_back(std::format("No finite {} matrix at this frequency",
                                    quantity == SParameterQuantity::Z ? "impedance" : "admittance"));
        return lines;
    }
    lines.push_back(std::format("{:.{}g} {} at {:.{}g} deg",
                                std::abs(*value),
                                READING_DIGITS,
                                unit,
                                Core::ToDegrees(*value),
                                READING_DIGITS));
    lines.push_back(std::format("{:.{}g} {} j{:.{}g} {}",
                                value->real(),
                                READING_DIGITS,
                                value->imag() < 0.0 ? "-" : "+",
                                std::abs(value->imag()),
                                READING_DIGITS,
                                unit));
    return lines;
}

/**
 * @brief   Joins the readings of several entries at one frequency into the text of a tooltip.
 * @param[in] result     Result of the sweep.
 * @param[in] frequency  Index of the frequency.
 * @param[in] entries    Entries to read.
 * @param[in] quantity   Scattering, impedance or admittance.
 * @return  The lines of each entry, as DescribeMatrixReading() words them, with a blank line between entries; empty
 *          without entries.
 */
std::string DescribeMatrixReadings(const Core::SParameters &result,
                                   const std::size_t frequency,
                                   const std::vector<SParameterEntry> &entries,
                                   const SParameterQuantity quantity) {
    std::string text;
    for (const SParameterEntry &entry : entries) {
        for (const std::string &line : DescribeMatrixReading(result, frequency, entry, quantity)) {
            text += (text.empty() ? "" : "\n") + line;
        }
        text += "\n";
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

/**
 * @brief   Builds the figure of the magnitude and phase plots of the shown entries of a matrix.
 * @param[in] result    Result of the sweep.
 * @param[in] entries   Entries to draw, which must lie inside the matrix.
 * @param[in] view      Frequencies the plots show, or none for the whole sweep.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  For S, the figure of BuildMagnitudePhaseFigure(); for Z and Y a magnitude panel in ohms or siemens on a
 *          logarithmic axis over a phase panel in degrees, whose curves break where the matrix is missing.
 */
ExportFigure BuildMatrixMagnitudePhaseFigure(const Core::SParameters &result,
                                             const std::vector<SParameterEntry> &entries,
                                             const std::optional<ExportRange> &view,
                                             const SParameterQuantity quantity) {
    if (quantity == SParameterQuantity::S) {
        return BuildMagnitudePhaseFigure(result, entries, view);
    }
    ExportFigure figure = MakeFigure(result);
    figure.XRange = view;
    const std::string unit = GetQuantityUnit(quantity);
    const std::string_view what = quantity == SParameterQuantity::Z ? "Impedance" : "Admittance";
    std::vector<std::vector<double>> magnitudes;
    for (const SParameterEntry &entry : entries) {
        magnitudes.push_back(ListMatrixMagnitudes(result, entry, quantity));
    }
    ExportPanel magnitude{.Title = "Magnitude",
                          .YAxes = {{.Name = std::format("{} magnitude ({})", what, unit),
                                     .Unit = unit,
                                     .Logarithmic = true,
                                     .Range = FindDecadeRange(magnitudes)}}};
    ExportPanel phase{.Title = "Phase", .YAxes = {{.Name = "Phase (deg)", .Unit = "deg", .UsesPrefixes = false}}};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::string name = GetMatrixEntryName(entries[index], quantity);
        const ExportColor color = GetEntryColor(index);
        magnitude.Series.push_back({.Name = name,
                                    .LegendLabel = name,
                                    .Values = ListMatrixMagnitudes(result, entries[index], quantity),
                                    .Color = color});
        phase.Series.push_back({.Name = name,
                                .LegendLabel = name,
                                .Values = ListMatrixAngles(result, entries[index], quantity),
                                .Color = color});
    }
    figure.Panels.push_back(std::move(magnitude));
    figure.Panels.push_back(std::move(phase));
    return figure;
}

/**
 * @brief   Builds an `ExportFigure` of the Polar view of checked S, Z or Y matrix entries.
 * @param[in] result    Result of the sweep.
 * @param[in] entries   Entries to draw, which must lie inside the matrix.
 * @param[in] quantity  Scattering, impedance or admittance.
 * @return  Export figure carrying `ComplexPlane` with `POLAR` kind.
 */
ExportFigure BuildMatrixPolarFigure(const Core::SParameters &result,
                                    const std::vector<SParameterEntry> &entries,
                                    const SParameterQuantity quantity) {
    const std::string unit = GetQuantityUnit(quantity);
    ExportComplexPlane plane;
    plane.Title = std::format("{}-parameters polar plot", GetQuantityLetter(quantity));
    plane.Kind = ComplexPlaneKind::POLAR;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::string name = GetMatrixEntryName(entries[index], quantity);
        plane.Traces.push_back({.Name = name,
                                .LegendLabel = name,
                                .Unit = unit,
                                .Color = GetEntryColor(index),
                                .Frequencies = result.Frequencies,
                                .Values = ListMatrixValues(result, entries[index], quantity)});
    }
    ExportFigure figure;
    figure.ComplexPlane = std::move(plane);
    return figure;
}

/**
 * @brief   Builds the figure whose table lists the real and imaginary parts of the shown entries of Z or Y.
 * @param[in] result    Result of the sweep.
 * @param[in] entries   Entries to list, which must lie inside the matrix.
 * @param[in] quantity  Impedance or admittance.
 * @return  One panel with two series per entry, real then imaginary, in ohms or siemens, with the cells of a frequency
 *          without a matrix empty. The table of S is the one of the Smith figure.
 */
ExportFigure BuildMatrixTableFigure(const Core::SParameters &result,
                                    const std::vector<SParameterEntry> &entries,
                                    const SParameterQuantity quantity) {
    ExportFigure figure = MakeFigure(result);
    ExportPanel panel{.Title = "Table", .YAxes = {{.Name = "Value", .Unit = GetQuantityUnit(quantity)}}};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const std::string name = GetMatrixEntryName(entries[index], quantity);
        ExportSeries real{.Name = name + " real", .Color = GetEntryColor(index)};
        ExportSeries imaginary{.Name = name + " imaginary", .Color = GetEntryColor(index)};
        for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
            const std::optional<std::complex<double>> value =
                ReadMatrixValue(result, frequency, entries[index], quantity);
            real.Values.push_back(value ? value->real() : MISSING);
            imaginary.Values.push_back(value ? value->imag() : MISSING);
        }
        panel.Series.push_back(std::move(real));
        panel.Series.push_back(std::move(imaginary));
    }
    figure.Panels.push_back(std::move(panel));
    return figure;
}

} // namespace GUI
