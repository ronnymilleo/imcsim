/**
 * @file    block_definitions.h
 * @brief   Checks, draws, compares and refreshes the block definitions a schematic keeps.
 * @details The bookkeeping of making and editing a block, apart from the schematic that holds the blocks: it works on
 *          the parts, wires and definitions it is given.
 */

#ifndef IMCSIM_BLOCK_DEFINITIONS_H
#define IMCSIM_BLOCK_DEFINITIONS_H

#include "base/helpers.h"
#include "circuit/components/subcircuit.h"
#include "schematic/schematic_file.h"
#include "ui_elements/ui_element.h"
#include "ui_elements/ui_wire.h"
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  SubcircuitPinCandidate
 * @brief   A pin marker that can become a pin of a block: where it sits and the name it suggests.
 * @details The name is the marker's own, or P1, P2... for an unnamed marker.
 */
struct SubcircuitPinCandidate {
    GridPoint Point;
    std::string SuggestedName;
};

// Checking
std::expected<void, std::string> ValidateMetadata(const std::vector<SavedSubcircuitPin> &pins,
                                                  const std::vector<Core::SubcircuitParameter> &parameters);
std::expected<void, std::string> ValidateExpressions(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                     const std::vector<Core::SubcircuitParameter> &parameters);
std::expected<void, std::string> CheckPinChoice(const std::vector<SubcircuitPinCandidate> &candidates,
                                                const std::vector<SavedSubcircuitPin> &pins);

// Pins
std::expected<std::vector<SubcircuitPinCandidate>, std::string>
CollectSubcircuitPinMarkers(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                            const std::set<std::size_t> *selected);
std::vector<std::optional<std::size_t>> MatchPins(const std::vector<SavedSubcircuitPin> &old_pins,
                                                  const std::vector<SavedSubcircuitPin> &new_pins);

// Definitions
std::expected<SavedSubcircuit, std::string>
DrawDefinition(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
               const std::set<std::size_t> &selected_elements, const std::set<std::size_t> &selected_wires,
               const std::vector<SavedSubcircuit> &definitions, SavedSubcircuit definition);
std::string DescribeDefinition(const SavedSubcircuit &definition);
std::vector<SavedSubcircuit> CopyDefinitions(const std::vector<SavedSubcircuit> &definitions);
std::string FindFreeName(const std::string &name, const std::vector<SavedSubcircuit> &definitions);

// Instances
void RefreshInstancesIn(std::vector<std::unique_ptr<UIElement>> &elements, std::vector<UIWire> &wires,
                        const SavedSubcircuit &definition, const std::vector<std::string> &old_names,
                        const std::vector<std::optional<std::size_t>> &matches);

} // namespace GUI

#endif // IMCSIM_BLOCK_DEFINITIONS_H
