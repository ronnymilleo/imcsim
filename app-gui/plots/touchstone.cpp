/**
 * @file    touchstone.cpp
 * @brief   Writes an S-parameter result as a Touchstone version 1 file, the .sNp text that network analyzers and RF
 *          tools read.
 */

#include "plots/touchstone.h"

#include "simulation/s_parameters.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>

namespace GUI {

namespace {

/// Significant digits of every number, enough that the file reads back the sweep to the precision of the solver.
constexpr int NUMBER_DIGITS = 10;
/// A line of the data holds at most this many pairs of numbers, as the format asks.
constexpr int MAX_PAIRS_PER_LINE = 4;
/// Ports of a matrix written in a single line per frequency, in the order S11 S21 S12 S22.
constexpr int PORTS_IN_ONE_LINE = 2;

double GetUnitFactor(const TouchstoneFrequencyUnit unit) {
    switch (unit) {
    case TouchstoneFrequencyUnit::KHZ:
        return 1e3;
    case TouchstoneFrequencyUnit::MHZ:
        return 1e6;
    case TouchstoneFrequencyUnit::GHZ:
        return 1e9;
    case TouchstoneFrequencyUnit::HZ:
    default:
        return 1.0;
    }
}

const char *GetFormatCode(const TouchstoneFormat format) {
    switch (format) {
    case TouchstoneFormat::MAGNITUDE_ANGLE:
        return "MA";
    case TouchstoneFormat::DECIBEL_ANGLE:
        return "DB";
    case TouchstoneFormat::REAL_IMAGINARY:
    default:
        return "RI";
    }
}

/**
 * @brief   The two numbers a value is written as, such as the real and imaginary parts.
 */
std::string FormatPair(const std::complex<double> value, const TouchstoneFormat format) {
    double first = value.real();
    double second = value.imag();
    if (format == TouchstoneFormat::MAGNITUDE_ANGLE) {
        first = std::abs(value);
        second = Core::ToDegrees(value);
    } else if (format == TouchstoneFormat::DECIBEL_ANGLE) {
        first = Core::ToDecibels(value);
        second = Core::ToDegrees(value);
    }
    return std::format("{:.{}g} {:.{}g}", first, NUMBER_DIGITS, second, NUMBER_DIGITS);
}

/**
 * @brief   A two-port is a single line per frequency, in the order S11 S21 S12 S22, which is not the order of the
 *          matrix; the others are one row of the matrix at a time, split in lines of four values.
 */
std::string FormatPoint(const Core::SParameters &result, const std::size_t frequency, const double unit_factor,
                        const TouchstoneFormat format) {
    const int ports = result.GetPortCount();
    std::string text = std::format("{:.{}g}", result.Frequencies[frequency] / unit_factor, NUMBER_DIGITS);
    if (ports == PORTS_IN_ONE_LINE) {
        for (const int column : {1, 2}) {
            for (const int row : {1, 2}) {
                text += " " + FormatPair(result.Get(frequency, row, column), format);
            }
        }
        return text + "\n";
    }
    for (int row = 1; row <= ports; ++row) {
        // Every line after the first of the point is indented
        if (row > 1) {
            text += " ";
        }
        int pairs_in_line = 0;
        for (int column = 1; column <= ports; ++column) {
            if (pairs_in_line == MAX_PAIRS_PER_LINE) {
                text += "\n ";
                pairs_in_line = 0;
            }
            text += " " + FormatPair(result.Get(frequency, row, column), format);
            ++pairs_in_line;
        }
        text += "\n";
    }
    return text;
}

} // namespace

/**
 * @brief   Names a frequency unit as the header of the file writes it.
 * @param[in] unit  Unit of the frequencies.
 * @return  "Hz", "kHz", "MHz" or "GHz".
 */
const char *GetTouchstoneUnitName(const TouchstoneFrequencyUnit unit) {
    switch (unit) {
    case TouchstoneFrequencyUnit::KHZ:
        return "kHz";
    case TouchstoneFrequencyUnit::MHZ:
        return "MHz";
    case TouchstoneFrequencyUnit::GHZ:
        return "GHz";
    case TouchstoneFrequencyUnit::HZ:
    default:
        return "Hz";
    }
}

/**
 * @brief   Names a number format for the export popup.
 * @param[in] format  Format of the values.
 * @return  The code of the header, "RI", "MA" or "DB".
 */
const char *GetTouchstoneFormatName(const TouchstoneFormat format) {
    return GetFormatCode(format);
}

/**
 * @brief   Gives the extension of a Touchstone file, which counts the ports.
 * @param[in] port_count  Number of ports of the result.
 * @return  ".s2p" for two ports, ".s3p" for three, and so on.
 */
std::string GetTouchstoneExtension(const int port_count) {
    return std::format(".s{}p", port_count);
}

/**
 * @brief   Writes an S-parameter result as a Touchstone version 1 file.
 * @param[in] result   S matrix of the sweep.
 * @param[in] options  Frequency unit and number format.
 * @return  The text of the file, or why it cannot be written: nothing to write, or ports with different reference
 *          impedances, which version 1 cannot hold.
 * @details A comment line names the program, the option line is "# GHz S MA R 50" in the default options, and each
 *          frequency follows with its matrix. A two-port lists S11 S21 S12 S22; larger matrices list row by row, at
 *          most four values to a line, and the continuation lines are indented.
 */
std::expected<std::string, std::string> FormatTouchstone(const Core::SParameters &result,
                                                         const TouchstoneOptions &options) {
    if (result.Frequencies.empty() || result.GetPortCount() < 1) {
        return std::unexpected("There is nothing to export");
    }
    const double reference = result.ReferenceOhms.front();
    for (std::size_t port = 1; port < result.ReferenceOhms.size(); ++port) {
        if (result.ReferenceOhms[port] != reference) {
            return std::unexpected(std::format(
                "A Touchstone file holds one reference impedance, and port 1 has {} ohm while port {} has {} ohm; "
                "give the ports the same Z0 to export",
                Core::FormatValue(reference), port + 1, Core::FormatValue(result.ReferenceOhms[port])));
        }
    }
    const double unit_factor = GetUnitFactor(options.Unit);
    std::string text = std::format("! S-parameters from imcsim, {} ports\n", result.GetPortCount());
    if (result.GetPortCount() == PORTS_IN_ONE_LINE) {
        text += "! Frequency S11 S21 S12 S22\n";
    }
    text +=
        std::format("# {} S {} R {}\n", GetTouchstoneUnitName(options.Unit), GetFormatCode(options.Format), reference);
    for (std::size_t frequency = 0; frequency < result.Frequencies.size(); ++frequency) {
        text += FormatPoint(result, frequency, unit_factor, options.Format);
    }
    return text;
}

} // namespace GUI
