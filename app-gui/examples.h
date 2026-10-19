/**
 * @file    examples.h
 * @brief   Ready circuits, with their analyses set up, that the user can open to learn from.
 */

#ifndef IMCSIM_EXAMPLES_H
#define IMCSIM_EXAMPLES_H

#include "schematic_file.h"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  Example
 * @brief   One example circuit: its name and submenu, what it shows and the file it is read from.
 * @details Files live in app-gui/examples and are compiled into the application. Examples of the same group sit
 *          together in the list, in the order of the submenu.
 */
struct Example {
    const char *Title;
    const char *Group;
    const char *Description;
    std::string_view FileName;
};

std::span<const Example> GetExamples();
std::vector<const char *> GetExampleGroups();
std::expected<LoadedSchematic, std::string> LoadExample(const Example &example);

} // namespace GUI

#endif // IMCSIM_EXAMPLES_H
