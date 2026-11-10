/**
 * @file    waveforms.cpp
 * @brief   Factory functions creating time-domain source waveform generators.
 */

#include "devices/waveforms.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nodalys {

namespace {

constexpr double CLOCK_EDGE_FRACTION = 1e-4;

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

double EvaluateExp(const ExpParameters &exp, const double time) {
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

double EvaluateSine(const SineParameters &sine, const double time) {
    const double omega = 2.0 * std::numbers::pi * sine.Frequency;
    const double phase_rad = sine.Phase * (std::numbers::pi / 180.0);
    return sine.Offset + sine.Amplitude * std::sin(omega * time + phase_rad);
}

double EvaluatePwl(const PwlParameters &pwl, const double time) {
    const std::vector<PwlPoint> &points = pwl.Points;
    if (points.empty()) {
        return 0.0;
    }
    if (time <= points.front().Time) {
        return points.front().Value;
    }
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (time <= points[index].Time) {
            const PwlPoint &before = points[index - 1];
            const PwlPoint &after = points[index];
            const double share = (time - before.Time) / (after.Time - before.Time);
            return before.Value + share * (after.Value - before.Value);
        }
    }
    return points.back().Value;
}

double EvaluateSffm(const SffmParameters &sffm, const double time) {
    const double phase = 2.0 * std::numbers::pi * sffm.CarrierFrequency * time +
                         sffm.ModulationIndex * std::sin(2.0 * std::numbers::pi * sffm.SignalFrequency * time);
    return sffm.Offset + sffm.Amplitude * std::sin(phase);
}

double EvaluateAm(const AmParameters &am, const double time) {
    if (time < am.Delay) {
        return 0.0;
    }
    const double local = time - am.Delay;
    return am.Offset + am.Amplitude * std::sin(2.0 * std::numbers::pi * am.ModulatingFrequency * local) *
                           std::sin(2.0 * std::numbers::pi * am.CarrierFrequency * local);
}

VoltageSourceDevice::Waveform MakeDcWaveform(const double) {
    return [](const double dc, const TimeContext &) { return dc; };
}

VoltageSourceDevice::Waveform MakePulseWaveform(const double, const PulseParameters &pulse) {
    return [pulse](const double dc, const TimeContext &time) {
        return time.IsTransient ? EvaluatePulse(pulse, time.Time) : dc;
    };
}

VoltageSourceDevice::Waveform MakeSineWaveform(const double, const SineParameters &sine) {
    return [sine](const double dc, const TimeContext &time) {
        return time.IsTransient ? EvaluateSine(sine, time.Time) : dc;
    };
}

VoltageSourceDevice::Waveform MakeExpWaveform(const double, const ExpParameters &exp) {
    return
        [exp](const double dc, const TimeContext &time) { return time.IsTransient ? EvaluateExp(exp, time.Time) : dc; };
}

VoltageSourceDevice::Waveform MakePwlWaveform(const double, const PwlParameters &pwl) {
    return
        [pwl](const double dc, const TimeContext &time) { return time.IsTransient ? EvaluatePwl(pwl, time.Time) : dc; };
}

VoltageSourceDevice::Waveform MakeSffmWaveform(const double, const SffmParameters &sffm) {
    return [sffm](const double dc, const TimeContext &time) {
        return time.IsTransient ? EvaluateSffm(sffm, time.Time) : dc;
    };
}

VoltageSourceDevice::Waveform MakeAmWaveform(const double, const AmParameters &am) {
    return [am](const double dc, const TimeContext &time) { return time.IsTransient ? EvaluateAm(am, time.Time) : dc; };
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

} // namespace nodalys
