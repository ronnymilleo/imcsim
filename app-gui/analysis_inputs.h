/**
 * @file    analysis_inputs.h
 * @brief   What an analysis takes from the circuit and the stored settings: the sources, nodes and inputs it runs with.
 */

#ifndef IMCSIM_ANALYSIS_INPUTS_H
#define IMCSIM_ANALYSIS_INPUTS_H

#include "analysis_settings.h"
#include "circuit.h"
#include "helpers.h"
#include "schematic.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SweepSource
 * @brief   A source a DC sweep can drive, and the unit of its values.
 */
struct SweepSource {
    std::string Name;
    const char *Unit;
};

// The swept source defaults to the first one and the stepped source to the second, so a transistor curve family needs
// no picking
constexpr std::size_t SWEPT_SOURCE_INDEX = 0;
constexpr std::size_t STEPPED_SOURCE_INDEX = 1;

// Sources
std::vector<SweepSource> ListSweepSources(const Core::Circuit &circuit);
const std::string &ResolveSweepSource(const Core::SweepRange &range, const std::vector<SweepSource> &sources,
                                      std::size_t preferred_index);
std::string ResolveOptionalInput(const std::string &stored, const std::vector<std::string> &sources);

// Nodes and logic inputs
std::optional<int> ResolveOutputNode(const std::optional<GridPoint> &point, const Core::Circuit &circuit,
                                     Schematic &schematic);
std::vector<int> ListMeasuredNodes(const Core::Circuit &circuit, const Schematic &schematic);
std::vector<std::string> ResolveTruthTableInputs(const std::vector<std::string> &stored,
                                                 const std::vector<std::string> &available);

// Stepped parameter
std::string GetParameterUnit(const Core::Circuit &circuit, const std::string &part);

} // namespace GUI

#endif // IMCSIM_ANALYSIS_INPUTS_H
