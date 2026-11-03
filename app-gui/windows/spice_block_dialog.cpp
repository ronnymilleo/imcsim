/**
 * @file    spice_block_dialog.cpp
 * @brief   The dialogs of blocks kept as SPICE text: picking what to import from a library, and their pin settings.
 */

#include "spice_block_dialog.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "spice_import.h"
#include "spice_value.h"
#include "subcircuit_pins.h"
#include "theme.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *IMPORT_POPUP = "Import SPICE library";
constexpr const char *SETTINGS_POPUP = "SPICE block";
// Widths in font sizes, so the dialogs follow the font
constexpr float PORT_COLUMN_WIDTH = 6.0f;
constexpr float NAME_FIELD_WIDTH = 8.0f;
constexpr float SIDE_FIELD_WIDTH = 6.0f;
constexpr float LIST_HEIGHT = 24.0f;
constexpr float TEXT_WIDTH = 36.0f;
constexpr float TEXT_HEIGHT = 12.0f;

// One row per port: the port as the text names it, the pin name and the edge of the box it goes on
void DrawPinTable(std::vector<SavedSubcircuitPin> &pins, const std::vector<std::string> &ports) {
    const float font_size = ImGui::GetFontSize();
    for (std::size_t index = 0; index < pins.size() && index < ports.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ports[index].c_str());
        ImGui::SameLine(font_size * PORT_COLUMN_WIDTH);
        ImGui::SetNextItemWidth(font_size * NAME_FIELD_WIDTH);
        ImGui::InputText("##name", &pins[index].Name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(font_size * SIDE_FIELD_WIDTH);
        if (ImGui::BeginCombo("##side", GetPinSideName(pins[index].Side))) {
            for (const Core::PinSide side : PIN_SIDES) {
                if (ImGui::Selectable(GetPinSideName(side), side == pins[index].Side)) {
                    pins[index].Side = side;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

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
 * @brief   Opens the dialog on the subcircuits of a library, to pick which to import and set up their pins.
 * @param[in] file_name  Name of the library file, shown in the dialog.
 * @param[in] library    What ReadSpiceLibrary() read from it.
 * @note    A library of one subcircuit has it picked already.
 */
void SpiceBlockDialog::OpenImport(std::string file_name, Core::SpiceLibrary library) {
    m_Mode = Mode::IMPORT;
    m_FileName = std::move(file_name);
    m_Warnings = std::move(library.Warnings);
    m_Choices.clear();
    const bool is_single = library.Subcircuits.size() == 1;
    for (Core::LibrarySubcircuit &subcircuit : library.Subcircuits) {
        std::vector<SavedSubcircuitPin> pins = SuggestSpicePins(subcircuit);
        m_Choices.push_back({.Subcircuit = std::move(subcircuit), .IsChosen = is_single, .Pins = std::move(pins)});
    }
    m_Error.clear();
    m_IsOpenRequested = true;
}

/**
 * @brief   Opens the dialog on the pins and parameters of a block kept as SPICE text.
 * @param[in] definition  The block; one that is not SPICE text opens nothing.
 */
void SpiceBlockDialog::OpenSettings(const SavedSubcircuit &definition) {
    if (!definition.Spice) {
        return;
    }
    m_Mode = Mode::SETTINGS;
    m_BlockName = definition.Name;
    m_Ports = definition.Spice->Ports;
    m_Pins = definition.Pins;
    m_Parameters = definition.Parameters;
    m_Text = definition.Spice->Text;
    m_Error.clear();
    m_IsOpenRequested = true;
}

/**
 * @brief   Draws the open dialog, if any.
 * @return  What the user confirmed, in the frame they confirm it; no value otherwise.
 * @note    Call it every frame from the same place, outside any window, so the popup opens where it is drawn.
 */
std::optional<SpiceBlockResult> SpiceBlockDialog::Draw() {
    if (std::exchange(m_IsOpenRequested, false)) {
        ImGui::OpenPopup(m_Mode == Mode::IMPORT ? IMPORT_POPUP : SETTINGS_POPUP);
    }
    return m_Mode == Mode::IMPORT ? DrawImport() : DrawSettings();
}

// Every subcircuit of the library with a box to pick it; a picked one shows its pins to name and place
std::optional<SpiceBlockResult> SpiceBlockDialog::DrawImport() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(IMPORT_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return std::nullopt;
    }
    const float font_size = ImGui::GetFontSize();
    ImGui::TextUnformatted(m_FileName.c_str());
    for (const std::string &warning : m_Warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::TextDisabled(
        "Pick the subcircuits to import, then name each pin and choose the edge of the block it goes on");
    // As tall as what it lists, up to a limit past which it scrolls
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {FLT_MAX, font_size * LIST_HEIGHT});
    ImGui::BeginChild("##subcircuits", {0.0f, 0.0f}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    for (std::size_t index = 0; index < m_Choices.size(); ++index) {
        Choice &choice = m_Choices[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Checkbox(choice.Subcircuit.Name.c_str(), &choice.IsChosen);
        if (choice.IsChosen) {
            ImGui::Indent();
            // What the vendor says each port is, which numbered ports need to be named
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
            DrawPinTable(choice.Pins, choice.Subcircuit.Ports);
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!m_Error.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_Error.c_str());
    }

    std::optional<SpiceBlockResult> result;
    const bool is_any_chosen = std::ranges::any_of(m_Choices, &Choice::IsChosen);
    ImGui::BeginDisabled(!is_any_chosen);
    if (ImGui::Button("Import")) {
        SpiceBlockResult imported;
        for (const Choice &choice : m_Choices) {
            if (!choice.IsChosen) {
                continue;
            }
            if (const std::optional<std::string> problem = CheckSpicePins(choice.Pins)) {
                m_Error = std::format("{}: {}", choice.Subcircuit.Name, *problem);
                imported.Imported.clear();
                break;
            }
            imported.Imported.push_back(MakeSpiceBlock(choice.Subcircuit, choice.Pins));
        }
        if (!imported.Imported.empty()) {
            result = std::move(imported);
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

// The pins and parameter defaults of the block, and its text, which stays as it came
std::optional<SpiceBlockResult> SpiceBlockDialog::DrawSettings() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(SETTINGS_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return std::nullopt;
    }
    const float font_size = ImGui::GetFontSize();
    ImGui::TextUnformatted(std::format("{}, SPICE text from a library", m_BlockName).c_str());
    ImGui::SeparatorText("Pins: port, name and edge");
    DrawPinTable(m_Pins, m_Ports);
    if (!m_Parameters.empty()) {
        ImGui::SeparatorText("Parameter defaults");
        for (std::size_t index = 0; index < m_Parameters.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(font_size * NAME_FIELD_WIDTH);
            ImGui::InputDouble(m_Parameters[index].Name.c_str(), &m_Parameters[index].DefaultValue, 0.0, 0.0, "%g");
            ImGui::PopID();
        }
    }
    ImGui::SeparatorText("Text");
    ImGui::PushFont(GetMonospaceFont(), 0.0f);
    ImGui::InputTextMultiline("##text", &m_Text, {font_size * TEXT_WIDTH, font_size * TEXT_HEIGHT},
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
    if (!m_Error.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_Error.c_str());
    }

    std::optional<SpiceBlockResult> result;
    if (ImGui::Button("Apply")) {
        if (const std::optional<std::string> problem = CheckSpicePins(m_Pins)) {
            m_Error = *problem;
        } else {
            result = SpiceBlockResult{
                .Imported = {}, .UpdatedName = m_BlockName, .Pins = m_Pins, .Parameters = m_Parameters};
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return result;
}

} // namespace GUI
