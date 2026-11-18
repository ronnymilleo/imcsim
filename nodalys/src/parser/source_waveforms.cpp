/**
 * @file    source_waveforms.cpp
 * @brief   Recognizes the waveform of an independent source line, such as PULSE(...), and adds the source it describes.
 */

#include "parser/source_waveforms.h"

#include "parser/spice_text.h"
#include "text/text_case.h"

#include <cstddef>

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

void AddPulse(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
              const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    PulseParameters parameters;
    Assign(parameters.Low, values, 0);
    Assign(parameters.High, values, 1);
    Assign(parameters.Delay, values, 2);
    Assign(parameters.RiseTime, values, 3);
    Assign(parameters.FallTime, values, 4);
    Assign(parameters.Width, values, 5);
    Assign(parameters.Period, values, 6);
    if (is_voltage) {
        builder.AddVpulse(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    } else {
        builder.AddIpulse(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    }
}

void AddSine(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
             const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    constexpr std::size_t DAMPING_INDEX = 4;
    SineParameters parameters;
    Assign(parameters.Offset, values, 0);
    Assign(parameters.Amplitude, values, 1);
    Assign(parameters.Frequency, values, 2);
    Assign(parameters.Phase, values, 3);
    parameters.Magnitude = levels.AcMagnitude > 0.0 ? levels.AcMagnitude : parameters.Amplitude;
    const bool is_damped = values.size() > DAMPING_INDEX && values[DAMPING_INDEX] != 0.0;
    if (is_voltage) {
        builder.AddVsine(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees, is_damped);
    } else {
        builder.AddIsine(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees, is_damped);
    }
}

void AddExp(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
            const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    ExpParameters parameters;
    Assign(parameters.InitialValue, values, 0);
    Assign(parameters.PeakValue, values, 1);
    Assign(parameters.RiseDelay, values, 2);
    Assign(parameters.RiseTimeConstant, values, 3);
    Assign(parameters.FallDelay, values, 4);
    Assign(parameters.FallTimeConstant, values, 5);
    if (is_voltage) {
        builder.AddVexp(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    } else {
        builder.AddIexp(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    }
}

void AddPwl(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
            const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    PwlParameters parameters;
    parameters.Points.clear();
    for (std::size_t index = 0; index + 1 < values.size(); index += 2) {
        parameters.Points.push_back({values[index], values[index + 1]});
    }
    if (is_voltage) {
        builder.AddVpwl(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    } else {
        builder.AddIpwl(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    }
}

void AddSffm(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
             const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    SffmParameters parameters;
    Assign(parameters.Offset, values, 0);
    Assign(parameters.Amplitude, values, 1);
    Assign(parameters.CarrierFrequency, values, 2);
    Assign(parameters.ModulationIndex, values, 3);
    Assign(parameters.SignalFrequency, values, 4);
    if (is_voltage) {
        builder.AddVsffm(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    } else {
        builder.AddIsffm(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    }
}

void AddAm(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
           const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    AmParameters parameters;
    Assign(parameters.Amplitude, values, 0);
    Assign(parameters.Offset, values, 1);
    Assign(parameters.ModulatingFrequency, values, 2);
    Assign(parameters.CarrierFrequency, values, 3);
    Assign(parameters.Delay, values, 4);
    if (is_voltage) {
        builder.AddVam(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    } else {
        builder.AddIam(name, positive, negative, parameters, levels.AcMagnitude, levels.AcPhaseDegrees);
    }
}

void AddNoise(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
              const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
    NoiseParameters parameters;
    parameters.Offset = levels.Dc;
    parameters.WhiteAmplitude = ValueOr(values, 0, 0.0);
    parameters.TimeStep = ValueOr(values, 1, 0.0);
    parameters.FlickerExponent = ValueOr(values, 2, DEFAULT_FLICKER_EXPONENT);
    parameters.FlickerAmplitude = ValueOr(values, 3, 0.0);
    parameters.TelegraphAmplitude = ValueOr(values, 4, 0.0);
    parameters.CaptureTime = ValueOr(values, 5, 0.0);
    parameters.EmissionTime = ValueOr(values, 6, 0.0);
    if (is_voltage) {
        builder.AddVnoise(name, positive, negative, parameters, levels.AcMagnitude);
    } else {
        builder.AddInoise(name, positive, negative, parameters, levels.AcMagnitude);
    }
}

void AddRandom(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
               const NodeId negative, const std::vector<double> &values, const SourceLevels &levels) {
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
    if (is_voltage) {
        builder.AddVrandom(name, positive, negative, parameters, levels.AcMagnitude);
    } else {
        builder.AddIrandom(name, positive, negative, parameters, levels.AcMagnitude);
    }
}

} // namespace

/**
 * @brief   Tells which waveform a token of a source line starts, such as "SIN(0 1 1k)".
 * @param[in] token  Token as written; the case does not matter.
 * @return  The waveform, or NONE when the token is not the name of one followed by an opening parenthesis.
 */
SourceWaveform FindSourceWaveform(const std::string_view token) {
    const std::string upper = ToUpper(token);
    if (upper.starts_with("PULSE(")) {
        return SourceWaveform::PULSE;
    }
    if (upper.starts_with("SIN(") || upper.starts_with("SINE(")) {
        return SourceWaveform::SINE;
    }
    if (upper.starts_with("EXP(")) {
        return SourceWaveform::EXP;
    }
    if (upper.starts_with("PWL(")) {
        return SourceWaveform::PWL;
    }
    if (upper.starts_with("SFFM(")) {
        return SourceWaveform::SFFM;
    }
    if (upper.starts_with("AM(")) {
        return SourceWaveform::AM;
    }
    if (upper.starts_with("TRNOISE(")) {
        return SourceWaveform::TRNOISE;
    }
    if (upper.starts_with("TRRANDOM(")) {
        return SourceWaveform::TRRANDOM;
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
void AddWaveformSource(CircuitBuilder &builder, const std::string &name, const bool is_voltage, const NodeId positive,
                       const NodeId negative, const SourceWaveform waveform, const std::vector<double> &values,
                       const SourceLevels &levels) {
    switch (waveform) {
    case SourceWaveform::PULSE:
        AddPulse(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::SINE:
        AddSine(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::EXP:
        AddExp(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::PWL:
        AddPwl(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::SFFM:
        AddSffm(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::AM:
        AddAm(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::TRNOISE:
        AddNoise(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::TRRANDOM:
        AddRandom(builder, name, is_voltage, positive, negative, values, levels);
        break;
    case SourceWaveform::NONE:
    default:
        if (is_voltage) {
            builder.AddVdc(name, positive, negative, levels.Dc, levels.AcMagnitude, levels.AcPhaseDegrees);
        } else {
            builder.AddIdc(name, positive, negative, levels.Dc, levels.AcMagnitude, levels.AcPhaseDegrees);
        }
        break;
    }
}

} // namespace Nodalys
