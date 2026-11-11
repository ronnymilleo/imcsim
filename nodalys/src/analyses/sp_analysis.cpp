/**
 * @file    sp_analysis.cpp
 * @brief   Multiport scattering parameter (S-parameter) analysis solver.
 */

#include "analyses/analyses.h"

#include "devices/devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

struct DeviceRestoreGuard {
    std::vector<std::unique_ptr<Device>> &Devices;
    std::size_t OriginalCount = 0;

    ~DeviceRestoreGuard() {
        if (Devices.size() > OriginalCount) {
            Devices.erase(Devices.begin() + static_cast<std::ptrdiff_t>(OriginalCount), Devices.end());
        }
    }
};

std::expected<std::vector<double>, std::string> GenerateFrequencyPoints(const SParameterConfig &settings) {
    if (settings.StartFrequency <= 0.0 || settings.StopFrequency < settings.StartFrequency) {
        return std::unexpected("The S-parameter analysis has invalid frequency range; start must be > 0 and <= stop");
    }
    if (settings.Points <= 0) {
        return std::unexpected("The S-parameter analysis must have at least 1 point");
    }

    std::vector<double> frequencies;
    switch (settings.SweepType) {
    case FrequencySweepType::LINEAR: {
        if (settings.Points == 1 || settings.StartFrequency == settings.StopFrequency) {
            frequencies.push_back(settings.StartFrequency);
        } else {
            const double step =
                (settings.StopFrequency - settings.StartFrequency) / static_cast<double>(settings.Points - 1);
            for (int i = 0; i < settings.Points; ++i) {
                const double f = settings.StartFrequency + static_cast<double>(i) * step;
                frequencies.push_back(std::min(f, settings.StopFrequency));
            }
        }
        break;
    }
    case FrequencySweepType::DECADE: {
        if (settings.StartFrequency == settings.StopFrequency) {
            frequencies.push_back(settings.StartFrequency);
        } else {
            for (int index = 0;; ++index) {
                const double frequency =
                    settings.StartFrequency *
                    std::pow(10.0, static_cast<double>(index) / static_cast<double>(settings.Points));
                if (frequency > settings.StopFrequency * (1.0 + 1e-9)) {
                    break;
                }
                frequencies.push_back(std::min(frequency, settings.StopFrequency));
                if (frequency >= settings.StopFrequency * (1.0 - 1e-9)) {
                    break;
                }
            }
        }
        break;
    }
    case FrequencySweepType::OCTAVE: {
        if (settings.StartFrequency == settings.StopFrequency) {
            frequencies.push_back(settings.StartFrequency);
        } else {
            for (int index = 0;; ++index) {
                const double frequency =
                    settings.StartFrequency *
                    std::pow(2.0, static_cast<double>(index) / static_cast<double>(settings.Points));
                if (frequency > settings.StopFrequency * (1.0 + 1e-9)) {
                    break;
                }
                frequencies.push_back(std::min(frequency, settings.StopFrequency));
                if (frequency >= settings.StopFrequency * (1.0 - 1e-9)) {
                    break;
                }
            }
        }
        break;
    }
    default:
        return std::unexpected("Unknown frequency sweep type for S-parameter analysis");
    }
    return frequencies;
}

std::expected<std::vector<CircuitPort>, std::string> ValidatePorts(const std::vector<CircuitPort> &raw_ports,
                                                                   const int circuit_node_count) {
    if (raw_ports.empty()) {
        return std::unexpected("The S-parameter analysis requires at least one port");
    }

    std::vector<CircuitPort> ports = raw_ports;
    std::ranges::sort(ports, {}, &CircuitPort::Number);

    for (std::size_t i = 0; i < ports.size(); ++i) {
        const int expected_number = static_cast<int>(i + 1);
        if (ports[i].Number != expected_number) {
            return std::unexpected(
                std::format("The S-parameter ports must be numbered 1 to N without gaps; expected port {}, found {}",
                            expected_number, ports[i].Number));
        }
        if (ports[i].ReferenceOhms <= 0.0) {
            return std::unexpected(
                std::format("The reference impedance of port {} must be greater than zero", ports[i].Number));
        }
        if (ports[i].Node <= 0 || ports[i].Node >= circuit_node_count) {
            return std::unexpected(
                std::format("Port {} is connected to ground or an invalid circuit node", ports[i].Number));
        }
    }

    for (std::size_t i = 0; i < ports.size(); ++i) {
        for (std::size_t j = i + 1; j < ports.size(); ++j) {
            if (ports[i].Node == ports[j].Node) {
                return std::unexpected(std::format("Ports {} and {} cannot connect to the same node {}",
                                                   ports[i].Number, ports[j].Number, ports[i].Node));
            }
        }
    }

    return ports;
}

} // namespace

std::expected<SParameterResult, std::string> ComputeSParameters(CompiledCircuit &compiled,
                                                                const SParameterConfig &settings,
                                                                const ConvergenceOptions &options,
                                                                std::vector<DiagnosticMessage> *messages) {

    const auto validated_ports =
        ValidatePorts(settings.Ports.empty() ? compiled.Ports : settings.Ports, compiled.CircuitNodeCount);
    if (!validated_ports) {
        return std::unexpected(validated_ports.error());
    }
    const std::vector<CircuitPort> &ports = *validated_ports;
    const auto num_ports = static_cast<int>(ports.size());

    const auto freq_points = GenerateFrequencyPoints(settings);
    if (!freq_points) {
        return std::unexpected(freq_points.error());
    }
    const std::vector<double> &frequencies = *freq_points;

    const int size = compiled.System->Size();

    // 1. Terminate all ports with their reference impedance to ground for DC operating point linearization
    const std::size_t original_device_count = compiled.Devices.size();
    DeviceRestoreGuard restore_guard{compiled.Devices, original_device_count};

    for (const CircuitPort &port : ports) {
        const int u = compiled.System->NodeIndex(port.Node);
        compiled.Devices.push_back(std::make_unique<ResistorDevice>(u, -1, port.ReferenceOhms));
    }

    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveNewton(*compiled.System, compiled.Devices, x_dc, TimeContext{}, options);
    if (!outcome.Converged) {
        return std::unexpected(
            "The native solver found no operating point to linearize the S-parameter analysis around");
    }
    DescribeOutcome(outcome, "operating point", messages);

    // 2. Assemble small-signal AC reactive matrix C_ac
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int i = 0; i < size; ++i) {
        node_flags[static_cast<std::size_t>(i)] = compiled.System->IsNodeUnknown(i);
    }
    MnaSystem reactive(std::move(node_flags));
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        device->StampAc(reactive, x_dc);
    }

    // 3. Pre-assemble Norton current excitation RHS vectors for each port
    // Excite port j with Norton equivalent current Is_j = 2 * sqrt(Gj), where Gj = 1 / Z0j
    std::vector<std::vector<std::complex<double>>> rhs_list(
        static_cast<std::size_t>(num_ports),
        std::vector<std::complex<double>>(static_cast<std::size_t>(size), std::complex<double>(0.0, 0.0)));

    std::vector<double> sqrt_g(static_cast<std::size_t>(num_ports));
    std::vector<int> port_unknowns(static_cast<std::size_t>(num_ports));

    for (std::size_t j = 0; j < static_cast<std::size_t>(num_ports); ++j) {
        const double gj = 1.0 / ports[j].ReferenceOhms;
        sqrt_g[j] = std::sqrt(gj);
        const int uj = compiled.System->NodeIndex(ports[j].Node);
        port_unknowns[j] = uj;
        if (uj >= 0) {
            rhs_list[j][static_cast<std::size_t>(uj)] = std::complex<double>(2.0 * sqrt_g[j], 0.0);
        }
    }

    SParameterResult result;
    result.Frequencies = frequencies;
    result.ReferenceOhms.reserve(static_cast<std::size_t>(num_ports));
    for (const CircuitPort &port : ports) {
        result.ReferenceOhms.push_back(port.ReferenceOhms);
    }
    result.Values.reserve(frequencies.size() * static_cast<std::size_t>(num_ports * num_ports));

    // 4. Frequency sweep
    std::vector<std::vector<std::complex<double>>> solutions;
    for (const double frequency : frequencies) {
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAcMultiple(reactive, omega, rhs_list, solutions)) {
            return std::unexpected(
                std::format("The native S-parameter solver encountered a singular matrix at f = {} Hz", frequency));
        }

        // 5. Convert port voltages to power-wave scattering matrix S_ij = V_i * sqrt(G_i) - delta_ij
        for (std::size_t i = 0; i < static_cast<std::size_t>(num_ports); ++i) {
            const int ui = port_unknowns[i];
            const double gi_sqrt = sqrt_g[i];
            for (std::size_t j = 0; j < static_cast<std::size_t>(num_ports); ++j) {
                const std::complex<double> vi =
                    (ui < 0) ? std::complex<double>(0.0, 0.0) : solutions[j][static_cast<std::size_t>(ui)];
                const double delta_ij = (i == j) ? 1.0 : 0.0;
                const std::complex<double> s_ij = vi * gi_sqrt - delta_ij;
                if (!std::isfinite(s_ij.real()) || !std::isfinite(s_ij.imag())) {
                    return std::unexpected(std::format("The native solver produced non-finite S({}, {}) at f = {} Hz",
                                                       i + 1, j + 1, frequency));
                }
                result.Values.push_back(s_ij);
            }
        }
    }

    if (messages != nullptr) {
        messages->push_back({std::format("nodalys: computed S-parameters for {} ports across {} frequency points",
                                         num_ports, frequencies.size()),
                             false});
    }

    return result;
}

} // namespace Nodalys
