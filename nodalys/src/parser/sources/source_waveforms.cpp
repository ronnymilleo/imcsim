/**
 * @file    source_waveforms.cpp
 * @brief   Recognizes the waveform of an independent source line, such as PULSE(...), and adds the source it describes.
 */

#include "parser/sources/source_waveforms.h"

#include "parser/syntax/spice_text.h"
#include "text/text_case.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Nodalys {

namespace {

/// The first and last distribution numbers a TRRANDOM source accepts.
constexpr int FIRST_RANDOM_DISTRIBUTION = 1;
constexpr int LAST_RANDOM_DISTRIBUTION = 4;
/// What a TRRANDOM source takes for the values its line leaves out.
constexpr double DEFAULT_RANDOM_TIME_STEP = 10e-6;
constexpr double DEFAULT_RANDOM_FIRST = 0.1;
constexpr double DEFAULT_FLICKER_EXPONENT = 1.0;

/**
 * @struct  SourcePlacement
 * @brief   Where an independent source goes and what its line gives beside the waveform, the same for every waveform.
 */
struct SourcePlacement {
    CircuitBuilder &Builder;
    const std::string &Name;
    /// True for a V source, false for an I source.
    bool IsVoltage;
    NodeId Positive;
    NodeId Negative;
    const SourceLevels &Levels;
};

/**
 * @struct  WaveformPrefix
 * @brief   How a word of a source line starts when it is a waveform, with its opening parenthesis.
 */
struct WaveformPrefix {
    std::string_view Prefix;
    SourceWaveform Waveform;
};

/// The waveforms a source line may carry, by the upper-case start of their word.
constexpr auto WAVEFORM_PREFIXES = std::to_array<WaveformPrefix>({
    {.Prefix = "PULSE(", .Waveform = SourceWaveform::PULSE},
    {.Prefix = "SIN(", .Waveform = SourceWaveform::SINE},
    {.Prefix = "SINE(", .Waveform = SourceWaveform::SINE},
    {.Prefix = "EXP(", .Waveform = SourceWaveform::EXP},
    {.Prefix = "PWL(", .Waveform = SourceWaveform::PWL},
    {.Prefix = "SFFM(", .Waveform = SourceWaveform::SFFM},
    {.Prefix = "AM(", .Waveform = SourceWaveform::AM},
    {.Prefix = "TRNOISE(", .Waveform = SourceWaveform::TRNOISE},
    {.Prefix = "TRRANDOM(", .Waveform = SourceWaveform::TRRANDOM},
});

/**
 * @brief   Sets target to the value at index when the line gave one, so a value left out keeps the default of its
 * field.
 */
void Assign(double &target, const std::vector<double> &values, const std::size_t index) {
    if (values.size() > index) {
        target = values[index];
    }
}

double ValueOr(const std::vector<double> &values, const std::size_t index, const double fallback) {
    return values.size() > index ? values[index] : fallback;
}

/**
 * @brief   Adds the voltage or the current source of a waveform, with the small-signal magnitude and phase of the line.
 * @details The builder has one function for each kind, which take the same arguments, so the placement picks one.
 */
template <typename Argument>
void AddWithPhase(const SourcePlacement &source,
                  const std::type_identity_t<Argument> waveform,
                  CircuitBuilder &(CircuitBuilder::*voltage)(std::string, NodeId, NodeId, Argument, double, double),
                  CircuitBuilder &(CircuitBuilder::*current)(std::string, NodeId, NodeId, Argument, double, double)) {
    (source.Builder.*(source.IsVoltage ? voltage : current))(source.Name,
                                                             source.Positive,
                                                             source.Negative,
                                                             waveform,
                                                             source.Levels.AcMagnitude,
                                                             source.Levels.AcPhaseDegrees);
}

/**
 * @brief   Adds the voltage or the current source of a random waveform, which takes a small-signal magnitude alone.
 */
template <typename Argument>
void AddWithMagnitude(const SourcePlacement &source,
                      const std::type_identity_t<Argument> waveform,
                      CircuitBuilder &(CircuitBuilder::*voltage)(std::string, NodeId, NodeId, Argument, double),
                      CircuitBuilder &(CircuitBuilder::*current)(std::string, NodeId, NodeId, Argument, double)) {
    (source.Builder.*(source.IsVoltage ? voltage : current))(source.Name,
                                                             source.Positive,
                                                             source.Negative,
                                                             waveform,
                                                             source.Levels.AcMagnitude);
}

PulseParameters ReadPulse(const std::vector<double> &values) {
    PulseParameters parameters;
    Assign(parameters.Low, values, 0);
    Assign(parameters.High, values, 1);
    Assign(parameters.Delay, values, 2);
    Assign(parameters.RiseTime, values, 3);
    Assign(parameters.FallTime, values, 4);
    Assign(parameters.Width, values, 5);
    Assign(parameters.Period, values, 6);
    return parameters;
}

/**
 * @brief   Reads a sine, whose small-signal magnitude is the AC one of the line or, without one, its amplitude.
 */
SineParameters ReadSine(const std::vector<double> &values, const SourceLevels &levels) {
    SineParameters parameters;
    Assign(parameters.Offset, values, 0);
    Assign(parameters.Amplitude, values, 1);
    Assign(parameters.Frequency, values, 2);
    Assign(parameters.Phase, values, 3);
    parameters.Magnitude = levels.AcMagnitude > 0.0 ? levels.AcMagnitude : parameters.Amplitude;
    return parameters;
}

/**
 * @brief   Whether a sine fades, as a damping factor that is given and not zero asks.
 */
bool IsDampedSine(const std::vector<double> &values) {
    constexpr std::size_t DAMPING_INDEX = 4;
    return values.size() > DAMPING_INDEX && values[DAMPING_INDEX] != 0.0;
}

void AddSine(const SourcePlacement &source, const std::vector<double> &values) {
    const auto add = source.IsVoltage ? &CircuitBuilder::AddVsine : &CircuitBuilder::AddIsine;
    (source.Builder.*add)(source.Name,
                          source.Positive,
                          source.Negative,
                          ReadSine(values, source.Levels),
                          source.Levels.AcMagnitude,
                          source.Levels.AcPhaseDegrees,
                          IsDampedSine(values));
}

ExpParameters ReadExp(const std::vector<double> &values) {
    ExpParameters parameters;
    Assign(parameters.InitialValue, values, 0);
    Assign(parameters.PeakValue, values, 1);
    Assign(parameters.RiseDelay, values, 2);
    Assign(parameters.RiseTimeConstant, values, 3);
    Assign(parameters.FallDelay, values, 4);
    Assign(parameters.FallTimeConstant, values, 5);
    return parameters;
}

/**
 * @brief   Reads the points of a PWL source, a time and a value each; a time left without its value is dropped.
 */
PwlParameters ReadPwl(const std::vector<double> &values) {
    PwlParameters parameters;
    parameters.Points.clear();
    for (std::size_t index = 0; index + 1 < values.size(); index += 2) {
        parameters.Points.push_back({values[index], values[index + 1]});
    }
    return parameters;
}

SffmParameters ReadSffm(const std::vector<double> &values) {
    SffmParameters parameters;
    Assign(parameters.Offset, values, 0);
    Assign(parameters.Amplitude, values, 1);
    Assign(parameters.CarrierFrequency, values, 2);
    Assign(parameters.ModulationIndex, values, 3);
    Assign(parameters.SignalFrequency, values, 4);
    return parameters;
}

AmParameters ReadAm(const std::vector<double> &values) {
    AmParameters parameters;
    Assign(parameters.Amplitude, values, 0);
    Assign(parameters.Offset, values, 1);
    Assign(parameters.ModulatingFrequency, values, 2);
    Assign(parameters.CarrierFrequency, values, 3);
    Assign(parameters.Delay, values, 4);
    return parameters;
}

/**
 * @brief   Reads transient noise, which rides on the DC value of the line.
 */
NoiseParameters ReadNoise(const std::vector<double> &values, const SourceLevels &levels) {
    NoiseParameters parameters;
    parameters.Offset = levels.Dc;
    parameters.WhiteAmplitude = ValueOr(values, 0, 0.0);
    parameters.TimeStep = ValueOr(values, 1, 0.0);
    parameters.FlickerExponent = ValueOr(values, 2, DEFAULT_FLICKER_EXPONENT);
    parameters.FlickerAmplitude = ValueOr(values, 3, 0.0);
    parameters.TelegraphAmplitude = ValueOr(values, 4, 0.0);
    parameters.CaptureTime = ValueOr(values, 5, 0.0);
    parameters.EmissionTime = ValueOr(values, 6, 0.0);
    return parameters;
}

/**
 * @brief   Reads a random source; a distribution number out of range keeps the default one, and the DC value of the
 *          line shifts the second figure, as in ngspice.
 */
RandomParameters ReadRandom(const std::vector<double> &values, const SourceLevels &levels) {
    RandomParameters parameters;
    if (!values.empty()) {
        const int distribution = static_cast<int>(values[0]);
        if (distribution >= FIRST_RANDOM_DISTRIBUTION && distribution <= LAST_RANDOM_DISTRIBUTION) {
            parameters.Distribution = static_cast<RandomDistribution>(distribution);
        }
    }
    parameters.TimeStep = ValueOr(values, 1, DEFAULT_RANDOM_TIME_STEP);
    parameters.Delay = ValueOr(values, 2, 0.0);
    parameters.First = ValueOr(values, 3, DEFAULT_RANDOM_FIRST);
    parameters.Second = levels.Dc + ValueOr(values, 4, 0.0);
    return parameters;
}

} // namespace

/**
 * @brief   Tells which waveform a token of a source line starts, such as "SIN(0 1 1k)".
 * @param[in] token  Token as written; the case does not matter.
 * @return  The waveform, or NONE when the token is not the name of one followed by an opening parenthesis.
 */
SourceWaveform FindSourceWaveform(const std::string_view token) {
    const std::string upper = ToUpper(token);
    for (const WaveformPrefix &entry : WAVEFORM_PREFIXES) {
        if (upper.starts_with(entry.Prefix)) {
            return entry.Waveform;
        }
    }
    return SourceWaveform::NONE;
}

/**
 * @brief   Adds an independent source with the waveform and the numbers read from its parentheses.
 * @param[in,out] builder     Circuit being built.
 * @param[in]     name        Name of the source in the circuit.
 * @param[in]     is_voltage  True for a V source, false for an I source.
 * @param[in]     positive    Positive node.
 * @param[in]     negative    Negative node.
 * @param[in]     waveform    Kind of waveform; NONE adds a DC source.
 * @param[in]     values      Numbers inside the parentheses, in the order SPICE writes them; those left out keep the
 *                            default of the waveform.
 * @param[in]     levels      DC value and small-signal magnitude and phase of the line.
 */
void AddWaveformSource(CircuitBuilder &builder,
                       const std::string &name,
                       const bool is_voltage,
                       const NodeId positive,
                       const NodeId negative,
                       const SourceWaveform waveform,
                       const std::vector<double> &values,
                       const SourceLevels &levels) {
    const SourcePlacement source{.Builder = builder,
                                 .Name = name,
                                 .IsVoltage = is_voltage,
                                 .Positive = positive,
                                 .Negative = negative,
                                 .Levels = levels};
    switch (waveform) {
    case SourceWaveform::PULSE:
        AddWithPhase(source, ReadPulse(values), &CircuitBuilder::AddVpulse, &CircuitBuilder::AddIpulse);
        break;
    case SourceWaveform::SINE:
        AddSine(source, values);
        break;
    case SourceWaveform::EXP:
        AddWithPhase(source, ReadExp(values), &CircuitBuilder::AddVexp, &CircuitBuilder::AddIexp);
        break;
    case SourceWaveform::PWL:
        AddWithPhase(source, ReadPwl(values), &CircuitBuilder::AddVpwl, &CircuitBuilder::AddIpwl);
        break;
    case SourceWaveform::SFFM:
        AddWithPhase(source, ReadSffm(values), &CircuitBuilder::AddVsffm, &CircuitBuilder::AddIsffm);
        break;
    case SourceWaveform::AM:
        AddWithPhase(source, ReadAm(values), &CircuitBuilder::AddVam, &CircuitBuilder::AddIam);
        break;
    case SourceWaveform::TRNOISE:
        AddWithMagnitude(source, ReadNoise(values, levels), &CircuitBuilder::AddVnoise, &CircuitBuilder::AddInoise);
        break;
    case SourceWaveform::TRRANDOM:
        AddWithMagnitude(source, ReadRandom(values, levels), &CircuitBuilder::AddVrandom, &CircuitBuilder::AddIrandom);
        break;
    case SourceWaveform::NONE:
    default:
        AddWithPhase(source, levels.Dc, &CircuitBuilder::AddVdc, &CircuitBuilder::AddIdc);
        break;
    }
}

} // namespace Nodalys
