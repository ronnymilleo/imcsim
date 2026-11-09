/**
 * @file    waveforms.cpp
 * @brief   Evaluates independent source waveforms at DC and transient time points, matching SPICE definitions.
 */

#include "native/waveforms.h"

#include "components/clock.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Core::Native {

namespace {

/// A zero rise or fall time is an instantaneous step in SPICE.
double LinearEdge(const double from, const double to, const double duration, const double progress) {
    if (duration <= 0.0) {
        return to;
    }
    return from + (to - from) * std::min(progress / duration, 1.0);
}

} // namespace

double EvaluatePulse(const PulseParameters &pulse, const double time) {
    if (time < pulse.Delay) {
        return pulse.Low;
    }
    const double local = std::fmod(time - pulse.Delay, pulse.Period);
    if (local < pulse.RiseTime) {
        return LinearEdge(pulse.Low, pulse.High, pulse.RiseTime, local);
    }
    if (local < pulse.RiseTime + pulse.Width) {
        return pulse.High;
    }
    if (local < pulse.RiseTime + pulse.Width + pulse.FallTime) {
        return LinearEdge(pulse.High, pulse.Low, pulse.FallTime, local - pulse.RiseTime - pulse.Width);
    }
    return pulse.Low;
}

double EvaluateEXP(const EXPParameters &exp, const double time) {
    if (time < exp.RiseDelay) {
        return exp.InitialValue;
    }
    const double rise =
        (exp.PeakValue - exp.InitialValue) * (1.0 - std::exp(-(time - exp.RiseDelay) / exp.RiseTimeConstant));
    if (time < exp.FallDelay) {
        return exp.InitialValue + rise;
    }
    const double peak =
        exp.InitialValue +
        (exp.PeakValue - exp.InitialValue) * (1.0 - std::exp(-(exp.FallDelay - exp.RiseDelay) / exp.RiseTimeConstant));
    return exp.InitialValue + (peak - exp.InitialValue) * std::exp(-(time - exp.FallDelay) / exp.FallTimeConstant);
}

double EvaluateSine(const ACParameters &ac, const double time) {
    return ac.Offset + ac.Amplitude * std::sin(2.0 * std::numbers::pi * ac.Frequency * time);
}

double EvaluatePWL(const PWLParameters &pwl, const double time) {
    const std::vector<PWLPoint> &points = pwl.Points;
    if (time <= points.front().Time) {
        return points.front().Value;
    }
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (time <= points[index].Time) {
            const PWLPoint &before = points[index - 1];
            const PWLPoint &after = points[index];
            const double share = (time - before.Time) / (after.Time - before.Time);
            return before.Value + share * (after.Value - before.Value);
        }
    }
    return points.back().Value;
}

double EvaluateSFFM(const SFFMParameters &sffm, const double time) {
    const double phase = 2.0 * std::numbers::pi * sffm.CarrierFrequency * time +
                         sffm.ModulationIndex * std::sin(2.0 * std::numbers::pi * sffm.SignalFrequency * time);
    return sffm.Offset + sffm.Amplitude * std::sin(phase);
}

double EvaluateAM(const AMParameters &am, const double time) {
    if (time < am.Delay) {
        return 0.0;
    }
    const double local = time - am.Delay;
    return am.Offset + am.Amplitude * std::sin(2.0 * std::numbers::pi * am.ModulatingFrequency * local) *
                           std::sin(2.0 * std::numbers::pi * am.CarrierFrequency * local);
}

double SourceDcValue(const Source &source) {
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        return source.GetValue();
    case Source::SourceType::AC:
        return source.GetAC().Offset;
    case Source::SourceType::PULSE:
        return source.GetPulse().Low;
    case Source::SourceType::EXP:
        return source.GetEXP().InitialValue;
    case Source::SourceType::SFFM:
        return source.GetSFFM().Offset;
    case Source::SourceType::AM:
        return source.GetAM().Offset;
    case Source::SourceType::PWL:
        return source.GetPWL().Points.front().Value;
    }
    return source.GetValue();
}

VoltageSourceDevice::Waveform MakeSourceWaveform(const Source &source) {
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        return [](const double dc, const TimeContext &) { return dc; };
    case Source::SourceType::AC: {
        const ACParameters ac = source.GetAC();
        return [ac](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluateSine(ac, time.Time) : dc;
        };
    }
    case Source::SourceType::PULSE: {
        const PulseParameters pulse = source.GetPulse();
        return [pulse](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluatePulse(pulse, time.Time) : dc;
        };
    }
    case Source::SourceType::EXP: {
        const EXPParameters exp = source.GetEXP();
        return [exp](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluateEXP(exp, time.Time) : dc;
        };
    }
    case Source::SourceType::SFFM: {
        const SFFMParameters sffm = source.GetSFFM();
        return [sffm](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluateSFFM(sffm, time.Time) : dc;
        };
    }
    case Source::SourceType::AM: {
        const AMParameters am = source.GetAM();
        return [am](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluateAM(am, time.Time) : dc;
        };
    }
    case Source::SourceType::PWL: {
        const PWLParameters pwl = source.GetPWL();
        return [pwl](const double dc, const TimeContext &time) {
            return time.IsTransient ? EvaluatePWL(pwl, time.Time) : dc;
        };
    }
    }
    return [](const double dc, const TimeContext &) { return dc; };
}

VoltageSourceDevice::Waveform MakeClockWaveform(const ClockParameters &parameters) {
    const double period = 1.0 / parameters.Frequency;
    const double edge = CLOCK_EDGE_FRACTION * period;
    PulseParameters pulse;
    pulse.Low = 0.0;
    pulse.High = parameters.LogicLevel;
    pulse.Delay = parameters.StartDelay;
    pulse.RiseTime = edge;
    pulse.FallTime = edge;
    pulse.Width = std::max(parameters.DutyCycle * period - edge, 0.0);
    pulse.Period = period;
    return [pulse](const double, const TimeContext &time) { return EvaluatePulse(pulse, time.Time); };
}

} // namespace Core::Native
