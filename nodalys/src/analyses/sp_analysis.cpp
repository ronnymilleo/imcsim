/**
 * @file    sp_analysis.cpp
 * @brief   Multiport scattering parameter (S-parameter) analysis solver.
 */

#include "analyses/analyses.h"

#include "analyses/analysis_preamble.h"
#include "analyses/frequency_sweep.h"
#include "analyses/port_matrices.h"
#include "devices/passive_devices.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <optional>
#include <set>
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

/**
 * @struct  PortDrives
 * @brief   What each port needs to be excited and read, one entry per port in the order of the ports.
 */
struct PortDrives {
    /// Unknown of the port node, or -1 when the port is on ground.
    std::vector<int> Unknowns;
    std::vector<double> SqrtConductances;
    /// Norton current source of each port, as the right-hand side that excites it.
    std::vector<std::vector<std::complex<double>>> Excitations;
};

// The messages of the checks that imcsim runs before any backend are worded the same way here, so a cause reads the
// same wherever it is found: tests/s_parameters_tests.cpp keeps the two in step

std::expected<std::vector<double>, std::string> GenerateFrequencyPoints(const SParameterConfig &settings) {
    if (settings.StartFrequency <= 0.0) {
        return std::unexpected("The start frequency must be positive");
    }
    if (settings.StopFrequency < settings.StartFrequency) {
        return std::unexpected("The stop frequency must be higher than the start frequency");
    }
    if (settings.Points <= 0) {
        return std::unexpected(settings.SweepType == FrequencySweepType::LINEAR
                                   ? "The number of points must be at least 1"
                                   : "The points per decade or octave must be at least 1");
    }

    return MakeFrequencySweep(settings.SweepType, settings.StartFrequency, settings.StopFrequency, settings.Points);
}

std::optional<std::string> CheckPortNumbers(const std::vector<CircuitPort> &sorted_ports) {
    std::set<int> numbers;
    for (const CircuitPort &port : sorted_ports) {
        if (!numbers.insert(port.Number).second) {
            return std::format("Two S-parameter ports are numbered {}; give each port its own number", port.Number);
        }
    }
    // The matrix and the Touchstone file index the ports by number, so a gap would leave a row with no port
    for (int number = 1; number <= static_cast<int>(sorted_ports.size()); ++number) {
        if (!numbers.contains(number)) {
            return std::format("The S-parameter ports must be numbered 1 to {} with no gap; port {} is missing",
                               sorted_ports.size(),
                               number);
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckPortNodes(const std::vector<CircuitPort> &ports, const int circuit_node_count) {
    for (std::size_t first = 0; first < ports.size(); ++first) {
        const CircuitPort &port = ports[first];
        if (port.ReferenceOhms <= 0.0) {
            return std::format("The reference impedance of port {} must be positive", port.Number);
        }
        if (port.Node == 0) {
            return std::format("Port {} is on ground; an S-parameter port needs a node of its own", port.Number);
        }
        if (port.Node < 0 || port.Node >= circuit_node_count) {
            return std::format("Port {} is on a node that is not in the circuit", port.Number);
        }
        for (std::size_t second = first + 1; second < ports.size(); ++second) {
            if (ports[second].Node == port.Node) {
                return std::format("Ports {} and {} are on the same node; each port needs a node of its own",
                                   port.Number,
                                   ports[second].Number);
            }
        }
    }
    return std::nullopt;
}

std::expected<std::vector<CircuitPort>, std::string> ValidatePorts(const std::vector<CircuitPort> &raw_ports,
                                                                   const int circuit_node_count) {
    if (raw_ports.empty()) {
        return std::unexpected("The circuit has no S-parameter port");
    }
    // The ports may be listed in any order; the matrix follows their numbers
    std::vector<CircuitPort> ports = raw_ports;
    std::ranges::sort(ports, {}, &CircuitPort::Number);
    if (const std::optional<std::string> error = CheckPortNumbers(ports)) {
        return std::unexpected(*error);
    }
    if (const std::optional<std::string> error = CheckPortNodes(ports, circuit_node_count)) {
        return std::unexpected(*error);
    }
    return ports;
}

/**
 * @brief   Fills the impedance and admittance matrices of a frequency from its S matrix.
 */
void AddPortMatrices(SParameterResult &result, const PortMatrix &scattering) {
    result.ImpedanceMatrices.push_back(ComputeImpedanceMatrix(scattering, result.ReferenceOhms));
    result.AdmittanceMatrices.push_back(ComputeAdmittanceMatrix(scattering, result.ReferenceOhms));
}

/**
 * @brief   Terminates each port with its reference impedance to ground, which the operating point is solved with.
 * @details The terminations are devices added to the circuit, so the guard that is returned takes them out again
 *          when it goes out of scope.
 * @return  The guard that removes the terminations.
 */
DeviceRestoreGuard AddPortTerminations(CompiledCircuit &compiled, const std::vector<CircuitPort> &ports) {
    const std::size_t original_device_count = compiled.Devices.size();
    for (const CircuitPort &port : ports) {
        const int u = compiled.System->NodeIndex(port.Node);
        compiled.Devices.push_back(std::make_unique<ResistorDevice>(u, -1, port.ReferenceOhms));
    }
    return DeviceRestoreGuard{compiled.Devices, original_device_count};
}

/**
 * @brief   Pre-assembles the excitation of each port.
 * @details Port j is excited with the Norton equivalent current \f$ I_j = 2 \sqrt{G_j} \f$, where
 *          \f$ G_j = 1 / Z_{0j} \f$.
 */
PortDrives MakePortDrives(const CompiledCircuit &compiled, const std::vector<CircuitPort> &ports) {
    const std::size_t port_count = ports.size();
    const auto size = static_cast<std::size_t>(compiled.System->GetSize());
    PortDrives drives{.Unknowns = std::vector<int>(port_count),
                      .SqrtConductances = std::vector<double>(port_count),
                      .Excitations = std::vector<std::vector<std::complex<double>>>(
                          port_count,
                          std::vector<std::complex<double>>(size, std::complex<double>(0.0, 0.0)))};
    for (std::size_t j = 0; j < port_count; ++j) {
        const double gj = 1.0 / ports[j].ReferenceOhms;
        drives.SqrtConductances[j] = std::sqrt(gj);
        const int uj = compiled.System->NodeIndex(ports[j].Node);
        drives.Unknowns[j] = uj;
        if (uj >= 0) {
            drives.Excitations[j][static_cast<std::size_t>(uj)] =
                std::complex<double>(2.0 * drives.SqrtConductances[j], 0.0);
        }
    }
    return drives;
}

SParameterResult MakeSweepResult(const std::vector<double> &frequencies, const std::vector<CircuitPort> &ports) {
    SParameterResult result;
    result.Frequencies = frequencies;
    result.ReferenceOhms.reserve(ports.size());
    for (const CircuitPort &port : ports) {
        result.ReferenceOhms.push_back(port.ReferenceOhms);
    }
    result.Values.reserve(frequencies.size() * ports.size() * ports.size());
    return result;
}

/**
 * @brief   Converts the port voltages of one frequency to the power-wave scattering matrix
 *          \f$ S_{ij} = V_i \sqrt{G_i} - \delta_{ij} \f$.
 * @param[in] drives     The ports.
 * @param[in] solutions  The solution of the circuit excited at each port.
 * @param[in] frequency  Frequency of the solutions in hertz, for the message of a refusal.
 * @return  The matrix in row-major order, or the entry that is not finite.
 */
std::expected<PortMatrix, std::string>
ScatteringFromSolutions(const PortDrives &drives,
                        const std::vector<std::vector<std::complex<double>>> &solutions,
                        const double frequency) {
    const std::size_t port_count = drives.Unknowns.size();
    PortMatrix scattering;
    scattering.reserve(port_count * port_count);
    for (std::size_t i = 0; i < port_count; ++i) {
        const int ui = drives.Unknowns[i];
        const double gi_sqrt = drives.SqrtConductances[i];
        for (std::size_t j = 0; j < port_count; ++j) {
            const std::complex<double> vi =
                (ui < 0) ? std::complex<double>(0.0, 0.0) : solutions[j][static_cast<std::size_t>(ui)];
            const double delta_ij = (i == j) ? 1.0 : 0.0;
            const std::complex<double> s_ij = vi * gi_sqrt - delta_ij;
            if (!std::isfinite(s_ij.real()) || !std::isfinite(s_ij.imag())) {
                return std::unexpected(std::format("The native solver produced non-finite S({}, {}) at f = {} Hz",
                                                   i + 1,
                                                   j + 1,
                                                   frequency));
            }
            scattering.push_back(s_ij);
        }
    }
    return scattering;
}

/**
 * @brief   Solves the circuit at each frequency with each port excited in turn and adds the matrices of the frequency.
 */
std::optional<std::string> AddFrequencyPoints(SParameterResult &result,
                                              const CompiledCircuit &compiled,
                                              const MnaSystem &reactive,
                                              const PortDrives &drives) {
    std::vector<std::vector<std::complex<double>>> solutions;
    for (const double frequency : result.Frequencies) {
        const double omega = 2.0 * std::numbers::pi * frequency;
        if (!compiled.System->SolveAcMultiple(reactive, omega, drives.Excitations, solutions)) {
            return std::format("The native S-parameter solver encountered a singular matrix at f = {} Hz", frequency);
        }
        const std::expected<PortMatrix, std::string> scattering = ScatteringFromSolutions(drives, solutions, frequency);
        if (!scattering) {
            return scattering.error();
        }
        result.Values.insert(result.Values.end(), scattering->begin(), scattering->end());
        AddPortMatrices(result, *scattering);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Computes the scattering, impedance and admittance matrices of the ports of the circuit over a range of
 *          frequencies.
 * @param[in,out] compiled  Compiled circuit with its matrix and devices; the terminations of the ports are added for
 *                          the analysis and taken out again.
 * @param[in]     settings  Ports, when they are not those of the circuit, and frequency range.
 * @param[in]     options   Numerical limits of the operating point.
 * @param[out]    messages  Diagnostic messages collected during the analysis; may be null.
 * @return  The matrices at each frequency, or an error description.
 */
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

    const auto freq_points = GenerateFrequencyPoints(settings);
    if (!freq_points) {
        return std::unexpected(freq_points.error());
    }
    const std::vector<double> &frequencies = *freq_points;

    // The terminations stay in the circuit until the end of the analysis
    const DeviceRestoreGuard restore_guard = AddPortTerminations(compiled, ports);

    const std::expected<std::vector<double>, std::string> x_dc =
        SolveOperatingPoint(compiled,
                            options,
                            "The native solver found no operating point to linearize the S-parameter analysis around",
                            messages);
    if (!x_dc) {
        return std::unexpected(x_dc.error());
    }
    const MnaSystem reactive = BuildReactiveSystem(compiled, *x_dc);
    const PortDrives drives = MakePortDrives(compiled, ports);

    SParameterResult result = MakeSweepResult(frequencies, ports);
    if (const std::optional<std::string> failure = AddFrequencyPoints(result, compiled, reactive, drives)) {
        return std::unexpected(*failure);
    }

    if (messages != nullptr) {
        messages->push_back({std::format("nodalys: computed S-parameters for {} ports across {} frequency points",
                                         ports.size(),
                                         frequencies.size()),
                             false});
    }

    return result;
}

} // namespace Nodalys
