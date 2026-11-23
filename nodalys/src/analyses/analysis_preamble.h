/**
 * @file    analysis_preamble.h
 * @brief   The steps that the small-signal analyses take before their own: the checks of the output node and the
 *          input source, the operating point and the reactive system linearized around it.
 */

#ifndef NODALYS_ANALYSIS_PREAMBLE_H
#define NODALYS_ANALYSIS_PREAMBLE_H

#include "compiler/compiled_circuit.h"
#include "devices/independent_sources.h"
#include "mna/mna_system.h"
#include "newton/newton.h"
#include "nodalys/types.h"
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  InputSource
 * @brief   The independent source that drives an analysis, which is a voltage source or a current source.
 * @details Points to a non-null independent source owned by the circuit.
 */
struct InputSource {
    const IndependentSourceDevice *Source = nullptr;

    bool IsVoltage() const;
    const Device &GetDevice() const;
    const IndependentSourceDevice &GetSource() const;
    const VoltageSourceDevice *AsVoltage() const;
};

// Checks
[[nodiscard]] std::optional<std::string> CheckOutputNode(const CompiledCircuit &compiled, int node);

// Input source
[[nodiscard]] std::expected<Device *, std::string> FindSourceByName(const CompiledCircuit &compiled,
                                                                    std::string_view name);
[[nodiscard]] std::expected<InputSource, std::string>
ResolveInputSource(const CompiledCircuit &compiled, std::string_view name, std::string_view purpose);

// Operating point
[[nodiscard]] std::expected<std::vector<double>, std::string>
SolveOperatingPoint(CompiledCircuit &compiled,
                    const ConvergenceOptions &options,
                    std::string_view failure_message,
                    std::vector<DiagnosticMessage> *messages);
void DescribeOutcome(const NewtonOutcome &outcome, const char *analysis, std::vector<DiagnosticMessage> *messages);

// Reactive system
MnaSystem MakeEmptySystemLike(const MnaSystem &system);
MnaSystem BuildReactiveSystem(const CompiledCircuit &compiled, const std::vector<double> &x_dc);

} // namespace Nodalys

#endif // NODALYS_ANALYSIS_PREAMBLE_H
