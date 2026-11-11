/**
 * @file    waveforms.h
 * @brief   Factory functions creating time-domain source waveform generators.
 */

#ifndef NODALYS_WAVEFORMS_H
#define NODALYS_WAVEFORMS_H

#include "devices/devices.h"
#include "nodalys/types.h"

namespace nodalys {

VoltageSourceDevice::Waveform MakeDcWaveform(double dc_value);
VoltageSourceDevice::Waveform MakePulseWaveform(double dc_value, const PulseParameters &pulse);
VoltageSourceDevice::Waveform MakeSineWaveform(double dc_value, const SineParameters &sine);
VoltageSourceDevice::Waveform MakeExpWaveform(double dc_value, const ExpParameters &exp);
VoltageSourceDevice::Waveform MakePwlWaveform(double dc_value, const PwlParameters &pwl);
VoltageSourceDevice::Waveform MakeSffmWaveform(double dc_value, const SffmParameters &sffm);
VoltageSourceDevice::Waveform MakeAmWaveform(double dc_value, const AmParameters &am);
VoltageSourceDevice::Waveform MakeClockWaveform(const ClockParameters &parameters);

// Corners the transient lands on
VoltageSourceDevice::Breakpoints MakePulseBreakpoints(const PulseParameters &pulse);
VoltageSourceDevice::Breakpoints MakeExpBreakpoints(const ExpParameters &exp);
VoltageSourceDevice::Breakpoints MakePwlBreakpoints(const PwlParameters &pwl);
VoltageSourceDevice::Breakpoints MakeAmBreakpoints(const AmParameters &am);
VoltageSourceDevice::Breakpoints MakeClockBreakpoints(const ClockParameters &parameters);
double FindNextPulseCorner(const PulseParameters &pulse, double time);

double EvaluatePulse(const PulseParameters &pulse, double time);
double EvaluateExp(const ExpParameters &exp, double time);
double EvaluateSine(const SineParameters &sine, double time);
double EvaluatePwl(const PwlParameters &pwl, double time);
double EvaluateSffm(const SffmParameters &sffm, double time);
double EvaluateAm(const AmParameters &am, double time);

} // namespace nodalys

#endif // NODALYS_WAVEFORMS_H
