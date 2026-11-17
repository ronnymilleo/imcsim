/**
 * @file    source_waveforms.h
 * @brief   Recognizes the waveform of an independent source line, such as PULSE(...), and adds the source it describes.
 */

#ifndef NODALYS_PARSER_SOURCE_WAVEFORMS_H
#define NODALYS_PARSER_SOURCE_WAVEFORMS_H

#include "nodalys/circuit_builder.h"

#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @enum    SourceWaveform
 * @brief   The kinds of waveform a V or I source line can carry; NONE is a source with a DC value only.
 */
enum class SourceWaveform {
    NONE,
    PULSE,
    SINE,
    EXP,
    PWL,
    SFFM,
    AM,
    TRNOISE,
    TRRANDOM
};

/**
 * @struct  SourceLevels
 * @brief   The DC value and the small-signal magnitude and phase a source line gives besides its waveform.
 */
struct SourceLevels {
    double Dc = 0.0;
    double AcMagnitude = 0.0;
    double AcPhaseDegrees = 0.0;
};

// Reading
SourceWaveform FindSourceWaveform(std::string_view token);

// Building
void AddWaveformSource(CircuitBuilder &builder, const std::string &name, bool is_voltage, NodeId positive,
                       NodeId negative, SourceWaveform waveform, const std::vector<double> &values,
                       const SourceLevels &levels);

} // namespace Nodalys

#endif // NODALYS_PARSER_SOURCE_WAVEFORMS_H
