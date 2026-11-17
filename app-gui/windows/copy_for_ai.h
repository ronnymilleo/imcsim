/**
 * @file    copy_for_ai.h
 * @brief   Puts the circuit, the settings of the selected analysis and a summary of its result on the clipboard as
 *          text for an AI assistant.
 */

#ifndef IMCSIM_COPY_FOR_AI_H
#define IMCSIM_COPY_FOR_AI_H

#include "results/result_store.h"
#include "schematic/schematic.h"
#include <string_view>

namespace GUI {

/// What the status bar says after the copy.
constexpr std::string_view COPIED_FOR_AI_NOTICE = "Copied the circuit and its results for an AI";
/// Seconds the notice stays on screen.
constexpr double COPY_NOTICE_SECONDS = 3.0;

// Clipboard
void CopyForAI(Schematic &schematic, const ResultStore &results);

} // namespace GUI

#endif // IMCSIM_COPY_FOR_AI_H
