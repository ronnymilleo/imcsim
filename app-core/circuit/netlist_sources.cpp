/**
 * @file    netlist_sources.cpp
 * @brief   Writes the netlist line of an independent source, with its waveform.
 */

#include "circuit/netlist_sources.h"

#include "circuit/circuit_currents.h"
#include "circuit/circuit_netlist.h"
#include "circuit/components/bjt.h"
#include "circuit/components/component.h"
#include "circuit/components/diode.h"
#include "circuit/components/mosfet.h"
#include "circuit/components/source.h"
#include "circuit/netlist_logic.h"
#include "circuit/netlist_subcircuits.h"
#include "spice/spice_value.h"
#include <format>
#include <string>
#include <vector>

namespace Core {

namespace {

/**
 * @brief   The arguments of TRNOISE, with the parts that are off written as 0: ngspice ignores a source with no time
 *          step and no telegraph amplitude, and takes the step only for the white and 1/f noise.
 */
std::string FormatNoiseArguments(const NoiseParameters &noise) {
    const bool is_flicker_on = noise.FlickerAmplitude != 0.0;
    const bool is_telegraph_on = noise.TelegraphAmplitude != 0.0;
    const bool needs_step = noise.WhiteAmplitude != 0.0 || is_flicker_on;
    return std::format("{} {} {} {} {} {} {}", FormatSpiceValue(noise.WhiteAmplitude),
                       FormatSpiceValue(needs_step ? noise.TimeStep : 0.0),
                       FormatSpiceValue(is_flicker_on ? noise.FlickerExponent : 0.0),
                       FormatSpiceValue(noise.FlickerAmplitude), FormatSpiceValue(noise.TelegraphAmplitude),
                       FormatSpiceValue(is_telegraph_on ? noise.CaptureTime : 0.0),
                       FormatSpiceValue(is_telegraph_on ? noise.EmissionTime : 0.0));
}

} // namespace

/**
 * @brief   One line serves every analysis: .op reads the DC value, .ac the AC magnitude and .tran the waveform.
 * @details Every waveform writes the value it starts from, so the operating point matches the beginning of the
 *          transient. The name tells SPICE whether it is a voltage or current source.
 * @param[in] source  Independent source to write.
 * @param[in] nodes   Nodes of its two terminals, in terminal order.
 * @return  The netlist line of the source, without the line break.
 */
std::string FormatSource(const Source &source, const std::vector<int> &nodes) {
    const std::string start = std::format("{} {} {}", source.GetName(), nodes[0], nodes[1]);
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        return std::format("{} DC {}\n", start, FormatSpiceValue(source.GetValue()));
    case Source::SourceType::AC: {
        const ACParameters &ac = source.GetAC();
        const std::string offset = FormatSpiceValue(ac.Offset);
        return std::format("{} DC {} AC {} SIN({} {} {})\n", start, offset, FormatSpiceValue(ac.Magnitude), offset,
                           FormatSpiceValue(ac.Amplitude), FormatSpiceValue(ac.Frequency));
    }
    case Source::SourceType::PULSE: {
        const PulseParameters &pulse = source.GetPulse();
        const std::string low = FormatSpiceValue(pulse.Low);
        return std::format("{} DC {} PULSE({} {} {} {} {} {} {})\n", start, low, low, FormatSpiceValue(pulse.High),
                           FormatSpiceValue(pulse.Delay), FormatSpiceValue(pulse.RiseTime),
                           FormatSpiceValue(pulse.FallTime), FormatSpiceValue(pulse.Width),
                           FormatSpiceValue(pulse.Period));
    }
    case Source::SourceType::EXP: {
        const EXPParameters &exp = source.GetEXP();
        const std::string initial = FormatSpiceValue(exp.InitialValue);
        return std::format("{} DC {} EXP({} {} {} {} {} {})\n", start, initial, initial,
                           FormatSpiceValue(exp.PeakValue), FormatSpiceValue(exp.RiseDelay),
                           FormatSpiceValue(exp.RiseTimeConstant), FormatSpiceValue(exp.FallDelay),
                           FormatSpiceValue(exp.FallTimeConstant));
    }
    case Source::SourceType::SFFM: {
        const SFFMParameters &sffm = source.GetSFFM();
        const std::string offset = FormatSpiceValue(sffm.Offset);
        return std::format("{} DC {} SFFM({} {} {} {} {})\n", start, offset, offset, FormatSpiceValue(sffm.Amplitude),
                           FormatSpiceValue(sffm.CarrierFrequency), FormatSpiceValue(sffm.ModulationIndex),
                           FormatSpiceValue(sffm.SignalFrequency));
    }
    case Source::SourceType::AM: {
        const AMParameters &am = source.GetAM();
        const std::string offset = FormatSpiceValue(am.Offset);
        // ngspice reads AM(offset mod_offset amplitude mod_frequency carrier_frequency delay); the modulation offset
        // stays 0, so the source is the offset plus the amplitude times the two sines
        return std::format("{} DC {} AM({} 0 {} {} {} {})\n", start, offset, offset, FormatSpiceValue(am.Amplitude),
                           FormatSpiceValue(am.ModulatingFrequency), FormatSpiceValue(am.CarrierFrequency),
                           FormatSpiceValue(am.Delay));
    }
    case Source::SourceType::PWL: {
        const PWLParameters &pwl = source.GetPWL();
        std::string points;
        for (const PWLPoint &point : pwl.Points) {
            if (!points.empty()) {
                points += " ";
            }
            points += std::format("{} {}", FormatSpiceValue(point.Time), FormatSpiceValue(point.Value));
        }
        const std::string first = FormatSpiceValue(pwl.Points.front().Value);
        return std::format("{} DC {} PWL({})\n", start, first, points);
    }
    case Source::SourceType::NOISE:
        return std::format("{} DC {} TRNOISE({})\n", start, FormatSpiceValue(source.GetNoise().Offset),
                           FormatNoiseArguments(source.GetNoise()));
    case Source::SourceType::RANDOM: {
        const RandomParameters &random = source.GetRandom();
        // ngspice adds the DC value to what TRRANDOM gives, so the offset or mean is written once, as the DC value,
        // which also makes it the operating point; the offset of TRRANDOM itself stays 0
        return std::format("{} DC {} TRRANDOM({} {} {} {} 0)\n", start, FormatSpiceValue(random.Second),
                           static_cast<int>(random.Distribution), FormatSpiceValue(random.TimeStep),
                           FormatSpiceValue(random.Delay), FormatSpiceValue(random.First));
    }
    default:
        return "";
    }
}

} // namespace Core
