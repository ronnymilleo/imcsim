/**
 * @file    transient_noise.h
 * @brief   Seedable transient noise (TRNOISE) and random (TRRANDOM) source waveform generators for Nodalys.
 */

#ifndef NODALYS_TRANSIENT_NOISE_H
#define NODALYS_TRANSIENT_NOISE_H

#include "devices/waveforms.h"
#include "nodalys/types.h"
#include <cstddef>
#include <cstdint>

namespace Nodalys {

/**
 * @struct  TransientNoiseSource
 * @brief   Waveform, breakpoint, and seed-reset callbacks for a transient noise or random source.
 */
struct TransientNoiseSource {
    WaveformCallback Evaluate;
    BreakpointCallback NextBreakpoint;
    SeedCallback ResetSeed;
};

// Mixing
std::uint64_t MixTransientSeed(std::uint64_t seed, std::size_t index);

// Factories
TransientNoiseSource MakeNoiseSource(const NoiseParameters &params);
TransientNoiseSource MakeRandomSource(const RandomParameters &params);

} // namespace Nodalys

#endif // NODALYS_TRANSIENT_NOISE_H
