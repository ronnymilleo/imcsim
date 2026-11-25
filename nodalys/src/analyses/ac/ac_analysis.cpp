/**
 * @file    ac_analysis.cpp
 * @brief   Small-signal AC frequency response analysis solver.
 */

#include "analyses/analyses.h"

#include "analyses/common/analysis_preamble.h"
#include "analyses/common/frequency_sweep.h"
#include "devices/device.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <utility>

namespace Nodalys {

namespace {

constexpr double MIN_MAGNITUDE = 1e-15;
constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;

AcSweepResult MakeSweepResult(const CompiledCircuit &compiled, std::vector<double> frequencies) {
    AcSweepResult result;
    result.Frequencies = std::move(frequencies);

    const std::size_t point_count = result.Frequencies.size();
    result.NodeMagnitudesDecibels.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
    result.NodePhasesDegrees.resize(static_cast<std::size_t>(compiled.CircuitNodeCount));
    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        result.NodeMagnitudesDecibels[static_cast<std::size_t>(node)].reserve(point_count);
        result.NodePhasesDegrees[static_cast<std::size_t>(node)].reserve(point_count);
    }
    result.CurrentMagnitudesDecibels.resize(compiled.Currents.size());
    result.CurrentPhasesDegrees.resize(compiled.Currents.size());
    for (std::size_t index = 0; index < compiled.Currents.size(); ++index) {
        result.CurrentMagnitudesDecibels[index].Name = compiled.Currents[index].Name;
        result.CurrentMagnitudesDecibels[index].Values.reserve(point_count);
        result.CurrentPhasesDegrees[index].Name = compiled.Currents[index].Name;
        result.CurrentPhasesDegrees[index].Values.reserve(point_count);
    }
    return result;
}

void AddNodeResponses(AcSweepResult &result,
                      const CompiledCircuit &compiled,
                      const std::vector<std::complex<double>> &x_ac) {
    for (int node = 1; node < compiled.CircuitNodeCount; ++node) {
        const int unknown = compiled.System->NodeIndex(node);
        const std::complex<double> v =
            unknown < 0 ? std::complex<double>{0.0, 0.0} : x_ac[static_cast<std::size_t>(unknown)];
        const double mag_db = 20.0 * std::log10(std::max(std::abs(v), MIN_MAGNITUDE));
        const double phase_deg = std::arg(v) * RAD_TO_DEG;
        result.NodeMagnitudesDecibels[static_cast<std::size_t>(node)].push_back(mag_db);
        result.NodePhasesDegrees[static_cast<std::size_t>(node)].push_back(phase_deg);
    }
}

void AddCurrentResponses(AcSweepResult &result,
                         const CompiledCircuit &compiled,
                         const std::vector<double> &x_dc,
                         const std::vector<std::complex<double>> &x_ac,
                         const double omega) {
    for (std::size_t index = 0; index < compiled.Currents.size(); ++index) {
        std::complex<double> current{0.0, 0.0};
        for (const CurrentContribution &contribution : compiled.Currents[index].Contributions) {
            current += contribution.Element->AcTerminalCurrent(contribution.Terminal, x_dc, x_ac, omega);
        }
        const double mag_db = 20.0 * std::log10(std::max(std::abs(current), MIN_MAGNITUDE));
        const double phase_deg = std::arg(current) * RAD_TO_DEG;
        result.CurrentMagnitudesDecibels[index].Values.push_back(mag_db);
        result.CurrentPhasesDegrees[index].Values.push_back(phase_deg);
    }
}

} // namespace

/**
 * @brief   Computes the small-signal response of every node and current over a range of frequencies.
 * @param[in,out] compiled  Compiled circuit with its matrix and devices.
 * @param[in]     settings  Sweep type and frequency range.
 * @param[in]     options   Numerical limits of the operating point.
 * @param[out]    messages  Diagnostic messages collected during the analysis; may be null.
 * @return  The magnitudes in decibels and the phases in degrees at each frequency, or an error description.
 */
std::expected<AcSweepResult, std::string> ComputeACSweep(CompiledCircuit &compiled,
                                                         const AcSweepConfig &settings,
                                                         const ConvergenceOptions &options,
                                                         std::vector<DiagnosticMessage> *messages) {
    if (!IsValidSweepRange(settings.StartFrequency, settings.StopFrequency, settings.PointsPerDecade)) {
        return std::unexpected("The AC sweep has invalid frequency settings; check start, stop and points per decade");
    }
    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "Nodalys found no operating point to linearize the AC sweep around",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }
    const MnaSystem reactive = BuildReactiveSystem(compiled, *x_dc);
    const std::vector<double> frequencies = MakeFrequencySweep(settings.SweepType,
                                                               settings.StartFrequency,
                                                               settings.StopFrequency,
                                                               settings.PointsPerDecade);
    AcSweepResult result = MakeSweepResult(compiled, frequencies);

    std::vector<std::complex<double>> x_ac(static_cast<std::size_t>(compiled.System->GetSize()));
    for (const double frequency : result.Frequencies) {
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAc(reactive, omega, x_ac)) {
            return std::unexpected(
                std::format("The Nodalys AC solver encountered a singular matrix at f = {} Hz", frequency));
        }
        AddNodeResponses(result, compiled, x_ac);
        AddCurrentResponses(result, compiled, *x_dc, x_ac, omega);
    }

    if (messages) {
        messages->push_back(
            {std::format("nodalys: AC sweep computed {} frequency points", result.Frequencies.size()), false});
    }
    return result;
}

} // namespace Nodalys
