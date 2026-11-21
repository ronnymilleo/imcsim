/**
 * @file    touchstone_export_control.cpp
 * @brief   The button of the S-parameters tab that saves the whole matrix as a Touchstone file, with its options.
 */

#include "windows/output_tabs/touchstone_export_control.h"

#include "base/theme.h"
#include "imgui.h"
#include <expected>

namespace GUI {

namespace {

/// Width of the combos of the Touchstone export, in font sizes.
constexpr float COMBO_WIDTH = 6.0f;
constexpr const char *TOUCHSTONE_POPUP = "touchstone_options";

} // namespace

/**
 * @brief   The frequency unit and number format of the Touchstone file, and the button that saves it.
 * @details The file holds the whole matrix, not only the checked entries.
 * @param[in]     result          The result whose matrix is saved.
 * @param[in,out] exporter        Asks where to save the file and writes it.
 * @param[in]     schematic_path  File of the schematic, which names the file, or no value for an unsaved schematic.
 */
void TouchstoneExportControl::Draw(const Core::SParameters &result,
                                   PlotExporter &exporter,
                                   const std::optional<std::filesystem::path> &schematic_path) {
    if (ImGui::Button("Save Touchstone...")) {
        ImGui::OpenPopup(TOUCHSTONE_POPUP);
    }
    ImGui::SetItemTooltip("Saves the whole matrix as a Touchstone .sNp file; a two-port lists S11 S21 S12 S22");
    if (ImGui::BeginPopup(TOUCHSTONE_POPUP)) {
        DrawOptions();
        if (PrimaryButton("Save...")) {
            Save(result, exporter, schematic_path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!m_ExportError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(GetErrorTextColor(), "%s", m_ExportError.c_str());
        ImGui::PopTextWrapPos();
    }
}

/**
 * @brief   The combos of the frequency unit and the number format, inside the popup.
 */
void TouchstoneExportControl::DrawOptions() {
    ImGui::TextUnformatted("Touchstone file of the whole matrix");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * COMBO_WIDTH);
    if (ImGui::BeginCombo("Frequency unit", GetTouchstoneUnitName(m_Options.Unit))) {
        for (const TouchstoneFrequencyUnit unit : TOUCHSTONE_UNITS) {
            if (ImGui::Selectable(GetTouchstoneUnitName(unit), unit == m_Options.Unit)) {
                m_Options.Unit = unit;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * COMBO_WIDTH);
    if (ImGui::BeginCombo("Format", GetTouchstoneFormatName(m_Options.Format))) {
        for (const TouchstoneFormat format : TOUCHSTONE_FORMATS) {
            if (ImGui::Selectable(GetTouchstoneFormatName(format), format == m_Options.Format)) {
                m_Options.Format = format;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("RI: real and imaginary parts. MA: magnitude and angle in degrees. DB: decibels and "
                          "angle in degrees");
}

/**
 * @brief   Writes the file in the unit and format picked and asks where to save it; a matrix that cannot be written
 *          leaves the reason under the button.
 */
void TouchstoneExportControl::Save(const Core::SParameters &result,
                                   PlotExporter &exporter,
                                   const std::optional<std::filesystem::path> &schematic_path) {
    const std::expected<std::string, std::string> text = FormatTouchstone(result, m_Options);
    if (text) {
        m_ExportError.clear();
        exporter.StartTouchstone(*text, result.GetPortCount(), schematic_path, "s-parameters");
    } else {
        m_ExportError = text.error();
    }
}

} // namespace GUI
