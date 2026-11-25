/**
 * @file    source_specs.cpp
 * @brief   Independent source waveform specifications built from public parameter structs.
 */

#include "compiler/source_specs.h"

#include "devices/sources/transient_noise.h"
#include <algorithm>
#include <utility>

namespace Nodalys {

namespace {

/// Fraction of one clock period used as the rise/fall edge time estimate for harmonic balance.
constexpr double CLOCK_EDGE_FRACTION = 1e-4;
/// The periodicity of the sources the harmonic balance cannot take: they have no period it could use.
constexpr SourcePeriodicity NON_PERIODIC{.Kind = SourceWaveformKind::NON_PERIODIC, .IsSupportedInHb = false};

} // namespace

/**
 * @brief   Describes an amplitude-modulated source.
 * @param[in] params        AM waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeAmSpec(const AmParameters &params, const double ac_magnitude) {
    return {.Dc = params.Offset,
            .Waveform = MakeAmWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakeAmBreakpoints(params)};
}

/**
 * @brief   Describes a periodic square-wave clock voltage source.
 * @param[in] params  Clock waveform parameters.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeClockSpec(const ClockParameters &params) {
    const double period = params.Frequency > 0.0 ? 1.0 / params.Frequency : 0.0;
    return {.Dc = 0.0,
            .Waveform = MakeClockWaveform(params),
            .AcMagnitude = 0.0,
            .Periodicity = {.Kind = SourceWaveformKind::CLOCK,
                            .Frequency = params.Frequency,
                            .MinEdgeTime = CLOCK_EDGE_FRACTION * period,
                            .IsSupportedInHb = params.Frequency > 0.0},
            .Breakpoints = MakeClockBreakpoints(params)};
}

/**
 * @brief   Describes a source that holds a constant level.
 * @param[in] dc            Constant DC level.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeDcSpec(const double dc, const double ac_magnitude) {
    return {.Dc = dc, .Waveform = MakeDcWaveform(dc), .AcMagnitude = ac_magnitude};
}

/**
 * @brief   Describes an exponential source.
 * @param[in] params        Exponential waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeExpSpec(const ExpParameters &params, const double ac_magnitude) {
    return {.Dc = params.InitialValue,
            .Waveform = MakeExpWaveform(params.InitialValue, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakeExpBreakpoints(params)};
}

/**
 * @brief   Describes a noise source, which redraws its samples when the seed is reset.
 * @param[in] params        Transient noise waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec with cloneable noise callback state.
 */
SourceSpec MakeNoiseSpec(const NoiseParameters &params, const double ac_magnitude) {
    TransientNoiseSource noise = MakeNoiseSource(params);
    return {.Dc = params.Offset,
            .Waveform = std::move(noise.Evaluate),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = std::move(noise.NextBreakpoint),
            .SetSeed = std::move(noise.ResetSeed),
            .CallbackFactory = [params]() {
                TransientNoiseSource fresh = MakeNoiseSource(params);
                return SourceCallbacks{.Evaluate = std::move(fresh.Evaluate),
                                       .NextBreakpoint = std::move(fresh.NextBreakpoint),
                                       .SetSeed = std::move(fresh.ResetSeed)};
            }};
}

/**
 * @brief   Describes a pulse source, which is periodic when the pulse has a period.
 * @param[in] params        Pulse waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakePulseSpec(const PulseParameters &params, const double ac_magnitude) {
    return {.Dc = params.Low,
            .Waveform = MakePulseWaveform(params.Low, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = {.Kind = SourceWaveformKind::PULSE,
                            .Frequency = params.Period > 0.0 ? 1.0 / params.Period : 0.0,
                            .MinEdgeTime = std::min(params.RiseTime, params.FallTime),
                            .IsSupportedInHb = params.High == params.Low || params.Period > 0.0},
            .Breakpoints = MakePulseBreakpoints(params)};
}

/**
 * @brief   Describes a piecewise linear source, which starts from the value of its first point.
 * @param[in] params        Piecewise-linear waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakePwlSpec(const PwlParameters &params, const double ac_magnitude) {
    const double dc = params.Points.empty() ? 0.0 : params.Points.front().Value;
    return {.Dc = dc,
            .Waveform = MakePwlWaveform(dc, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = MakePwlBreakpoints(params)};
}

/**
 * @brief   Describes a source of random levels, which redraws them when the seed is reset.
 * @param[in] params        Random-sample waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec with cloneable RNG callback state.
 */
SourceSpec MakeRandomSpec(const RandomParameters &params, const double ac_magnitude) {
    TransientNoiseSource random = MakeRandomSource(params);
    return {.Dc = params.Second,
            .Waveform = std::move(random.Evaluate),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC,
            .Breakpoints = std::move(random.NextBreakpoint),
            .SetSeed = std::move(random.ResetSeed),
            .CallbackFactory = [params]() {
                TransientNoiseSource fresh = MakeRandomSource(params);
                return SourceCallbacks{.Evaluate = std::move(fresh.Evaluate),
                                       .NextBreakpoint = std::move(fresh.NextBreakpoint),
                                       .SetSeed = std::move(fresh.ResetSeed)};
            }};
}

/**
 * @brief   Describes a single-frequency FM source.
 * @param[in] params        SFFM waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeSffmSpec(const SffmParameters &params, const double ac_magnitude) {
    return {.Dc = params.Offset,
            .Waveform = MakeSffmWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = NON_PERIODIC};
}

/**
 * @brief   Describes a sine source; a damped one is not periodic, so harmonic balance cannot take it.
 * @param[in] params        Sine waveform parameters.
 * @param[in] ac_magnitude  AC small-signal excitation magnitude.
 * @param[in] is_damped     True when exponential damping is active.
 * @return  Configured SourceSpec.
 */
SourceSpec MakeSineSpec(const SineParameters &params, const double ac_magnitude, const bool is_damped) {
    return {.Dc = params.Offset,
            .Waveform = MakeSineWaveform(params.Offset, params),
            .AcMagnitude = ac_magnitude,
            .Periodicity = {.Kind = SourceWaveformKind::SINE,
                            .Frequency = params.Frequency,
                            .MinEdgeTime = 0.0,
                            .IsSupportedInHb = !is_damped && (params.Amplitude == 0.0 || params.Frequency > 0.0)}};
}

} // namespace Nodalys
