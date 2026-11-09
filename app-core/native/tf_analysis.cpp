/**
 * @file    tf_analysis.cpp
 * @brief   Small-signal DC Transfer Function analysis (`ComputeTransferFunction`, SPICE `.tf`) of the Nodalys solver.
 * @details
 * ### How Small-Signal DC Transfer Function Analysis Works
 * Given an input source $S_{\text{in}}$ (`VoltageSourceDevice` or `CurrentSourceDevice`) and an output node $n_{\text{out}}$:
 * 1. **DC Operating Point ($x_{\text{dc}}$)**: Solves the nonlinear DC equations $F(x_{\text{dc}}) = 0$ via `SolveDc()`.
 *    We then re-stamp the linearized Jacobian $A_{\text{DC}}(x_{\text{dc}})$ with an open-circuit floor conductance
 *    $G_{\text{open}} = 10^{-20}\text{ S}$ so an unloaded ideal source reports $Z_{\text{in}} = 10^{20}\text{ }\Omega$,
 *    matching `ngspice`.
 * 2. **Solve #1 — Unit Input Perturbation ($A_{\text{DC}} \cdot \delta x_{\text{in}} = e_{\text{in}}$)**:
 *    - If $S_{\text{in}}$ is a voltage source ($\Delta V_{\text{in}} = 1\text{ V}$ on branch $b_{\text{in}}$):
 *      $$\text{Gain} = \delta V_{n_{\text{out}}} \quad (\text{V/V}), \qquad Z_{\text{in}} = -\frac{1}{\delta I_{b_{\text{in}}}} \quad (\Omega)$$
 *    - If $S_{\text{in}}$ is a current source ($\Delta I_{\text{in}} = 1\text{ A}$ from $n_+$ to $n_-$):
 *      $$\text{Gain} = \delta V_{n_{\text{out}}} \quad (\text{V/A}), \qquad Z_{\text{in}} = \delta V_{n_-} - \delta V_{n_+} \quad (\Omega)$$
 * 3. **Solve #2 — Unit Output Test Current ($A_{\text{DC}} \cdot \delta x_{\text{out}} = e_{\text{out}}$)**:
 *    Injects $+1\text{ A}$ into $n_{\text{out}}$ with all independent sources zeroed:
 *    $$Z_{\text{out}} = \delta x_{\text{out}}[n_{\text{out}}] \quad (\Omega)$$
 */

#include "native/analyses.h"

#include "native/devices.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Core::Native {

namespace {

/// Open-circuit impedance cap (in Ohms) matching SPICE3f5 / ngspice `tfanal.c`.
constexpr double OPEN_CIRCUIT_IMPEDANCE = 1e20;
/// Threshold below which an output impedance held by an ideal voltage source snaps to exact 0 Ohm.
constexpr double SHORT_CIRCUIT_THRESHOLD = 1e-15;

} // namespace

std::expected<TransferFunction, std::string> ComputeTransferFunction(const Circuit &circuit, CompiledCircuit &compiled,
                                                                     const TransferFunctionSettings &settings,
                                                                     const ConvergenceOptions &options,
                                                                     std::vector<SimulatorMessage> &messages) {
    if (settings.OutputNode <= 0 || settings.OutputNode >= compiled.CircuitNodeCount) {
        return std::unexpected(std::format("The circuit has no output node {}", settings.OutputNode));
    }
    const auto source_it = compiled.SourcesByName.find(settings.InputSource);
    if (source_it == compiled.SourcesByName.end()) {
        const bool exists = std::ranges::any_of(circuit.GetEntries(), [&](const CircuitEntry &entry) {
            return entry.Part->GetName() == settings.InputSource;
        });
        return std::unexpected(
            exists ? std::format("The source {} cannot drive a transfer function", settings.InputSource)
                   : std::format("The circuit has no source named {}", settings.InputSource));
    }

    auto *voltage_source = dynamic_cast<VoltageSourceDevice *>(source_it->second);
    auto *current_source = dynamic_cast<CurrentSourceDevice *>(source_it->second);
    if (voltage_source == nullptr && current_source == nullptr) {
        return std::unexpected(std::format("The source {} cannot drive a transfer function", settings.InputSource));
    }

    // Step 1: Solve the DC operating point
    const int size = compiled.System->Size();
    std::vector<double> x_dc(static_cast<std::size_t>(size), 0.0);
    const NewtonOutcome outcome = SolveDc(*compiled.System, compiled.Devices, x_dc, options, TimeContext{});
    if (!outcome.Converged) {
        return std::unexpected("The native solver found no operating point for the transfer function");
    }
    DescribeOutcome(outcome, "operating point", messages);

    // SolveDc leaves compiled.System stamped with the linearized Jacobian at x_dc (including options.Gmin on all nodes).
    const int out_idx = compiled.System->NodeIndex(settings.OutputNode);

    // Step 2: Solve #1 — Unit perturbation on the input source
    compiled.System->ClearRhs();
    if (voltage_source != nullptr) {
        compiled.System->AddRhs(voltage_source->Branch(), 1.0);
    } else {
        compiled.System->AddRhs(current_source->NodeA(), -1.0);
        compiled.System->AddRhs(current_source->NodeB(), 1.0);
    }

    std::vector<double> dx_in(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_in)) {
        return std::unexpected("The native solver encountered a singular matrix while computing the transfer function");
    }

    TransferFunction result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = voltage_source != nullptr ? "V" : "A";
    result.Gain = dx_in[static_cast<std::size_t>(out_idx)];

    if (voltage_source != nullptr) {
        const double current_into_circuit = -dx_in[static_cast<std::size_t>(voltage_source->Branch())];
        // If the only current drawn from the 1 V source is the Gmin regularization floor, the port is an open circuit
        if (std::fabs(current_into_circuit) <= 1.01 * options.Gmin) {
            result.InputImpedance = OPEN_CIRCUIT_IMPEDANCE;
        } else {
            result.InputImpedance = 1.0 / current_into_circuit;
        }
    } else {
        const double va =
            current_source->NodeA() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(current_source->NodeA())];
        const double vb =
            current_source->NodeB() < 0 ? 0.0 : dx_in[static_cast<std::size_t>(current_source->NodeB())];
        result.InputImpedance = std::min(vb - va, OPEN_CIRCUIT_IMPEDANCE);
    }

    // Step 3: Solve #2 — Unit test current (+1 A) injected into the output node
    compiled.System->ClearRhs();
    compiled.System->AddRhs(out_idx, 1.0);
    std::vector<double> dx_out(static_cast<std::size_t>(size), 0.0);
    if (!compiled.System->Solve(dx_out)) {
        return std::unexpected("The native solver encountered a singular matrix while computing the output impedance");
    }
    const double z_out = dx_out[static_cast<std::size_t>(out_idx)];
    result.OutputImpedance =
        std::fabs(z_out) < SHORT_CIRCUIT_THRESHOLD ? 0.0 : std::min(z_out, OPEN_CIRCUIT_IMPEDANCE);

    messages.push_back({"native: computed small-signal transfer function and impedances", false});
    return result;
}

} // namespace Core::Native
