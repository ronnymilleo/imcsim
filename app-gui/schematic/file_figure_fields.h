/**
 * @file    file_figure_fields.h
 * @brief   Writes and reads the figures of the parts that are all figures in the schematic file.
 * @details The switch that follows a voltage, the relay, the fuse, the transformer, the gates and flip-flops with their
 *          logic family, a clock and the logic and display parts. Only the file modules include this header, since
 *          nlohmann/json is private to the library.
 */

#ifndef IMCSIM_FILE_FIGURE_FIELDS_H
#define IMCSIM_FILE_FIGURE_FIELDS_H

#include "circuit/components/component.h"
#include "nlohmann/json_fwd.hpp"
#include <expected>
#include <string>

namespace GUI {

// Figures
void WriteFigureFields(const Core::Component &component, nlohmann::json &object);
[[nodiscard]] std::expected<void, std::string> ReadFigureFields(const nlohmann::json &object,
                                                                Core::Component &component);

} // namespace GUI

#endif // IMCSIM_FILE_FIGURE_FIELDS_H
