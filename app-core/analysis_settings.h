/**
 * @file    analysis_settings.h
 * @brief   What each analysis is asked to do: time spans, frequency ranges, sweeps, inputs and outputs.
 */

#ifndef IMCSIM_ANALYSIS_SETTINGS_H
#define IMCSIM_ANALYSIS_SETTINGS_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  TransientSettings
 * @brief   Time span and resolution of a transient simulation.
 */
struct TransientSettings {
    double StopTime = 10e-3;
    // Largest step between output points; ngspice may take smaller ones where the signals change fast
    double TimeStep = 10e-6;

    bool operator==(const TransientSettings &) const = default;
};

/**
 * @struct  ACSweepSettings
 * @brief   Frequency range and resolution of an AC sweep, with points spaced logarithmically.
 */
struct ACSweepSettings {
    double StartFrequency = 1.0;
    double StopFrequency = 1e6;
    int PointsPerDecade = 20;

    bool operator==(const ACSweepSettings &) const = default;
};

/**
 * @struct  SweepRange
 * @brief   A source and the values a DC sweep takes it through, in the unit of the source (volts or amperes).
 * @details Source is the name of a voltage source, a current source or a VCC rail. The values go from Start to
 *          Stop in steps of Step, which may be negative to sweep downward.
 */
struct SweepRange {
    std::string Source;
    double Start = 0.0;
    double Stop = 5.0;
    double Step = 0.1;

    bool operator==(const SweepRange &) const = default;
};

/**
 * @struct  DCSweepSettings
 * @brief   The source a DC sweep runs through its values and, optionally, a second source stepped once per curve.
 * @details With a second source, the whole sweep repeats for each of its values, which draws a family of curves,
 *          such as the collector characteristics of a transistor for several base currents.
 */
struct DCSweepSettings {
    SweepRange Swept;
    std::optional<SweepRange> Stepped;
};

/**
 * @struct  ParameterValue
 * @brief   The value a stepped analysis gives its parameter for one run: the value of a part, or the temperature.
 * @details Part names a resistor, a capacitor or an inductor, whose value becomes Value for the run; empty, Value is
 *          the temperature of the run in degrees Celsius instead of the 27 C ngspice simulates at.
 */
struct ParameterValue {
    std::string Part;
    double Value = 27.0;
};

/**
 * @struct  NoiseSettings
 * @brief   Where a noise analysis measures, what it refers the noise to, and its frequency range.
 * @details The input source must be an independent source set to AC; ngspice only uses it to find the gain from it
 *          to the output, whatever its AC magnitude. Points are spaced logarithmically, as in an AC sweep.
 */
struct NoiseSettings {
    int OutputNode = 0;
    std::string InputSource;
    double StartFrequency = 10.0;
    double StopFrequency = 100e3;
    int PointsPerDecade = 20;

    bool operator==(const NoiseSettings &) const = default;
};

/**
 * @struct  TransferFunctionSettings
 * @brief   Where a transfer function analysis measures its output, and which source drives the circuit.
 * @details The input must be an independent source, a voltage source, current source or supply rail; ngspice uses
 *          it only to find the gain from it to the output, whatever its value or waveform.
 */
struct TransferFunctionSettings {
    int OutputNode = 0;
    std::string InputSource;

    bool operator==(const TransferFunctionSettings &) const = default;
};

/**
 * @struct  TruthTableSettings
 * @brief   Which logic inputs a truth table varies and which nodes it reads.
 * @details Inputs are named logic inputs, the first being the most significant bit of the row number.
 */
struct TruthTableSettings {
    std::vector<std::string> Inputs;
    std::vector<int> OutputNodes;

    bool operator==(const TruthTableSettings &) const = default;
};

// Most inputs a truth table varies; each one doubles the number of runs
inline constexpr std::size_t MAX_TRUTH_TABLE_INPUTS = 10;

} // namespace Core

#endif // IMCSIM_ANALYSIS_SETTINGS_H
