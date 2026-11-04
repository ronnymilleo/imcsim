/**
 * @file    block_nesting.cpp
 * @brief   Blocks inside blocks: which blocks some parts use, whether a block contains another, and the order that
 *          puts every block after the blocks it uses.
 */

#include "document/block_nesting.h"

#include "components/subcircuit.h"
#include <algorithm>
#include <functional>
#include <optional>

namespace GUI {

namespace {

/**
 * @brief   The position of a definition in the list, or none for a name the list does not have.
 */
std::optional<std::size_t> FindDefinition(const std::span<const SavedSubcircuit> definitions,
                                          const std::string_view name) {
    const auto found = std::ranges::find(definitions, name, &SavedSubcircuit::Name);
    if (found == definitions.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - definitions.begin());
}

/**
 * @brief   Whether a block, or a block it uses on the way down, is the one named; each block is looked into once, so a
 *          cycle in a damaged file ends the walk instead of looping.
 */
bool ReachesBlock(const std::span<const SavedSubcircuit> definitions, const std::string_view from,
                  const std::string_view name, std::set<std::string, std::less<>> &visited) {
    if (from == name) {
        return true;
    }
    if (!visited.insert(std::string(from)).second) {
        return false;
    }
    const std::optional<std::size_t> index = FindDefinition(definitions, from);
    if (!index) {
        return false;
    }
    return std::ranges::any_of(ListUsedBlocks(definitions[*index].Elements),
                               [&](const std::string &used) { return ReachesBlock(definitions, used, name, visited); });
}

} // namespace

/**
 * @brief   Lists the blocks some parts place directly.
 * @param[in] elements  Parts of a schematic or of a block.
 * @return  The names of the definitions their block instances use, each once.
 */
std::set<std::string> ListUsedBlocks(const std::vector<std::unique_ptr<UIElement>> &elements) {
    std::set<std::string> used;
    for (const auto &element : elements) {
        if (element->GetComponent().GetType() == Core::ComponentType::SUBCIRCUIT) {
            used.insert(static_cast<const Core::SubcircuitInstance &>(element->GetComponent()).GetDefinitionName());
        }
    }
    return used;
}

/**
 * @brief   Tells whether some parts use a block, directly or through the blocks they place.
 * @param[in] elements     Parts of a schematic or of a block.
 * @param[in] definitions  Every block definition, where the blocks they place are looked up.
 * @param[in] name         The block to look for.
 * @return  True when an instance among the parts is that block, or a block that contains it at any depth.
 */
bool UsesBlock(const std::vector<std::unique_ptr<UIElement>> &elements,
               const std::span<const SavedSubcircuit> definitions, const std::string_view name) {
    std::set<std::string, std::less<>> visited;
    return std::ranges::any_of(ListUsedBlocks(elements),
                               [&](const std::string &used) { return ReachesBlock(definitions, used, name, visited); });
}

/**
 * @brief   Tells whether a block contains another, at any depth, or is that block; placing the outer one inside the
 *          inner one would then make a block that contains itself.
 * @param[in] definitions  Every block definition.
 * @param[in] outer        The block that may contain the other.
 * @param[in] inner        The block looked for.
 * @return  True when outer is inner, or one of its parts, or a part of a block it places, is inner.
 */
bool ContainsBlock(const std::span<const SavedSubcircuit> definitions, const std::string_view outer,
                   const std::string_view inner) {
    std::set<std::string, std::less<>> visited;
    return ReachesBlock(definitions, outer, inner, visited);
}

/**
 * @brief   Orders the definitions so each one comes after every block it uses, as a netlist or a merge reads them.
 * @param[in] definitions  Every block definition.
 * @return  Their positions in that order; among blocks that do not depend on each other, the order of the list stays.
 *          A block that uses one it contains, which only a damaged file can hold, keeps its place after the others.
 */
std::vector<std::size_t> OrderByUse(const std::span<const SavedSubcircuit> definitions) {
    std::vector<std::size_t> order;
    std::vector<bool> is_placed(definitions.size(), false);
    std::vector<bool> is_open(definitions.size(), false);
    const std::function<void(std::size_t)> place = [&](const std::size_t index) {
        if (is_placed[index] || is_open[index]) {
            return;
        }
        is_open[index] = true;
        for (const std::string &used : ListUsedBlocks(definitions[index].Elements)) {
            if (const std::optional<std::size_t> inner = FindDefinition(definitions, used)) {
                place(*inner);
            }
        }
        is_open[index] = false;
        is_placed[index] = true;
        order.push_back(index);
    };
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        place(index);
    }
    return order;
}

/**
 * @brief   Points the instances of a block at its new name.
 * @param[in,out] elements  Parts of a schematic or of a block.
 * @param[in]     old_name  The name the block had.
 * @param[in]     new_name  The name it has now.
 */
void RenameBlockInstances(std::vector<std::unique_ptr<UIElement>> &elements, const std::string_view old_name,
                          const std::string &new_name) {
    for (auto &element : elements) {
        if (element->GetComponent().GetType() != Core::ComponentType::SUBCIRCUIT) {
            continue;
        }
        auto &instance = static_cast<Core::SubcircuitInstance &>(element->GetComponent());
        if (instance.GetDefinitionName() == old_name) {
            instance.SetDefinitionName(new_name);
        }
    }
}

} // namespace GUI
