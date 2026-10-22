/**
 * @file    analysis_suggestions.cpp
 * @brief   Analysis settings suggested from the parts of a circuit: transient times and the AC sweep range.
 */

#include "analysis_suggestions.h"

#include "components/clock.h"
#include "components/op_amp.h"
#include "components/source.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>
#include <vector>

namespace Core {

namespace {

// A transient shows this many periods of the slowest source, with this many points in a period of the fastest
constexpr double SuggestedPeriods = 5.0;
constexpr double PointsPerPeriod = 200.0;
// A waveform that happens once, such as an exponential or piecewise linear one, shows its whole span with this much
// room after it
constexpr double OneShotMargin = 1.2;
// The AC sweep reaches two decades past the lowest and the highest frequency the parts set
constexpr double ACMarginDecades = 2.0;
constexpr double MinACFrequency = 1e-3;
constexpr double MaxACFrequency = 1e10;

// Rounds to the closest 1, 2 or 5 times a power of ten, below or above the value, so suggestions read well
double RoundToNiceStep(const double value, const bool up) {
    const double decade = std::pow(10.0, std::floor(std::log10(value)));
    constexpr auto Steps = std::to_array({1.0, 2.0, 5.0, 10.0});
    if (up) {
        for (const double step : Steps) {
            if (step * decade >= value * (1.0 - 1e-9)) {
                return step * decade;
            }
        }
        return 10.0 * decade;
    }
    for (const double step : Steps | std::views::reverse) {
        if (step * decade <= value * (1.0 + 1e-9)) {
            return step * decade;
        }
    }
    return decade;
}

std::vector<double> ListPartValues(const Circuit &circuit, const ComponentType type) {
    std::vector<double> values;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetType() == type) {
            values.push_back(entry.Part->GetValue());
        }
    }
    return values;
}

// Corners of every resistor with every capacitor or inductor, resonances of every inductor with every capacitor,
// and the gain-bandwidth product of every op-amp
std::vector<double> ListCharacteristicFrequencies(const Circuit &circuit) {
    const std::vector<double> resistances = ListPartValues(circuit, ComponentType::Resistor);
    const std::vector<double> capacitances = ListPartValues(circuit, ComponentType::Capacitor);
    const std::vector<double> inductances = ListPartValues(circuit, ComponentType::Inductor);
    constexpr double TwoPi = 2.0 * std::numbers::pi;
    std::vector<double> frequencies;
    for (const double capacitance : capacitances) {
        for (const double resistance : resistances) {
            frequencies.push_back(1.0 / (TwoPi * resistance * capacitance));
        }
        for (const double inductance : inductances) {
            frequencies.push_back(1.0 / (TwoPi * std::sqrt(inductance * capacitance)));
        }
    }
    for (const double inductance : inductances) {
        for (const double resistance : resistances) {
            frequencies.push_back(resistance / (TwoPi * inductance));
        }
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetType() == ComponentType::OpAmp) {
            const auto &op_amp = static_cast<const OpAmp &>(*entry.Part);
            frequencies.push_back(op_amp.IsIdeal() ? op_amp.GetIdealBandwidth() : op_amp.GetParameters().GainBandwidth);
        }
    }
    return frequencies;
}

// What the sources of a circuit say about time: the periods of the repeating waveforms, the spans of those that
// happen once, and the longest delay before a waveform starts
struct Waveforms {
    std::vector<double> Periods;
    std::vector<double> Spans;
    double LongestDelay = 0.0;
};

void AddSourceWaveform(const Source &source, Waveforms &waveforms) {
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        break;
    case Source::SourceType::AC:
        waveforms.Periods.push_back(1.0 / source.GetAC().Frequency);
        break;
    case Source::SourceType::Pulse:
        waveforms.Periods.push_back(source.GetPulse().Period);
        waveforms.LongestDelay = std::max(waveforms.LongestDelay, source.GetPulse().Delay);
        break;
    case Source::SourceType::EXP: {
        const EXPParameters &exp = source.GetEXP();
        waveforms.Spans.push_back(exp.FallDelay + 5.0 * exp.FallTimeConstant);
        break;
    }
    case Source::SourceType::SFFM:
        waveforms.Periods.push_back(1.0 / source.GetSFFM().CarrierFrequency);
        waveforms.Periods.push_back(1.0 / source.GetSFFM().SignalFrequency);
        break;
    case Source::SourceType::AM:
        waveforms.Periods.push_back(1.0 / source.GetAM().CarrierFrequency);
        waveforms.Periods.push_back(1.0 / source.GetAM().ModulatingFrequency);
        waveforms.LongestDelay = std::max(waveforms.LongestDelay, source.GetAM().Delay);
        break;
    case Source::SourceType::PWL: {
        const PWLParameters &pwl = source.GetPWL();
        if (pwl.Points.size() >= 2) {
            waveforms.Spans.push_back(pwl.Points.back().Time);
        }
        break;
    }
    }
}

// A clock is a periodic source like a pulse one, with its period and its start delay
void AddWaveform(const Component &part, Waveforms &waveforms) {
    if (part.GetType() == ComponentType::Clock) {
        const ClockParameters &clock = static_cast<const Clock &>(part).GetParameters();
        waveforms.Periods.push_back(1.0 / clock.Frequency);
        waveforms.LongestDelay = std::max(waveforms.LongestDelay, clock.StartDelay);
        return;
    }
    if (const auto *source = dynamic_cast<const Source *>(&part)) {
        AddSourceWaveform(*source, waveforms);
    }
}

} // namespace

/**
 * @brief   Suggests transient times from the waveforms of the sources of a circuit.
 * @param[in] circuit  Circuit to simulate.
 * @return  A stop time of five periods of the slowest source, after the longest pulse delay, and a step of a 200th
 *          of the period of the fastest, both rounded to 1, 2 or 5 times a power of ten; no value without a source
 *          with a waveform.
 * @note    A sine, pulse, clock, SFFM or AM source sets a period. The exponential and piecewise linear waveforms
 *          happen once, so the stop time covers their span with a little room, and the step a 200th of it.
 */
std::optional<TransientSettings> SuggestTransientSettings(const Circuit &circuit) {
    Waveforms waveforms;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        AddWaveform(*entry.Part, waveforms);
    }
    if (waveforms.Periods.empty() && waveforms.Spans.empty()) {
        return std::nullopt;
    }
    double stop_time = 0.0;
    double shortest = std::numeric_limits<double>::max();
    if (!waveforms.Periods.empty()) {
        const auto [shortest_period, longest_period] = std::ranges::minmax(waveforms.Periods);
        stop_time = waveforms.LongestDelay + SuggestedPeriods * longest_period;
        shortest = shortest_period;
    }
    if (!waveforms.Spans.empty()) {
        const auto [shortest_span, longest_span] = std::ranges::minmax(waveforms.Spans);
        stop_time = std::max(stop_time, OneShotMargin * longest_span);
        shortest = std::min(shortest, shortest_span);
    }
    return TransientSettings{.StopTime = RoundToNiceStep(stop_time, true),
                             .TimeStep = RoundToNiceStep(shortest / PointsPerPeriod, false)};
}

/**
 * @brief   Suggests an AC sweep range around the frequencies the parts of a circuit set.
 * @param[in] circuit  Circuit to simulate.
 * @return  A range from two decades below the lowest RC, RL or LC corner, or op-amp gain-bandwidth product, to
 *          two decades above the highest, on whole decades and within 1 mHz to 10 GHz; no value when no part sets
 *          a frequency. The points per decade keep their default.
 */
std::optional<ACSweepSettings> SuggestACSweepSettings(const Circuit &circuit) {
    const std::vector<double> frequencies = ListCharacteristicFrequencies(circuit);
    if (frequencies.empty()) {
        return std::nullopt;
    }
    const auto [lowest, highest] = std::ranges::minmax(frequencies);
    const double start = std::pow(10.0, std::floor(std::log10(lowest) - ACMarginDecades));
    const double stop = std::pow(10.0, std::ceil(std::log10(highest) + ACMarginDecades));
    return ACSweepSettings{.StartFrequency = std::clamp(start, MinACFrequency, MaxACFrequency),
                           .StopFrequency = std::clamp(stop, MinACFrequency, MaxACFrequency)};
}

} // namespace Core
