/**
 * @file    transient_noise.cpp
 * @brief   Seedable transient noise (TRNOISE) and random (TRRANDOM) source waveform generators for Nodalys.
 */

#include "devices/transient_noise.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace Nodalys {

namespace {

constexpr double NO_CORNER = std::numeric_limits<double>::infinity();
constexpr std::size_t FLICKER_TAPS = 1024;

bool IsAfter(const double corner, const double time) {
    return corner > time + 1e-12 * std::max(std::fabs(time), std::fabs(corner)) + 1e-21;
}

class NoiseState {
public:
    explicit NoiseState(const NoiseParameters &params) : m_Params(params) { Reset(1); }

    void Reset(const std::uint64_t seed) {
        m_Rng.seed(seed);
        m_Samples.clear();
        m_Samples.push_back(0.0);
        m_FlickerWhite.clear();
        m_FlickerWeights.clear();
        if (m_Params.FlickerAmplitude != 0.0 && m_Params.TimeStep > 0.0) {
            m_FlickerWeights.resize(FLICKER_TAPS, 0.0);
            m_FlickerWeights[0] = 1.0;
            const double half_alpha = 0.5 * m_Params.FlickerExponent;
            for (std::size_t k = 1; k < FLICKER_TAPS; ++k) {
                const double kd = static_cast<double>(k);
                m_FlickerWeights[k] = m_FlickerWeights[k - 1] * ((kd - 1.0 + half_alpha) / kd);
            }
            m_FlickerWhite.push_back(0.0);
        }
        m_TelegraphTimes.clear();
        m_TelegraphValues.clear();
        m_TelegraphTimes.push_back(0.0);
        m_TelegraphValues.push_back(0.0);
    }

    double Evaluate(const double time) {
        double value = 0.0;
        if (time <= 0.0) {
            return value;
        }
        if (HasSteppedNoise()) {
            const double scaled = time / m_Params.TimeStep;
            const auto index = static_cast<std::size_t>(std::floor(scaled));
            const double frac = scaled - static_cast<double>(index);
            EnsureNoiseSample(index + 1);
            value += m_Samples[index] + frac * (m_Samples[index + 1] - m_Samples[index]);
        }
        if (HasTelegraph()) {
            value += EvaluateTelegraph(time);
        }
        return value;
    }

    double NextBreakpoint(const double time) {
        double next = NO_CORNER;
        if (HasSteppedNoise()) {
            const double k = std::floor(time / m_Params.TimeStep) + 1.0;
            const double step_corner = k * m_Params.TimeStep;
            next = IsAfter(step_corner, time) ? step_corner : (k + 1.0) * m_Params.TimeStep;
        }
        if (HasTelegraph()) {
            EnsureTelegraphPast(time);
            for (const double corner : m_TelegraphTimes) {
                if (IsAfter(corner, time)) {
                    next = std::min(next, corner);
                    break;
                }
            }
        }
        return next;
    }

private:
    bool HasSteppedNoise() const {
        return m_Params.TimeStep > 0.0 && (m_Params.WhiteAmplitude != 0.0 || m_Params.FlickerAmplitude != 0.0);
    }

    bool HasTelegraph() const {
        return m_Params.TelegraphAmplitude != 0.0 && m_Params.CaptureTime > 0.0 && m_Params.EmissionTime > 0.0;
    }

    void EnsureNoiseSample(const std::size_t target) {
        std::normal_distribution<double> normal(0.0, 1.0);
        while (m_Samples.size() <= target) {
            double sample = 0.0;
            if (m_Params.WhiteAmplitude != 0.0) {
                sample += m_Params.WhiteAmplitude * normal(m_Rng);
            }
            if (m_Params.FlickerAmplitude != 0.0) {
                m_FlickerWhite.push_back(m_Params.FlickerAmplitude * normal(m_Rng));
                const std::size_t n = m_FlickerWhite.size() - 1;
                const std::size_t count = std::min(n, FLICKER_TAPS);
                double flicker = 0.0;
                for (std::size_t k = 0; k < count; ++k) {
                    flicker += m_FlickerWeights[k] * m_FlickerWhite[n - k];
                }
                sample += flicker;
            }
            m_Samples.push_back(sample);
        }
    }

    void EnsureTelegraphPast(const double time) {
        while (m_TelegraphTimes.back() <= time) {
            const bool currently_high = m_TelegraphValues.back() != 0.0;
            const double mean_duration = currently_high ? m_Params.CaptureTime : m_Params.EmissionTime;
            std::exponential_distribution<double> exp_dist(1.0 / mean_duration);
            double dt = exp_dist(m_Rng);
            dt = std::max(dt, 1e-18);
            m_TelegraphTimes.push_back(m_TelegraphTimes.back() + dt);
            m_TelegraphValues.push_back(currently_high ? 0.0 : m_Params.TelegraphAmplitude);
        }
    }

    double EvaluateTelegraph(const double time) {
        EnsureTelegraphPast(time);
        const auto it = std::upper_bound(m_TelegraphTimes.begin(), m_TelegraphTimes.end(), time);
        const auto idx = static_cast<std::size_t>(std::distance(m_TelegraphTimes.begin(), it)) - 1;
        return m_TelegraphValues[idx];
    }

    NoiseParameters m_Params;
    std::mt19937_64 m_Rng;
    std::vector<double> m_Samples;
    std::vector<double> m_FlickerWhite;
    std::vector<double> m_FlickerWeights;
    std::vector<double> m_TelegraphTimes;
    std::vector<double> m_TelegraphValues;
};

class RandomState {
public:
    explicit RandomState(const RandomParameters &params) : m_Params(params) { Reset(1); }

    void Reset(const std::uint64_t seed) {
        m_Rng.seed(seed);
        m_Samples.clear();
    }

    double Evaluate(const double time) {
        if (time <= 0.0 || time < m_Params.Delay || m_Params.TimeStep <= 0.0) {
            return 0.0;
        }
        const auto index = static_cast<std::size_t>(std::floor((time - m_Params.Delay) / m_Params.TimeStep));
        EnsureSample(index);
        return m_Samples[index];
    }

    double NextBreakpoint(const double time) const {
        if (m_Params.TimeStep <= 0.0) {
            return NO_CORNER;
        }
        if (time < m_Params.Delay && IsAfter(m_Params.Delay, time)) {
            return m_Params.Delay;
        }
        const double elapsed = std::max(0.0, time - m_Params.Delay);
        const double k = std::floor(elapsed / m_Params.TimeStep) + 1.0;
        const double corner = m_Params.Delay + k * m_Params.TimeStep;
        return IsAfter(corner, time) ? corner : m_Params.Delay + (k + 1.0) * m_Params.TimeStep;
    }

private:
    void EnsureSample(const std::size_t target) {
        while (m_Samples.size() <= target) {
            m_Samples.push_back(DrawValue());
        }
    }

    double DrawValue() {
        switch (m_Params.Distribution) {
        case RandomDistribution::UNIFORM: {
            const double span = std::fabs(m_Params.First);
            return std::uniform_real_distribution<double>(-span, span)(m_Rng);
        }
        case RandomDistribution::GAUSSIAN:
            return std::normal_distribution<double>(0.0, std::fabs(m_Params.First))(m_Rng);
        case RandomDistribution::EXPONENTIAL: {
            const double mean = m_Params.First > 0.0 ? m_Params.First : 1.0;
            return std::exponential_distribution<double>(1.0 / mean)(m_Rng);
        }
        case RandomDistribution::POISSON: {
            const double lambda = m_Params.First > 0.0 ? m_Params.First : 1.0;
            return static_cast<double>(std::poisson_distribution<int>(lambda)(m_Rng));
        }
        default:
            return 0.0;
        }
    }

    RandomParameters m_Params;
    std::mt19937_64 m_Rng;
    std::vector<double> m_Samples;
};

} // namespace

/**
 * @brief   Mixes a run seed and a source index into an independent 64-bit stream seed via SplitMix64.
 * @param[in] seed   Run seed.
 * @param[in] index  Index of the device in the circuit.
 * @return  Mixed 64-bit seed.
 */
std::uint64_t MixTransientSeed(const std::uint64_t seed, const std::size_t index) {
    std::uint64_t z = seed + (static_cast<std::uint64_t>(index) + 1ULL) * 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/**
 * @brief   Creates the waveform, breakpoint, and seed callbacks for a TRNOISE source.
 * @param[in] params  White, flicker, and telegraph noise parameters.
 * @return  Callbacks sharing the same seeded generator state.
 */
TransientNoiseSource MakeNoiseSource(const NoiseParameters &params) {
    auto state = std::make_shared<NoiseState>(params);
    return {.Evaluate =
                [state](const double dc_value, const TimeContext &time) {
                    return time.IsTransient ? dc_value + state->Evaluate(time.Time) : dc_value;
                },
            .NextBreakpoint = [state](const double time) { return state->NextBreakpoint(time); },
            .ResetSeed = [state](const std::uint64_t seed) { state->Reset(seed); }};
}

/**
 * @brief   Creates the waveform, breakpoint, and seed callbacks for a TRRANDOM source.
 * @param[in] params  Distribution, interval, delay, and parameter pair.
 * @return  Callbacks sharing the same seeded generator state.
 */
TransientNoiseSource MakeRandomSource(const RandomParameters &params) {
    auto state = std::make_shared<RandomState>(params);
    return {.Evaluate =
                [state](const double dc_value, const TimeContext &time) {
                    return time.IsTransient ? dc_value + state->Evaluate(time.Time) : dc_value;
                },
            .NextBreakpoint = [state](const double time) { return state->NextBreakpoint(time); },
            .ResetSeed = [state](const std::uint64_t seed) { state->Reset(seed); }};
}

} // namespace Nodalys
