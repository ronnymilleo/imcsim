/**
 * @file    block_definitions.cpp
 * @brief   Checks, draws, compares and refreshes the block definitions a schematic keeps.
 */

#include "schematic/block_definitions.h"

#include "circuit/components/net_label.h"
#include "schematic/block_wiring.h"
#include "schematic/connectivity.h"
#include "schematic/subcircuit_pins.h"
#include "schematic/wire_editing.h"
#include "ui_elements/ui_subcircuit.h"
#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace GUI {

namespace {

/**
 * @struct  ReroutedPin
 * @brief   A pin that moved to another edge of a block.
 */
struct ReroutedPin {
    const UIElement *Block = nullptr;
    GridPoint OldPoint;
    GridPoint NewPoint;
};

/**
 * @struct  TerminalMoves
 * @brief   What happens to the wires on the terminals of the instances of a block that changed.
 */
struct TerminalMoves {
    /// Terminals whose wires follow them, before the change.
    std::vector<GridPoint> OldPoints;
    /// The same terminals after it, in the same order.
    std::vector<GridPoint> NewPoints;
    /// Terminals of pins that went away, whose wires go too.
    std::vector<GridPoint> Removed;
    /// Pins that moved to another edge, which get a new wire around the box from where they were.
    std::vector<ReroutedPin> Rerouted;
    /// The instances, which the new wires go around.
    RoutingObstacles Blocks;
};

/**
 * @brief   Brings one instance of a definition that changed up to date, and notes where its wires go.
 */
void RefreshInstance(UIElement &element, const SavedSubcircuit &definition, const std::vector<std::string> &old_names,
                     const std::vector<std::optional<std::size_t>> &matches, TerminalMoves &moves) {
    auto &instance = static_cast<Core::SubcircuitInstance &>(element.GetComponent());
    const std::vector<std::string> instance_pins = instance.GetPins();
    const std::vector<GridPoint> old_terminals = element.GetTerminals();
    std::vector<GridPoint> old_inwards;
    for (std::size_t terminal = 0; terminal < old_terminals.size(); ++terminal) {
        old_inwards.push_back(element.GetTerminalInward(terminal));
    }
    const auto overrides = instance.GetParameters();
    instance.ClearParameters();
    for (const auto &[parameter, value] : overrides) {
        if (std::ranges::contains(definition.Parameters, parameter, &Core::SubcircuitParameter::Name)) {
            instance.SetParameter(parameter, value);
        }
    }
    ApplyDefinitionPins(instance, definition);
    const std::vector<GridPoint> new_terminals = element.GetTerminals();
    KeepOffBlock(moves.Blocks, static_cast<const UISubcircuit &>(element));

    std::set<std::size_t> kept_terminals;
    for (std::size_t new_index = 0; new_index < matches.size(); ++new_index) {
        const std::optional<std::size_t> old_pin = matches[new_index];
        if (!old_pin) {
            continue;
        }
        // The terminal the old pin had on this instance, found by its name in case the instance is behind
        const auto found = std::ranges::find(instance_pins, old_names[*old_pin]);
        if (found == instance_pins.end()) {
            continue;
        }
        const auto terminal = static_cast<std::size_t>(found - instance_pins.begin());
        kept_terminals.insert(terminal);
        const GridPoint old_point = old_terminals[terminal];
        const GridPoint new_point = new_terminals[new_index];
        if (old_point != new_point && element.GetTerminalInward(new_index) != old_inwards[terminal]) {
            moves.Rerouted.push_back({.Block = &element, .OldPoint = old_point, .NewPoint = new_point});
        } else {
            moves.OldPoints.push_back(old_point);
            moves.NewPoints.push_back(new_point);
        }
    }
    for (std::size_t terminal = 0; terminal < old_terminals.size(); ++terminal) {
        if (!kept_terminals.contains(terminal)) {
            moves.Removed.push_back(old_terminals[terminal]);
        }
    }
}

} // namespace

/**
 * @brief   Checks that the pins and parameters of a block each have a name of their own.
 * @param[in] pins        Pins of the block.
 * @param[in] parameters  Parameters of the block.
 * @return  Nothing when every name is an identifier no other of its kind repeats; the reason otherwise.
 */
std::expected<void, std::string> ValidateMetadata(const std::vector<SavedSubcircuitPin> &pins,
                                                  const std::vector<Core::SubcircuitParameter> &parameters) {
    std::set<std::string> names;
    for (const SavedSubcircuitPin &pin : pins) {
        if (!Core::SubcircuitInstance::IsValidIdentifier(pin.Name) || !names.insert(pin.Name).second) {
            return std::unexpected("Subcircuit pin names must be unique identifiers");
        }
    }
    names.clear();
    for (const Core::SubcircuitParameter &parameter : parameters) {
        if (!Core::SubcircuitInstance::IsValidIdentifier(parameter.Name) || !names.insert(parameter.Name).second) {
            return std::unexpected("Subcircuit parameter names must be unique identifiers");
        }
    }
    return {};
}

/**
 * @brief   Checks that a parameter a part of the block follows cannot go away with the part still following it.
 * @param[in] elements    Parts of the block.
 * @param[in] parameters  Parameters the block will have.
 * @return  Nothing when every followed parameter is listed; the reason otherwise.
 */
std::expected<void, std::string> ValidateExpressions(const std::vector<std::unique_ptr<UIElement>> &elements,
                                                     const std::vector<Core::SubcircuitParameter> &parameters) {
    for (const auto &element : elements) {
        const Core::Component &component = element->GetComponent();
        const std::optional<std::string> &expression = component.GetValueExpression();
        if (expression &&
            !std::ranges::contains(parameters, Core::SubcircuitInstance::GetExpressionParameter(*expression),
                                   &Core::SubcircuitParameter::Name)) {
            return std::unexpected(
                std::format("{} follows {}, which is not a parameter of the block", component.GetName(), *expression));
        }
    }
    return {};
}

/**
 * @brief   Checks that each marker offered is one pin, whatever order the pins were put in.
 * @param[in] candidates  Markers that can become pins.
 * @param[in] pins        Pins chosen for the block.
 * @return  Nothing when every marker has exactly one pin; the reason otherwise.
 */
std::expected<void, std::string> CheckPinChoice(const std::vector<SubcircuitPinCandidate> &candidates,
                                                const std::vector<SavedSubcircuitPin> &pins) {
    std::set<GridPoint> pin_points;
    for (const SavedSubcircuitPin &pin : pins) {
        if (!std::ranges::contains(candidates, pin.Point, &SubcircuitPinCandidate::Point) ||
            !pin_points.insert(pin.Point).second) {
            return std::unexpected("Choose one name for every pin marker");
        }
    }
    if (pins.size() != candidates.size()) {
        return std::unexpected("Choose one name for every pin marker");
    }
    return {};
}

/**
 * @brief   Finds the pin markers among some parts, with the name each suggests for its pin.
 * @param[in] elements  Parts to look in.
 * @param[in] wires     Wires among them, which join the markers to nodes.
 * @param[in] selected  Indices of the parts to consider, or null for all of them.
 * @return  The markers in the order of the parts; the reason when two give one name or share a node.
 */
std::expected<std::vector<SubcircuitPinCandidate>, std::string>
CollectSubcircuitPinMarkers(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
                            const std::set<std::size_t> *selected) {
    const Connectivity connectivity(elements, wires);
    std::vector<SubcircuitPinCandidate> candidates;
    std::set<std::string> names;
    std::set<int> nodes;
    int unnamed = 0;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        if (selected != nullptr && !selected->contains(index)) {
            continue;
        }
        const Core::Component &component = elements[index]->GetComponent();
        if (!Core::IsSubcircuitPin(component.GetType())) {
            continue;
        }
        const GridPoint point = elements[index]->GetTerminals().front();
        const std::string &marker_name = static_cast<const Core::NetLabel &>(component).GetNet();
        std::string suggested = marker_name;
        if (suggested.empty()) {
            suggested = std::format("P{}", ++unnamed);
        }
        if (!Core::SubcircuitInstance::IsValidIdentifier(suggested) || !names.insert(suggested).second) {
            return std::unexpected("Subcircuit pin names must be unique identifiers");
        }
        const std::optional<int> node = connectivity.GetNode(point);
        if (node && !nodes.insert(*node).second) {
            return std::unexpected("Place only one pin marker on each subcircuit pin node");
        }
        candidates.push_back({point, std::move(suggested)});
    }
    return candidates;
}

/**
 * @brief   Finds the old pin each new pin continues.
 * @details By the point of its marker, so a renamed pin keeps its wires, or else by name, so a marker moved inside the
 *          block keeps them too.
 * @param[in] old_pins  Pins of the definition as it was.
 * @param[in] new_pins  Pins it will have.
 * @return  For each new pin, the index of the old one it continues, or no value for a pin that is new.
 */
std::vector<std::optional<std::size_t>> MatchPins(const std::vector<SavedSubcircuitPin> &old_pins,
                                                  const std::vector<SavedSubcircuitPin> &new_pins) {
    std::vector<std::optional<std::size_t>> matches(new_pins.size());
    std::set<std::size_t> taken;
    const auto match = [&](const auto &is_same) {
        for (std::size_t new_index = 0; new_index < new_pins.size(); ++new_index) {
            for (std::size_t old_index = 0; old_index < old_pins.size() && !matches[new_index]; ++old_index) {
                if (!taken.contains(old_index) && is_same(old_pins[old_index], new_pins[new_index])) {
                    matches[new_index] = old_index;
                    taken.insert(old_index);
                }
            }
        }
    };
    match([](const SavedSubcircuitPin &old_pin, const SavedSubcircuitPin &new_pin) {
        return old_pin.Point == new_pin.Point;
    });
    match([](const SavedSubcircuitPin &old_pin, const SavedSubcircuitPin &new_pin) {
        return old_pin.Name == new_pin.Name;
    });
    return matches;
}

/**
 * @brief   Draws the contents of a new block: a copy of the selection, with each marker named after its pin.
 * @details The pins must reach nodes the outside can connect to.
 * @param[in] elements           Parts of the schematic.
 * @param[in] wires              Wires of the schematic.
 * @param[in] selected_elements  Indices of the parts to copy.
 * @param[in] selected_wires     Indices of the wires to copy.
 * @param[in] definitions        Blocks the schematic has, which the copied instances use.
 * @param[in] definition         The block with its name, pins and parameters set, which receives the contents.
 * @return  The block with its parts and wires; the reason when its pins cannot connect.
 */
std::expected<SavedSubcircuit, std::string>
DrawDefinition(const std::vector<std::unique_ptr<UIElement>> &elements, const std::vector<UIWire> &wires,
               const std::set<std::size_t> &selected_elements, const std::set<std::size_t> &selected_wires,
               const std::vector<SavedSubcircuit> &definitions, SavedSubcircuit definition) {
    std::vector<const UIElement *> copied_elements;
    for (const std::size_t index : selected_elements) {
        copied_elements.push_back(elements[index].get());
    }
    std::vector<UIWire> copied_wires;
    for (const std::size_t index : selected_wires) {
        copied_wires.push_back(wires[index]);
    }
    // The blocks the selection places go along, so their instances read back
    auto copied = LoadSchematic(SaveParts(copied_elements, copied_wires, definitions));
    if (!copied) {
        return std::unexpected(copied.error());
    }
    definition.Elements = std::move(copied->Elements);
    definition.Wires = std::move(copied->Wires);
    for (auto &element : definition.Elements) {
        if (!Core::IsSubcircuitPin(element->GetComponent().GetType())) {
            continue;
        }
        const GridPoint point = element->GetTerminals().front();
        const auto pin = std::ranges::find(definition.Pins, point, &SavedSubcircuitPin::Point);
        static_cast<Core::NetLabel &>(element->GetComponent()).SetNet(pin->Name);
    }
    if (const std::optional<std::string> problem =
            CheckPinNodes(Connectivity(definition.Elements, definition.Wires), definition.Pins)) {
        return std::unexpected(*problem);
    }
    return definition;
}

/**
 * @brief   Writes a definition as text, so two definitions of one name can be told equal or not.
 * @param[in] definition  The definition to describe.
 * @return  The text of a file that holds only that definition.
 */
std::string DescribeDefinition(const SavedSubcircuit &definition) {
    return SaveSchematic({}, {}, {}, {}, std::span<const SavedSubcircuit>(&definition, 1));
}

/**
 * @brief   Copies every definition into ones that share nothing with them, through the text of a file.
 * @param[in] definitions  The definitions to copy.
 * @return  The copies, in the same order; none when the text cannot be read back.
 */
std::vector<SavedSubcircuit> CopyDefinitions(const std::vector<SavedSubcircuit> &definitions) {
    auto loaded = LoadSchematic(SaveSchematic({}, {}, {}, {}, definitions));
    return loaded ? std::move(loaded->Subcircuits) : std::vector<SavedSubcircuit>{};
}

/**
 * @brief   Finds a name no definition has yet, from the one a pasted definition came with.
 * @param[in] name         The name the pasted definition came with.
 * @param[in] definitions  The definitions already here.
 * @return  The name with a suffix such as "_2", the first that is free.
 */
std::string FindFreeName(const std::string &name, const std::vector<SavedSubcircuit> &definitions) {
    for (int suffix = 2;; ++suffix) {
        std::string candidate = std::format("{}_{}", name, suffix);
        if (!std::ranges::contains(definitions, candidate, &SavedSubcircuit::Name)) {
            return candidate;
        }
    }
}

/**
 * @brief   Brings the instances of a definition that changed up to date among some parts: their pins and edges, the
 *          overrides of the parameters it still has, and the wires on their terminals, which follow the pins that go
 *          on and are removed from the pins that went away.
 * @param[in,out] elements    Parts among which the instances are.
 * @param[in,out] wires       Wires among them, which follow the terminals.
 * @param[in]     definition  The definition as it is now.
 * @param[in]     old_names   Names of its pins as they were.
 * @param[in]     matches     For each new pin, the index in old_names of the old pin it continues, if any.
 * @details A pin that moves to another edge gets a new wire around the box from where its wires leave the box, since
 *          stretching them could run them along it. They are stretched as before when none leaves the box, and the new
 *          wire bends once when no route is found.
 */
void RefreshInstancesIn(std::vector<std::unique_ptr<UIElement>> &elements, std::vector<UIWire> &wires,
                        const SavedSubcircuit &definition, const std::vector<std::string> &old_names,
                        const std::vector<std::optional<std::size_t>> &matches) {
    TerminalMoves moves;
    for (auto &element : elements) {
        if (element->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT &&
            static_cast<const Core::SubcircuitInstance &>(element->GetComponent()).GetDefinitionName() ==
                definition.Name) {
            RefreshInstance(*element, definition, old_names, matches, moves);
        }
    }
    std::erase_if(wires, [&](const UIWire &wire) {
        return std::ranges::contains(moves.Removed, wire.GetStart()) ||
               std::ranges::contains(moves.Removed, wire.GetEnd());
    });
    wires = FollowTerminals(wires, moves.OldPoints, moves.NewPoints);
    if (moves.Rerouted.empty()) {
        return;
    }
    std::vector<WireLink> links;
    for (const ReroutedPin &pin : moves.Rerouted) {
        if (const std::optional<GridPoint> start = DetachFromBox(wires, pin.OldPoint, *pin.Block)) {
            links.push_back({.From = *start, .To = pin.NewPoint});
        } else {
            wires = FollowTerminals(wires, {pin.OldPoint}, {pin.NewPoint});
        }
    }
    const auto routed = RouteLinks(elements, wires, moves.Blocks, links);
    for (std::size_t index = 0; index < links.size(); ++index) {
        if (const std::optional<std::vector<UIWire>> &path = routed[index]) {
            std::ranges::copy(*path, std::back_inserter(wires));
        } else {
            const GridPoint bend = {links[index].To.X, links[index].From.Y};
            wires.emplace_back(links[index].From, bend);
            wires.emplace_back(bend, links[index].To);
        }
    }
}

} // namespace GUI
