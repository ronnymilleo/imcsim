/**
 * @file    ai_copy.h
 * @brief   Writes the circuit, the settings of the selected analysis and a summary of its result as one text to paste
 *          into a conversation with an AI assistant.
 * @details Pure text building, so the Edit menu and the Output window only put it on the clipboard.
 */

#ifndef IMCSIM_AI_COPY_H
#define IMCSIM_AI_COPY_H

#include "results/result_store.h"
#include "schematic/schematic.h"
#include <string>
#include <string_view>

namespace GUI {

// Document
std::string BuildAICopyText(Schematic &schematic, const ResultStore &results, std::string_view engine_name);

} // namespace GUI

#endif // IMCSIM_AI_COPY_H
