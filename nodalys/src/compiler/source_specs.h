/**
 * @file    source_specs.h
 * @brief   Independent source waveform specifications built from public parameter structs.
 */

#ifndef NODALYS_COMPILER_SOURCE_SPECS_H
#define NODALYS_COMPILER_SOURCE_SPECS_H

#include "devices/independent_sources.h"
#include "devices/waveforms.h"
#include "nodalys/types.h"

namespace Nodalys {

/**
 * @struct  SourceSpec
 * @brief   What an independent source is made of, whether it is a voltage source or a current source.
 */
struct SourceSpec {
    double Dc = 0.0;
    WaveformCallback Waveform;
    double AcMagnitude = 0.0;
    SourcePeriodicity Periodicity;
    BreakpointCallback Breakpoints;
    SeedCallback SetSeed;
    SourceCallbackFactory CallbackFactory;
};

// Independent source specification factories
[[nodiscard]] SourceSpec MakeAmSpec(const AmParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakeClockSpec(const ClockParameters &params);
[[nodiscard]] SourceSpec MakeDcSpec(double dc, double ac_magnitude);
[[nodiscard]] SourceSpec MakeExpSpec(const ExpParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakeNoiseSpec(const NoiseParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakePulseSpec(const PulseParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakePwlSpec(const PwlParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakeRandomSpec(const RandomParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakeSffmSpec(const SffmParameters &params, double ac_magnitude);
[[nodiscard]] SourceSpec MakeSineSpec(const SineParameters &params, double ac_magnitude, bool is_damped);

} // namespace Nodalys

#endif // NODALYS_COMPILER_SOURCE_SPECS_H
