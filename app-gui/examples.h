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

namespace GUI {

/**
 * @struct  Example
 * @brief   One example circuit: its name in the menu, what it shows and the file it is read from.
 * @details Files live in app-gui/examples and are compiled into the application.
 */
struct Example {
    const char *Title;
    const char *Description;
    std::string_view FileName;
};

std::span<const Example> GetExamples();
std::expected<LoadedSchematic, std::string> LoadExample(const Example &example);

} // namespace GUI

#endif // IMCSIM_EXAMPLES_H
