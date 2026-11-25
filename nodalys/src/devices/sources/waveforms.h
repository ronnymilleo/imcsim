/**
 * @file    waveforms.h
 * @brief   Factory functions creating time-domain source waveform generators.
 */

#ifndef NODALYS_WAVEFORMS_H
#define NODALYS_WAVEFORMS_H

#include "mna/mna_system.h"
#include "nodalys/types.h"
#include <cstdint>
#include <functional>

namespace Nodalys {

/// Next corner of a source waveform strictly after an instant, or infinity; see Device::NextBreakpoint().
using BreakpointCallback = std::function<double(double time)>;
/// Resets the random stream seed of a stateful transient noise or random waveform.
using SeedCallback = std::function<void(std::uint64_t)>;
/// Value of an independent source at the stamped point, given its DC level and the time context.
using WaveformCallback = std::function<double(double dc_value, const TimeContext &time)>;

/**
 * @struct  SourceCallbacks
 * @brief   Fresh waveform, breakpoint, and seed callbacks created for an isolated clone of a stateful source.
 */
struct SourceCallbacks {
    WaveformCallback Evaluate;
    BreakpointCallback NextBreakpoint;
    SeedCallback SetSeed;
};

/// Creates an independent SourceCallbacks bundle when cloning a source with mutable waveform state (e.g. TRNOISE).
using SourceCallbackFactory = std::function<SourceCallbacks()>;

// Corners the transient lands on
double FindNextPulseCorner(const PulseParameters &pulse, double time);
BreakpointCallback MakePulseBreakpoints(const PulseParameters &pulse);
BreakpointCallback MakeExpBreakpoints(const ExpParameters &exp);
BreakpointCallback MakePwlBreakpoints(const PwlParameters &pwl);
BreakpointCallback MakeAmBreakpoints(const AmParameters &am);
BreakpointCallback MakeClockBreakpoints(const ClockParameters &parameters);

// Direct waveform evaluation
double EvaluatePulse(const PulseParameters &pulse, double time);
double EvaluateExp(const ExpParameters &exp, double time);
double EvaluateSine(const SineParameters &sine, double time);
double EvaluatePwl(const PwlParameters &pwl, double time);
double EvaluateSffm(const SffmParameters &sffm, double time);
double EvaluateAm(const AmParameters &am, double time);

// Waveform callback factories
WaveformCallback MakeDcWaveform(double dc_value);
WaveformCallback MakePulseWaveform(double dc_value, const PulseParameters &pulse);
WaveformCallback MakeSineWaveform(double dc_value, const SineParameters &sine);
WaveformCallback MakeExpWaveform(double dc_value, const ExpParameters &exp);
WaveformCallback MakePwlWaveform(double dc_value, const PwlParameters &pwl);
WaveformCallback MakeSffmWaveform(double dc_value, const SffmParameters &sffm);
WaveformCallback MakeAmWaveform(double dc_value, const AmParameters &am);
WaveformCallback MakeClockWaveform(const ClockParameters &parameters);

} // namespace Nodalys

#endif // NODALYS_WAVEFORMS_H
