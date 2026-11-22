/**
 * @file    hb_setup.h
 * @brief   Checks a circuit for harmonic balance and prepares its linear part and its excitation.
 */

#ifndef NODALYS_HB_SETUP_H
#define NODALYS_HB_SETUP_H

#include "analyses/hb_workspace.h"
#include "compiler/compiled_circuit.h"
#include "devices/independent_sources.h"
#include "nodalys/analyses.h"
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  PartitionedCircuit
 * @brief   The devices of a circuit sorted by the role they play in the balance.
 */
struct PartitionedCircuit {
    std::vector<Device *> LinearDevices;
    std::vector<Device *> NonlinearDevices;
    /// The nodesets, which only guide the DC bias that starts the balance.
    std::vector<Device *> Nodesets;
    std::vector<VoltageSourceDevice *> VoltageSources;
    std::vector<CurrentSourceDevice *> CurrentSources;
};

// Validation
[[nodiscard]] std::expected<std::size_t, std::string>
ValidateAndResolveSampleCount(const HarmonicBalanceConfig &config);
[[nodiscard]] std::expected<void, std::string> ValidateDevices(const CompiledCircuit &compiled,
                                                               const HarmonicBalanceConfig &config);
[[nodiscard]] std::expected<void, std::string>
ValidateSystemSize(int unknown_count, int harmonics, std::vector<std::string> &warnings);
[[nodiscard]] std::optional<std::string>
FindSharpEdgeWarning(const CompiledCircuit &compiled, const HarmonicBalanceConfig &config, std::size_t sample_count);

// Partition
PartitionedCircuit PartitionDevices(const CompiledCircuit &compiled);

// Linear part and excitation
void AssembleLinearAndExcitation(HbWorkspace &ws, const PartitionedCircuit &partitioned, double period);

} // namespace Nodalys

#endif // NODALYS_HB_SETUP_H
