/**
 * @file    plot_exporter.cpp
 * @brief   Export of a plot of the Output window: its popup, its save dialog and the file it writes.
 */

#include "plot_exporter.h"

#include "imgui.h"
#include "schematic_file.h"
#include "theme.h"
#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *EXPORT_POPUP = "export";
constexpr const char *EXPORT_ERROR_POPUP = "Could not export the plot";
// Width of the size fields of the export popup, in font sizes
constexpr float EXPORT_SIZE_WIDTH = 7.0f;
// Must stay valid until the dialog callback runs, so they live for the whole program
constexpr std::array<SDL_DialogFileFilter, 1> SVG_FILTERS = {{{"SVG image", "svg"}}};
constexpr std::array<SDL_DialogFileFilter, 1> CSV_FILTERS = {{{"CSV table", "csv"}}};

std::string GetExtension(const ExportFormat format) {
    return format == ExportFormat::SVG ? ".svg" : ".csv";
}

} // namespace

/**
 * @brief   Opens the export popup.
 * @note    Call it where DrawPopup() is called, so ImGui finds the popup in the same ID stack.
 */
void PlotExporter::OpenPopup() {
    ImGui::OpenPopup(EXPORT_POPUP);
}

/**
 * @brief   Draws the export popup while it is open: like the export dialog of a scope, the image follows the plot as
 *          shown, at the size and theme chosen here.
 * @param[in] plot_name   Name of the plot, such as "AC sweep", for the title of the popup.
 * @param[in] can_export  Whether the plot has something to export; the save buttons are disabled otherwise.
 * @return  The format to save once the user picks one.
 */
std::optional<ExportFormat> PlotExporter::DrawPopup(const std::string &plot_name, const bool can_export) {
    if (!ImGui::BeginPopup(EXPORT_POPUP)) {
        return std::nullopt;
    }
    ImGui::TextUnformatted(std::format("Export the {} plot", plot_name).c_str());
    ImGui::TextDisabled("The measured traces: the image shows the visible range, the table every sample");
    ImGui::Separator();
    if (ImGui::RadioButton("Light, for print", !m_Style.Dark)) {
        m_Style.Dark = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Dark", m_Style.Dark)) {
        m_Style.Dark = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * EXPORT_SIZE_WIDTH);
    ImGui::InputInt("##width", &m_Style.Width, 100, 500);
    ImGui::SameLine();
    ImGui::TextUnformatted("x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * EXPORT_SIZE_WIDTH);
    ImGui::InputInt("pixels##height", &m_Style.Height, 100, 500);
    m_Style.Width = std::clamp(m_Style.Width, MIN_EXPORT_SIZE, MAX_EXPORT_SIZE);
    m_Style.Height = std::clamp(m_Style.Height, MIN_EXPORT_SIZE, MAX_EXPORT_SIZE);
    ImGui::Separator();

    std::optional<ExportFormat> format;
    if (!can_export) {
        ImGui::TextDisabled("Run the analysis and measure a trace to export it");
    }
    ImGui::BeginDisabled(!can_export || m_Dialog.IsPending());
    if (PrimaryButton("Save SVG...")) {
        format = ExportFormat::SVG;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save CSV...")) {
        format = ExportFormat::CSV;
    }
    ImGui::EndDisabled();
    if (format) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return format;
}

/**
 * @brief   Keeps the figure and shows the save dialog, which starts next to the schematic, with a name after it and
 *          the plot. Nothing happens while another dialog is open.
 * @param[in] figure          The plot as shown now.
 * @param[in] format          Format the user picked.
 * @param[in] schematic_path  File of the schematic, if it has one.
 * @param[in] file_suffix     Names the plot in the file name, such as "ac-sweep".
 */
void PlotExporter::Start(ExportFigure figure, const ExportFormat format,
                         const std::optional<std::filesystem::path> &schematic_path, const std::string &file_suffix) {
    if (m_Dialog.IsPending()) {
        return;
    }
    m_PendingFigure = std::move(figure);
    m_PendingFormat = format;
    const std::string stem = schematic_path ? schematic_path->stem().string() : "circuit";
    const std::string file_name = std::format("{}-{}{}", stem, file_suffix, GetExtension(format));
    const std::filesystem::path location =
        schematic_path ? schematic_path->parent_path() / file_name : std::filesystem::path(file_name);
    if (format == ExportFormat::SVG) {
        m_Dialog.ShowSave(SVG_FILTERS, location.string());
    } else {
        m_Dialog.ShowSave(CSV_FILTERS, location.string());
    }
}

/**
 * @brief   Writes the figure once the dialog answers, adding the extension when the dialog left it out, and opens the
 *          error popup when it cannot.
 * @note    Call it every frame, where DrawError() is called.
 */
void PlotExporter::Update() {
    const std::optional<std::optional<std::filesystem::path>> result = m_Dialog.TakeResult();
    if (!result) {
        return;
    }
    const std::optional<ExportFigure> figure = std::exchange(m_PendingFigure, std::nullopt);
    if (!*result || !figure) {
        return;
    }
    std::filesystem::path path = **result;
    const std::string extension = GetExtension(m_PendingFormat);
    if (path.extension() != extension) {
        path += extension;
    }
    std::expected<std::string, std::string> text =
        m_PendingFormat == ExportFormat::SVG ? RenderSvg(*figure, m_Style) : RenderCsv(*figure);
    std::expected<void, std::string> written =
        text ? WriteTextFile(path, *text) : std::expected<void, std::string>(std::unexpected(text.error()));
    if (!written) {
        m_Error = written.error();
        ImGui::OpenPopup(EXPORT_ERROR_POPUP);
    }
}

/**
 * @brief   Draws the popup that tells why the last export failed, while it is open.
 */
void PlotExporter::DrawError() {
    if (!ImGui::BeginPopupModal(EXPORT_ERROR_POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextColored(GetErrorTextColor(), "%s", m_Error.c_str());
    if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace GUI
