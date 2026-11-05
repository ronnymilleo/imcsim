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
    /// Largest step between output points; ngspice may take smaller ones where the signals change fast.
    double TimeStep = 10e-6;
    /// Seed of the random numbers of the Random sources and the telegraph part of the Noise sources, so a run repeats;
    /// 0 draws new ones each run. The white and 1/f noise of a Noise source change each run whatever the seed, since
    /// ngspice does not seed them.
    unsigned int Seed = 0;

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
 * @details The input must be an independent source: a voltage or current source, supply rail, clock or logic input;
 *          ngspice uses it only to find the gain from it to the output, whatever its value or waveform.
 */
struct TransferFunctionSettings {
    int OutputNode = 0;
    std::string InputSource;

    bool operator==(const TransferFunctionSettings &) const = default;
};

/**
 * @struct  PoleZeroSettings
 * @brief   Where a pole-zero analysis measures its output, and which source drives the circuit.
 * @details The input must be an independent source: a voltage or current source, supply rail, clock or logic input.
 *          Its terminals are the input port, and the kind of source tells ngspice whether the input is a voltage or a
 *          current, since poles and zeros depend on what the port is held to.
 */
struct PoleZeroSettings {
    int OutputNode = 0;
    std::string InputSource;

    bool operator==(const PoleZeroSettings &) const = default;
};

/// Default range of the S-parameter analysis, where the parts of a radio-frequency circuit work.
constexpr double DEFAULT_S_START_FREQUENCY = 1e6;
constexpr double DEFAULT_S_STOP_FREQUENCY = 1e9;

/**
 * @struct  SParameterSettings
 * @brief   Frequency range and resolution of an S-parameter analysis.
 * @details The ports are the pin markers of the schematic that are set as ports, so there is nothing else to pick. A
 *          logarithmic sweep takes PointsPerDecade points per decade; a linear one takes LinearPoints points in all,
 *          both ends included, and at least 3, since ngspice gives one point for two.
 */
struct SParameterSettings {
    double StartFrequency = DEFAULT_S_START_FREQUENCY;
    double StopFrequency = DEFAULT_S_STOP_FREQUENCY;
    bool IsLogarithmic = true;
    int PointsPerDecade = 100;
    int LinearPoints = 101;

    bool operator==(const SParameterSettings &) const = default;
};

/**
 * @struct  SensitivitySettings
 * @brief   The node whose DC voltage a sensitivity analysis measures.
 * @details Every part with a value is varied one at a time around the DC operating point; there is nothing else to set.
 */
struct SensitivitySettings {
    int OutputNode = 0;

    bool operator==(const SensitivitySettings &) const = default;
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

/// Most inputs a truth table varies; each one doubles the number of runs.
inline constexpr std::size_t MAX_TRUTH_TABLE_INPUTS = 10;

} // namespace Core

#endif // IMCSIM_ANALYSIS_SETTINGS_H
