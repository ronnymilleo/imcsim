/**
 * @file    block_nesting.h
 * @brief   Blocks inside blocks: which blocks some parts use, whether a block contains another, and the order that
 *          puts every block after the blocks it uses.
 */

#ifndef IMCSIM_BLOCK_NESTING_H
#define IMCSIM_BLOCK_NESTING_H

#include "document/schematic_file.h"
#include "ui_elements/ui_element.h"
#include <cstddef>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

// Uses
std::set<std::string> ListUsedBlocks(const std::vector<std::unique_ptr<UIElement>> &elements);
bool UsesBlock(const std::vector<std::unique_ptr<UIElement>> &elements, std::span<const SavedSubcircuit> definitions,
               std::string_view name);
bool ContainsBlock(std::span<const SavedSubcircuit> definitions, std::string_view outer, std::string_view inner);

// Order
std::vector<std::size_t> OrderByUse(std::span<const SavedSubcircuit> definitions);

// Renaming
void RenameBlockInstances(std::vector<std::unique_ptr<UIElement>> &elements, std::string_view old_name,
                          const std::string &new_name);

} // namespace GUI

#endif // IMCSIM_BLOCK_NESTING_H
