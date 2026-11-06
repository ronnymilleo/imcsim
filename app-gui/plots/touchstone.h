/**
 * @file    touchstone.h
 * @brief   Writes an S-parameter result as a Touchstone version 1 file, the .sNp text that network analyzers and RF
 *          tools read.
 * @details Pure text, with no ImGui. The file holds one reference impedance, so a result whose ports differ is refused.
 */

#ifndef IMCSIM_TOUCHSTONE_H
#define IMCSIM_TOUCHSTONE_H

#include "simulation/simulation_results.h"
#include <array>
#include <expected>
#include <string>

namespace GUI {

/**
 * @enum    TouchstoneFormat
 * @brief   How each complex value is written: real and imaginary parts, magnitude and angle, or decibels and angle.
 */
enum class TouchstoneFormat {
    REAL_IMAGINARY,
    MAGNITUDE_ANGLE,
    DECIBEL_ANGLE
};

/**
 * @enum    TouchstoneFrequencyUnit
 * @brief   The unit the frequencies of the file are written in.
 */
enum class TouchstoneFrequencyUnit {
    HZ,
    KHZ,
    MHZ,
    GHZ
};

/**
 * @struct  TouchstoneOptions
 * @brief   The format and frequency unit of the file; the defaults are the ones most tools write.
 */
struct TouchstoneOptions {
    TouchstoneFrequencyUnit Unit = TouchstoneFrequencyUnit::GHZ;
    TouchstoneFormat Format = TouchstoneFormat::MAGNITUDE_ANGLE;

    bool operator==(const TouchstoneOptions &) const = default;
};

/// Every unit and format, in the order the export popup lists them.
inline constexpr std::array<TouchstoneFrequencyUnit, 4> TOUCHSTONE_UNITS = {
    TouchstoneFrequencyUnit::HZ, TouchstoneFrequencyUnit::KHZ, TouchstoneFrequencyUnit::MHZ,
    TouchstoneFrequencyUnit::GHZ};
inline constexpr std::array<TouchstoneFormat, 3> TOUCHSTONE_FORMATS = {
    TouchstoneFormat::REAL_IMAGINARY, TouchstoneFormat::MAGNITUDE_ANGLE, TouchstoneFormat::DECIBEL_ANGLE};

// Names
const char *GetTouchstoneUnitName(TouchstoneFrequencyUnit unit);
const char *GetTouchstoneFormatName(TouchstoneFormat format);
std::string GetTouchstoneExtension(int port_count);

// File
std::expected<std::string, std::string> FormatTouchstone(const Core::SParameters &result,
                                                         const TouchstoneOptions &options);

} // namespace GUI

#endif // IMCSIM_TOUCHSTONE_H
