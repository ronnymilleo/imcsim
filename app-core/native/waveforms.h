/**
 * @file    waveforms.h
 * @brief   Evaluates independent source waveforms at DC and transient time points, matching SPICE definitions.
 */

#ifndef IMCSIM_NATIVE_WAVEFORMS_H
#define IMCSIM_NATIVE_WAVEFORMS_H

#include "components/clock.h"
#include "components/source.h"
#include "native/devices.h"

namespace Core::Native {

/**
 * @brief   Returns the initial DC value of a source component (used at the operating point and as the starting
 *          value before a DC sweep overrides it), matching `FormatSource()` in `circuit.cpp`.
 */
double SourceDcValue(const Source &source);

/**
 * @brief   Builds the waveform function of an independent voltage or current source.
 * @param[in] source  Source component with its waveform parameters.
 * @return  A function returning `dc_value` during DC analyses (`!time.IsTransient`) and the time-domain waveform
 *          value during transient analyses (`time.IsTransient`).
 */
VoltageSourceDevice::Waveform MakeSourceWaveform(const Source &source);

/**
 * @brief   Builds the waveform function of a digital clock source (a periodic pulse between $0\text{ V}$ and
 *          `LogicLevel`).
 */
VoltageSourceDevice::Waveform MakeClockWaveform(const ClockParameters &parameters);

double EvaluatePulse(const PulseParameters &pulse, double time);
double EvaluateEXP(const EXPParameters &exp, double time);
double EvaluateSine(const ACParameters &ac, double time);
double EvaluatePWL(const PWLParameters &pwl, double time);
double EvaluateSFFM(const SFFMParameters &sffm, double time);
double EvaluateAM(const AMParameters &am, double time);

} // namespace Core::Native

#endif // IMCSIM_NATIVE_WAVEFORMS_H
