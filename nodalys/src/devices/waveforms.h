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

double EvaluatePulse(const PulseParameters &pulse, double time);
double EvaluateExp(const ExpParameters &exp, double time);
double EvaluateSine(const SineParameters &sine, double time);
double EvaluatePwl(const PwlParameters &pwl, double time);
double EvaluateSffm(const SffmParameters &sffm, double time);
double EvaluateAm(const AmParameters &am, double time);

} // namespace nodalys

#endif // NODALYS_WAVEFORMS_H
