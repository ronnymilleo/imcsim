/**
 * @file    spice_import_dialog.cpp
 * @brief   The dialog that picks what to import from a SPICE library and sets up the pins of the blocks it becomes.
 */

#include "windows/spice_import_dialog.h"

#include "base/theme.h"
#include "imgui.h"
#include "schematic/spice_import.h"
#include "spice/spice_value.h"
#include "windows/spice_pin_table.h"
#include <algorithm>
#include <cfloat>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *POPUP = "Import SPICE library";
/// Tallest the list of subcircuits grows before it scrolls, in font sizes.
constexpr float LIST_HEIGHT = 24.0f;

std::string DescribeParameters(const std::vector<Core::SubcircuitParameter> &parameters) {
    std::string text;
    for (const Core::SubcircuitParameter &parameter : parameters) {
        text +=
            std::format("{}{}={}", text.empty() ? "" : ", ", parameter.Name, Core::FormatValue(parameter.DefaultValue));
    }
    return text;
}

} // namespace

/**
 * @brief   Opens the dialog on the subcircuits and device models of a library, to pick which to import and set up the
 *          pins of the subcircuits.
 * @param[in] file_name  Name of the library file, shown in the dialog.
 * @param[in] library    What ReadSpiceLibrary() read from it.
 * @note    A library of one subcircuit has it picked already.
 */
void SpiceImportDialog::Open(std::string file_name, Core::SpiceLibrary library) {
    m_FileName = std::move(file_name);
    m_Warnings = std::move(library.Warnings);
    m_Choices.clear();
    m_Models = std::move(library.Models);
    // A file of models only has them picked, as a single subcircuit is
    m_AreModelsChosen.assign(m_Models.size(), static_cast<char>(library.Subcircuits.empty()));
    const bool is_single = library.Subcircuits.size() == 1;
    for (Core::LibrarySubcircuit &subcircuit : library.Subcircuits) {
        std::vector<SavedSubcircuitPin> pins = SuggestSpicePins(subcircuit);
        m_Choices.push_back({.Subcircuit = std::move(subcircuit), .IsChosen = is_single, .Pins = std::move(pins)});
    }
    m_Error.clear();
    m_IsOpenRequested = true;
}

/**
 * @brief   Draws the dialog while it is open: every subcircuit of the library with a box to pick it, a picked one with
 *          its pins to name and place, and the device models.
 * @return  What the user picked, in the frame they confirm it; no value otherwise.
 * @note    Call it every frame from the same place, outside any window, so the popup opens where it is drawn.
 */
std::optional<SpiceImportResult> SpiceImportDialog::Draw() {
    if (std::exchange(m_IsOpenRequested, false)) {
        ImGui::OpenPopup(POPUP);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return std::nullopt;
    }
    const float font_size = ImGui::GetFontSize();
    ImGui::TextUnformatted(m_FileName.c_str());
    for (const std::string &warning : m_Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    if (!m_Choices.empty()) {
        ImGui::TextDisabled(
            "Pick the subcircuits to import, then name each pin and choose the edge of the block it goes on");
    }
    // As tall as what it lists, up to a limit past which it scrolls
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {FLT_MAX, font_size * LIST_HEIGHT});
    ImGui::BeginChild("##subcircuits", {0.0f, 0.0f}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    for (std::size_t index = 0; index < m_Choices.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        DrawSubcircuitChoice(m_Choices[index]);
        ImGui::PopID();
    }
    DrawModelChoices();
    ImGui::EndChild();
    if (!m_Error.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_Error.c_str());
    }

    std::optional<SpiceImportResult> result;
    const bool is_any_chosen = std::ranges::any_of(m_Choices, &Choice::IsChosen)
                            || std::ranges::any_of(m_AreModelsChosen, [](const char chosen) { return chosen != 0; });
    ImGui::BeginDisabled(!is_any_chosen);
    if (ImGui::Button("Import")) {
        result = TakeChosen();
        if (result) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}

/**
 * @brief   A subcircuit of the library with a box to pick it; once picked, what the vendor says of its ports, its
 *          parameters, its warnings and its pins to name and place.
 */
void SpiceImportDialog::DrawSubcircuitChoice(Choice &choice) {
    ImGui::Checkbox(choice.Subcircuit.Name.c_str(), &choice.IsChosen);
    if (!choice.IsChosen) {
        return;
    }
    ImGui::Indent();
    // In a fixed-width font, since vendors line their ports up with "|" columns
    if (!choice.Subcircuit.Description.empty()) {
        ImGui::PushFont(GetMonospaceFont(), 0.0f);
        ImGui::TextDisabled("%s", choice.Subcircuit.Description.c_str());
        ImGui::PopFont();
    }
    if (!choice.Subcircuit.Parameters.empty()) {
        ImGui::TextDisabled("Parameters: %s", DescribeParameters(choice.Subcircuit.Parameters).c_str());
    }
    for (const std::string &warning : choice.Subcircuit.Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    DrawSpicePinTable(choice.Pins, choice.Subcircuit.Ports);
    ImGui::Unindent();
}

/**
 * @brief   The device models of the library, each with a box to pick it.
 */
void SpiceImportDialog::DrawModelChoices() {
    if (m_Models.empty()) {
        return;
    }
    ImGui::SeparatorText("Device models");
    ImGui::TextDisabled("Diodes and transistors list the picked ones in their Model choice");
    for (std::size_t index = 0; index < m_Models.size(); ++index) {
        ImGui::PushID(static_cast<int>(m_Choices.size() + index));
        bool is_chosen = m_AreModelsChosen[index] != 0;
        const Core::LibraryModel &model = m_Models[index];
        if (ImGui::Checkbox(std::format("{} ({})", model.Name, Core::GetModelKindName(model.Kind)).c_str(),
                            &is_chosen)) {
            m_AreModelsChosen[index] = static_cast<char>(is_chosen);
        }
        ImGui::PopID();
    }
}

/**
 * @brief   The picked subcircuits as blocks and the picked models; no value when a pin name cannot be kept, which
 *          m_Error tells.
 */
std::optional<SpiceImportResult> SpiceImportDialog::TakeChosen() {
    SpiceImportResult imported;
    for (const Choice &choice : m_Choices) {
        if (!choice.IsChosen) {
            continue;
        }
        if (const std::optional<std::string> problem = CheckSpicePins(choice.Pins)) {
            m_Error = std::format("{}: {}", choice.Subcircuit.Name, *problem);
            return std::nullopt;
        }
        imported.Blocks.push_back(MakeSpiceBlock(choice.Subcircuit, choice.Pins));
    }
    for (std::size_t index = 0; index < m_Models.size(); ++index) {
        if (m_AreModelsChosen[index] != 0) {
            imported.Models.push_back(m_Models[index]);
        }
    }
    return imported;
}

} // namespace GUI
