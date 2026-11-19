/**
 * @file    hb_setup.cpp
 * @brief   Checks a circuit for harmonic balance and prepares its linear part and its excitation.
 */

#include "analyses/hb_setup.h"

#include "linear/fft.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

namespace Nodalys {

namespace {

constexpr std::size_t MIN_AUTO_SAMPLES = 16;
constexpr std::size_t MAX_DENSE_UNKNOWNS_WARNING = 1500;
/// Most unknowns the dense harmonic Jacobian may hold: 4000^2 doubles are 128 MB, and each Newton iteration factors it
/// in seconds; a larger system would take the memory and the time of a run nobody waits for.
constexpr std::size_t MAX_DENSE_UNKNOWNS = 4000;
constexpr double FREQ_RATIO_RELTOL = 1e-9;

/// An independent source seen through what the balance needs to know about it.
struct SourceView {
    /// The periodicity of the source, or null when the device is not an independent source.
    const SourcePeriodicity *Periodicity = nullptr;
    /// "voltage" or "current", for the messages.
    std::string_view KindLabel;
};

SourceView FindSource(const Device &device) {
    if (const auto *vs = dynamic_cast<const VoltageSourceDevice *>(&device)) {
        return {.Periodicity = &vs->GetPeriodicity(), .KindLabel = "voltage"};
    }
    if (const auto *cs = dynamic_cast<const CurrentSourceDevice *>(&device)) {
        return {.Periodicity = &cs->GetPeriodicity(), .KindLabel = "current"};
    }
    return {};
}

/**
 * @brief   Checks that a source repeats with a harmonic of the fundamental that the expansion keeps.
 */
std::expected<void, std::string> CheckSourcePeriodicity(const SourcePeriodicity &periodicity,
                                                        const std::string_view kind_label,
                                                        const HarmonicBalanceConfig &config) {
    if (!periodicity.IsSupportedInHb) {
        return std::unexpected(
            std::format("Harmonic Balance does not support non-periodic or damped {} sources", kind_label));
    }
    if (periodicity.Kind == SourceWaveformKind::DC) {
        return {};
    }
    if (periodicity.Frequency <= 0.0) {
        return {};
    }
    const double ratio = periodicity.Frequency / config.FundamentalFrequency;
    const double harmonic_nearest = std::round(ratio);
    if (harmonic_nearest < 1.0 || std::abs(ratio - harmonic_nearest) > FREQ_RATIO_RELTOL * harmonic_nearest) {
        return std::unexpected(std::format("Source frequency {} Hz is not an integer multiple of fundamental {} Hz "
                                           "for single-tone Harmonic Balance",
                                           periodicity.Frequency,
                                           config.FundamentalFrequency));
    }
    if (static_cast<int>(harmonic_nearest) > config.Harmonics) {
        return std::unexpected(std::format("Source frequency {} Hz (harmonic {}) exceeds configured Harmonics = {}",
                                           periodicity.Frequency,
                                           static_cast<int>(harmonic_nearest),
                                           config.Harmonics));
    }
    return {};
}

} // namespace

/**
 * @brief   Validates the sample count of the configuration, or picks one when it asks for the automatic count.
 * @param[in] config  Fundamental frequency, harmonic count and samples (zero for automatic).
 * @return  The number of samples of the period, a power of two, or the reason the configuration is refused.
 */
std::expected<std::size_t, std::string> ValidateAndResolveSampleCount(const HarmonicBalanceConfig &config) {
    if (!std::isfinite(config.FundamentalFrequency) || config.FundamentalFrequency <= 0.0) {
        return std::unexpected("Harmonic Balance requires a positive finite fundamental frequency");
    }
    if (config.Harmonics < 0) {
        return std::unexpected("Harmonic Balance requires a non-negative number of harmonics");
    }
    if (config.Samples == 0) {
        const std::size_t min_samples =
            std::max(MIN_AUTO_SAMPLES, static_cast<std::size_t>(4) * static_cast<std::size_t>(config.Harmonics));
        return NextPowerOfTwo(min_samples);
    }
    if (config.Samples < 0 || !IsPowerOfTwo(static_cast<std::size_t>(config.Samples))) {
        return std::unexpected("Harmonic Balance sample count must be a positive power of two");
    }
    const auto sample_count = static_cast<std::size_t>(config.Samples);
    if (sample_count <= static_cast<std::size_t>(2) * static_cast<std::size_t>(config.Harmonics)) {
        return std::unexpected(std::format(
            "Harmonic Balance sample count ({}) must exceed twice the harmonic count ({}) to prevent aliasing",
            sample_count,
            config.Harmonics));
    }
    return sample_count;
}

/**
 * @brief   Checks that every device can take part in a harmonic balance.
 * @param[in] compiled  The circuit, whose devices are looked at in order.
 * @param[in] config    Fundamental frequency and harmonic count that the sources must fit.
 * @return  Nothing, or the first reason a source or a device is refused.
 */
std::expected<void, std::string> ValidateDevices(const CompiledCircuit &compiled, const HarmonicBalanceConfig &config) {
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (const SourceView source = FindSource(*device); source.Periodicity != nullptr) {
            if (auto ok = CheckSourcePeriodicity(*source.Periodicity, source.KindLabel, config); !ok) {
                return std::unexpected(ok.error());
            }
        } else if (!device->SupportsHarmonicBalance()) {
            return std::unexpected("Harmonic Balance does not support hysteretic switches or relays, nor a "
                                   "behavioral source that reads time");
        }
    }
    return {};
}

/**
 * @brief   Refuses a system too large for the dense Jacobian, and warns about one that will be slow.
 * @param[in]  unknown_count  MNA unknowns of the circuit.
 * @param[in]  harmonics      Highest harmonic of the expansion.
 * @param[out] warnings       Receives the warning for a large system.
 * @return  Nothing, or the reason the system is refused.
 */
std::expected<void, std::string>
ValidateSystemSize(const int unknown_count, const int harmonics, std::vector<std::string> &warnings) {
    const std::size_t total_unknowns =
        static_cast<std::size_t>(unknown_count) * static_cast<std::size_t>(2 * harmonics + 1);
    if (total_unknowns > MAX_DENSE_UNKNOWNS) {
        return std::unexpected(std::format(
            "Harmonic balance would solve {} unknowns ({} per harmonic x {}), more than the {} it can hold; use fewer "
            "harmonics",
            total_unknowns,
            unknown_count,
            2 * harmonics + 1,
            MAX_DENSE_UNKNOWNS));
    }
    if (total_unknowns > MAX_DENSE_UNKNOWNS_WARNING) {
        warnings.push_back(std::format(
            "Harmonic Balance system has {} unknowns ({} MNA x {}); solving dense Jacobian may take noticeable time",
            total_unknowns,
            unknown_count,
            2 * harmonics + 1));
    }
    return {};
}

/**
 * @brief   Finds the periodic pulse or clock whose edge is faster than the time grid, which the spectrum cannot hold.
 * @param[in] compiled      The circuit, whose sources are looked at in device order.
 * @param[in] config        Fundamental frequency of the balance.
 * @param[in] sample_count  Samples of the period.
 * @return  The warning for the first such source, or empty when every edge is resolved.
 * @note    The sources must have passed ValidateDevices().
 */
std::optional<std::string> FindSharpEdgeWarning(const CompiledCircuit &compiled,
                                                const HarmonicBalanceConfig &config,
                                                const std::size_t sample_count) {
    const double sample_dt = 1.0 / config.FundamentalFrequency / static_cast<double>(sample_count);
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        const SourcePeriodicity *periodicity = FindSource(*device).Periodicity;
        if (periodicity == nullptr || !periodicity->IsSupportedInHb || periodicity->Frequency <= 0.0) {
            continue;
        }
        const bool is_edged =
            periodicity->Kind == SourceWaveformKind::PULSE || periodicity->Kind == SourceWaveformKind::CLOCK;
        if (is_edged && periodicity->MinEdgeTime < sample_dt) {
            return std::format(
                "A periodic pulse/clock edge ({:.3g} s) is faster than the time grid ({:.3g} s); increase Samples or "
                "Harmonics to reduce Gibbs/aliasing distortion",
                periodicity->MinEdgeTime,
                sample_dt);
        }
    }
    return std::nullopt;
}

/**
 * @brief   Sorts the devices into linear, nonlinear, nodesets and independent sources.
 * @param[in] compiled  The circuit, which must have passed ValidateDevices().
 * @return  The devices of each role, in circuit order.
 */
PartitionedCircuit PartitionDevices(const CompiledCircuit &compiled) {
    PartitionedCircuit partitioned;
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (auto *vs = dynamic_cast<VoltageSourceDevice *>(device.get())) {
            partitioned.VoltageSources.push_back(vs);
        } else if (auto *cs = dynamic_cast<CurrentSourceDevice *>(device.get())) {
            partitioned.CurrentSources.push_back(cs);
        }

        if (dynamic_cast<const NodesetDevice *>(device.get()) != nullptr) {
            partitioned.Nodesets.push_back(device.get());
        } else if (device->IsLinear()) {
            partitioned.LinearDevices.push_back(device.get());
        } else {
            partitioned.NonlinearDevices.push_back(device.get());
        }
    }
    return partitioned;
}

/**
 * @brief   Stamps the linear devices once and samples the sources over a period into the excitation B.
 * @param[in,out] ws           Workspace receiving GLin, CLin and BaseRhsHarmonics.
 * @param[in]     partitioned  The devices by role.
 * @param[in]     period       Period of the fundamental in seconds.
 */
void AssembleLinearAndExcitation(HbWorkspace &ws, const PartitionedCircuit &partitioned, const double period) {
    const std::vector<double> zero_x(static_cast<std::size_t>(ws.UnknownCount), 0.0);
    ws.GLin.Clear();
    ws.CLin.Clear();
    for (Device *device : partitioned.LinearDevices) {
        device->Stamp(ws.GLin, zero_x, TimeContext{});
        device->StampAc(ws.CLin, zero_x);
    }

    // Sample the independent sources' RHS over one steady-state period [0, T0) and transform to B
    MnaSystem rhs_probe(ws.IsNodeUnknown);
    std::vector<double> rhs_samples(static_cast<std::size_t>(ws.UnknownCount) * ws.SampleCount, 0.0);
    for (std::size_t m = 0; m < ws.SampleCount; ++m) {
        rhs_probe.Clear();
        const double t_m = static_cast<double>(m) * period / static_cast<double>(ws.SampleCount);
        const TimeContext steady_state{.IsPeriodicSteadyState = true, .Time = t_m};
        for (VoltageSourceDevice *vs : partitioned.VoltageSources) {
            vs->Stamp(rhs_probe, zero_x, steady_state);
        }
        for (CurrentSourceDevice *cs : partitioned.CurrentSources) {
            cs->Stamp(rhs_probe, zero_x, steady_state);
        }
        const std::vector<double> &rhs = rhs_probe.Rhs();
        for (int u = 0; u < ws.UnknownCount; ++u) {
            rhs_samples[static_cast<std::size_t>(u) * ws.SampleCount + m] = rhs[static_cast<std::size_t>(u)];
        }
    }

    std::vector<double> series(ws.SampleCount);
    for (int u = 0; u < ws.UnknownCount; ++u) {
        const std::size_t base = static_cast<std::size_t>(u) * ws.SampleCount;
        std::copy_n(rhs_samples.data() + base, ws.SampleCount, series.data());
        (void)ws.Plan.RealToHarmonics(series, ws.Harmonics, ws.CoeffsScratch, ws.FftScratch);
        for (int c = 0; c < ws.CoeffsPerUnknown; ++c) {
            ws.BaseRhsHarmonics[ws.Index(u, c)] = ws.CoeffsScratch[static_cast<std::size_t>(c)];
        }
    }
}

} // namespace Nodalys
