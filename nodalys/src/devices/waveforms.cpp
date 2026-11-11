/**
 * @file    waveforms.cpp
 * @brief   Factory functions creating time-domain source waveform generators.
 */

#include "devices/waveforms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace nodalys {

namespace {

constexpr double CLOCK_EDGE_FRACTION = 1e-4;
constexpr double NO_CORNER = std::numeric_limits<double>::infinity();

/**
 * @brief   Whether a corner lies after an instant by more than rounding, so the transient never lands twice on one.
 */
bool IsAfter(const double corner, const double time) {
    return corner > time + 1e-12 * std::max(std::fabs(time), std::fabs(corner)) + 1e-21;
}

/**
 * @brief   The pulse a clock drives, with edges a fixed share of its period.
 */
PulseParameters ClockPulse(const ClockParameters &parameters) {
    const double period = 1.0 / parameters.Frequency;
    const double edge = CLOCK_EDGE_FRACTION * period;
    return {.Low = 0.0,
            .High = parameters.LogicLevel,
            .Delay = parameters.StartDelay,
            .RiseTime = edge,
            .FallTime = edge,
            .Width = std::max(parameters.DutyCycle * period - edge, 0.0),
            .Period = period};
}

double LinearEdge(const double from, const double to, const double duration, const double progress) {
    if (duration <= 0.0) {
        return to;
    }
    return from + (to - from) * std::min(progress / duration, 1.0);
}

} // namespace

/**
 * @brief   Finds the next corner of a pulse: the start and end of each edge and of each period.
 * @param[in] pulse  Pulse to follow; a period of 0 or less makes it a single pulse.
 * @param[in] time   Instant already reached.
 * @return  The first corner strictly after `time`, or infinity once a single pulse has ended.
 */
double FindNextPulseCorner(const PulseParameters &pulse, const double time) {
    const std::array<double, 4> offsets = {0.0, pulse.RiseTime, pulse.RiseTime + pulse.Width,
                                           pulse.RiseTime + pulse.Width + pulse.FallTime};
    const bool is_periodic = pulse.Period > 0.0 && std::isfinite(pulse.Period);
    double cycle_start = pulse.Delay;
    if (is_periodic && time > pulse.Delay) {
        cycle_start += std::floor((time - pulse.Delay) / pulse.Period) * pulse.Period;
    }
    // The corners of this cycle, then those of the next one, cover every instant
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (const double offset : offsets) {
            if (const double corner = cycle_start + offset; IsAfter(corner, time)) {
                return corner;
            }
        }
        if (!is_periodic) {
            return NO_CORNER;
        }
        cycle_start += pulse.Period;
    }
    return NO_CORNER;
}

VoltageSourceDevice::Breakpoints MakePulseBreakpoints(const PulseParameters &pulse) {
    return [pulse](const double time) { return FindNextPulseCorner(pulse, time); };
}

VoltageSourceDevice::Breakpoints MakeExpBreakpoints(const ExpParameters &exp) {
    return [exp](const double time) {
        for (const double corner : {exp.RiseDelay, exp.FallDelay}) {
            if (IsAfter(corner, time)) {
                return corner;
            }
        }
        return NO_CORNER;
    };
}

VoltageSourceDevice::Breakpoints MakePwlBreakpoints(const PwlParameters &pwl) {
    return [pwl](const double time) {
        for (const PwlPoint &point : pwl.Points) {
            if (IsAfter(point.Time, time)) {
                return point.Time;
            }
        }
        return NO_CORNER;
    };
}

VoltageSourceDevice::Breakpoints MakeAmBreakpoints(const AmParameters &am) {
    return [am](const double time) { return IsAfter(am.Delay, time) ? am.Delay : NO_CORNER; };
}

VoltageSourceDevice::Breakpoints MakeClockBreakpoints(const ClockParameters &parameters) {
    return MakePulseBreakpoints(ClockPulse(parameters));
}

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
        if (time.IsTransient) {
            return EvaluatePulse(pulse, time.Time);
        }
        if (time.IsPeriodicSteadyState) {
            // Wrap startup delay modulo Period so Harmonic Balance evaluates the periodic steady-state extension.
            const double shift =
                (pulse.Period > 0.0 && pulse.Delay > 0.0) ? std::ceil(pulse.Delay / pulse.Period) * pulse.Period : 0.0;
            return EvaluatePulse(pulse, time.Time + shift);
        }
        return dc;
    };
}

VoltageSourceDevice::Waveform MakeSineWaveform(const double, const SineParameters &sine) {
    return [sine](const double dc, const TimeContext &time) {
        return (time.IsTransient || time.IsPeriodicSteadyState) ? EvaluateSine(sine, time.Time) : dc;
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
    const PulseParameters pulse = ClockPulse(parameters);
    return [pulse](const double, const TimeContext &time) {
        if (time.IsPeriodicSteadyState && !time.IsTransient) {
            const double shift =
                (pulse.Period > 0.0 && pulse.Delay > 0.0) ? std::ceil(pulse.Delay / pulse.Period) * pulse.Period : 0.0;
            return EvaluatePulse(pulse, time.Time + shift);
        }
        return EvaluatePulse(pulse, time.Time);
    };
}

} // namespace nodalys
