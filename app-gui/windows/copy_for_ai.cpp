/**
 * @file    copy_for_ai.cpp
 * @brief   Puts the circuit, the settings of the selected analysis and a summary of its result on the clipboard as
 *          text for an AI assistant.
 */

#include "windows/copy_for_ai.h"

#include "imgui.h"
#include "plots/ai_copy.h"
#include "simulation/simulation_backend.h"
#include <string>

namespace GUI {

/**
 * @brief   Writes the text of BuildAICopyText() to the clipboard.
 * @param[in,out] schematic  The circuit to describe.
 * @param[in]     results    The results of its last runs.
 * @note    The engine named is the one in use now, which is the one the last run used unless the preference changed
 *          after it.
 */
void CopyForAI(Schematic &schematic, const ResultStore &results) {
    const std::string text = BuildAICopyText(schematic, results, Core::GetBackend().GetName());
    ImGui::SetClipboardText(text.c_str());
}

} // namespace GUI
