/**
 * @file    check_harmonic_balance.cpp
 * @brief   Checks the settings and the frequency grid of a harmonic balance before it runs.
 */

#include "simulation/check_harmonic_balance.h"

#include "circuit/circuit.h"
#include "circuit/components/clock.h"
#include "circuit/components/logic_part.h"
#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "simulation/analysis_settings.h"
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

namespace {

bool IsPowerOfTwo(const int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::optional<std::string> CheckSourceHarmonicRatio(const std::string_view name, const double source_frequency,
                                                    const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(source_frequency) || source_frequency <= 0.0) {
        return std::format("The frequency of {} must be positive for harmonic balance", name);
    }
    const std::optional<int> harmonic = FindHarmonicNumber(source_frequency, settings.FundamentalFrequency);
    if (!harmonic) {
        return std::format("{} runs at {} Hz, which is not an integer multiple of the fundamental {} Hz", name,
                           source_frequency, settings.FundamentalFrequency);
    }
    if (*harmonic > settings.Harmonics) {
        return std::format("{} runs at {} Hz (harmonic {}), which exceeds the {} configured harmonics", name,
                           source_frequency, *harmonic, settings.Harmonics);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks the fundamental frequency, harmonic count, sample grid, and periodic sources for harmonic balance.
 * @param[in] circuit   Circuit to simulate, including any sources inside blocks.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason harmonic balance cannot run on those settings, or no value when every check passes.
 */
std::optional<std::string> CheckHarmonicBalanceSettings(const Circuit &circuit,
                                                        const HarmonicBalanceSettings &settings) {
    if (std::optional<std::string> error = CheckHarmonicBalanceGrid(settings)) {
        return error;
    }
    for (const Component *part : CollectParts(circuit)) {
        if (part->GetType() == ComponentType::CLOCK) {
            const double frequency = static_cast<const Clock &>(*part).GetParameters().Frequency;
            if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                return error;
            }
            continue;
        }
        if (!IsSource(part->GetType())) {
            continue;
        }
        const auto &source = static_cast<const Source &>(*part);
        switch (source.GetSourceType()) {
        case Source::SourceType::DC:
            break;
        case Source::SourceType::AC: {
            const ACParameters &ac = source.GetAC();
            if (ac.Amplitude != 0.0) {
                if (std::optional<std::string> error =
                        CheckSourceHarmonicRatio(part->GetName(), ac.Frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::PULSE: {
            const PulseParameters &pulse = source.GetPulse();
            if (pulse.High != pulse.Low) {
                const double frequency = pulse.Period > 0.0 ? 1.0 / pulse.Period : 0.0;
                if (std::optional<std::string> error = CheckSourceHarmonicRatio(part->GetName(), frequency, settings)) {
                    return error;
                }
            }
            break;
        }
        case Source::SourceType::EXP:
        case Source::SourceType::SFFM:
        case Source::SourceType::AM:
        case Source::SourceType::PWL:
        case Source::SourceType::NOISE:
        case Source::SourceType::RANDOM:
        default:
            return std::format("{} uses a {} waveform, which harmonic balance does not support", part->GetName(),
                               GetSourceTypeName(source.GetSourceType()));
        }
    }
    return std::nullopt;
}

/**
 * @brief   Checks the frequency grid of a harmonic balance alone, without a circuit, as a file is read.
 * @param[in] settings  Fundamental frequency, number of harmonics, and optional power-of-two sample count.
 * @return  The reason the grid cannot be solved, or no value when it can.
 */
std::optional<std::string> CheckHarmonicBalanceGrid(const HarmonicBalanceSettings &settings) {
    if (!std::isfinite(settings.FundamentalFrequency) || settings.FundamentalFrequency <= 0.0) {
        return "The fundamental frequency must be positive";
    }
    if (settings.Harmonics < 1 || settings.Harmonics > MAX_HARMONIC_BALANCE_HARMONICS) {
        return std::format("The number of harmonics must be between 1 and {}", MAX_HARMONIC_BALANCE_HARMONICS);
    }
    if (settings.Samples != 0 && (!IsPowerOfTwo(settings.Samples) || settings.Samples <= 2 * settings.Harmonics ||
                                  settings.Samples > MAX_HARMONIC_BALANCE_SAMPLES)) {
        return std::format("The sample count must be 0 (automatic) or a power of two greater than {} (up to {})",
                           2 * settings.Harmonics, MAX_HARMONIC_BALANCE_SAMPLES);
    }
    return std::nullopt;
}

/**
 * @brief   Finds which harmonic of a fundamental a frequency is.
 * @param[in] frequency    Frequency of a periodic source, in hertz.
 * @param[in] fundamental  Fundamental frequency, in hertz, above 0.
 * @return  The harmonic number, from 1, or none when the frequency is no integer multiple of the fundamental within
 *          HARMONIC_RATIO_TOLERANCE.
 */
std::optional<int> FindHarmonicNumber(const double frequency, const double fundamental) {
    const double ratio = frequency / fundamental;
    const double nearest = std::round(ratio);
    if (!std::isfinite(ratio) || nearest < 1.0 || std::abs(ratio - nearest) > HARMONIC_RATIO_TOLERANCE * nearest) {
        return std::nullopt;
    }
    return static_cast<int>(nearest);
}

} // namespace Core
