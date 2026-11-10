/**
 * @file    plot_exporter.cpp
 * @brief   Export of a plot of the Output window: its popup, its save dialog and the file it writes.
 */

#include "plots/plot_exporter.h"

#include "document/schematic_file.h"
#include "drawing/theme.h"
#include "imgui.h"
#include "plots/touchstone.h"
#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <utility>

namespace GUI {

namespace {

constexpr const char *EXPORT_POPUP = "export";
constexpr const char *EXPORT_ERROR_POPUP = "Could not export the plot";
/// Width of the size fields of the export popup, in font sizes.
constexpr float EXPORT_SIZE_WIDTH = 7.0f;
// Must stay valid until the dialog callback runs, so they live for the whole program
constexpr std::array<SDL_DialogFileFilter, 1> SVG_FILTERS = {{{"SVG image", "svg"}}};
constexpr std::array<SDL_DialogFileFilter, 1> CSV_FILTERS = {{{"CSV table", "csv"}}};
/// Touchstone files take the number of ports in their extension, so the filter lists the usual ones.
constexpr std::array<SDL_DialogFileFilter, 1> TOUCHSTONE_FILTERS = {
    {{"Touchstone file", "s1p;s2p;s3p;s4p;s5p;s6p;s7p;s8p;s9p"}}};

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
 * @param[in] can_export      Whether the plot has something to export; the save buttons are disabled otherwise.
 * @param[in] has_longer_run  Whether the plot shows only a window of a longer run, whose every point a CSV table can
 *                            hold; the image always shows the window.
 * @return  What to save once the user picks it.
 */
std::optional<ExportChoice> PlotExporter::DrawPopup(const std::string &plot_name, const bool can_export,
                                                    const bool has_longer_run) {
    if (!ImGui::BeginPopup(EXPORT_POPUP)) {
        return std::nullopt;
    }
    ImGui::TextUnformatted(std::format("Export the {} plot", plot_name).c_str());
    ImGui::TextDisabled("The measured traces: the image shows the visible range, the table every sample%s",
                        has_longer_run ? " of the window, or of the whole run" : "");
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

    std::optional<ExportChoice> choice;
    if (!can_export) {
        ImGui::TextDisabled("Run the analysis and measure a trace to export it");
    }
    ImGui::BeginDisabled(!can_export || m_Dialog.IsPending());
    if (PrimaryButton("Save SVG...")) {
        choice = ExportChoice{.Format = ExportFormat::SVG, .IsWholeRun = false};
    }
    ImGui::SameLine();
    if (ImGui::Button(has_longer_run ? "Save CSV of the window..." : "Save CSV...")) {
        choice = ExportChoice{.Format = ExportFormat::CSV, .IsWholeRun = false};
    }
    if (has_longer_run) {
        ImGui::SameLine();
        if (ImGui::Button("Save CSV of the whole run...")) {
            choice = ExportChoice{.Format = ExportFormat::CSV, .IsWholeRun = true};
        }
    }
    ImGui::EndDisabled();
    if (choice) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return choice;
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
    m_PendingText.reset();
    m_PendingFormat = format;
    m_PendingExtension = GetExtension(format);
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
 * @brief   Asks where to save a CSV table that is not a plot, such as the runs of a Monte Carlo analysis.
 * @param[in] csv             The table, already written.
 * @param[in] schematic_path  File the schematic was saved to, which names the table and sets the folder; none for an
 *                            untitled one.
 * @param[in] file_suffix     Added to the schematic's name.
 */
void PlotExporter::StartText(std::string csv, const std::optional<std::filesystem::path> &schematic_path,
                             const std::string &file_suffix) {
    if (m_Dialog.IsPending()) {
        return;
    }
    m_PendingText = std::move(csv);
    m_PendingFormat = ExportFormat::CSV;
    m_PendingExtension = GetExtension(ExportFormat::CSV);
    const std::string stem = schematic_path ? schematic_path->stem().string() : "circuit";
    const std::string file_name = std::format("{}-{}.csv", stem, file_suffix);
    const std::filesystem::path location =
        schematic_path ? schematic_path->parent_path() / file_name : std::filesystem::path(file_name);
    m_Dialog.ShowSave(CSV_FILTERS, location.string());
}

/**
 * @brief   Asks where to save the Touchstone file of an S-parameter result.
 * @param[in] text            The file, already written.
 * @param[in] port_count      Ports of the result, which the extension counts (.s2p for two).
 * @param[in] schematic_path  File the schematic was saved to, which names the file and sets the folder; none for an
 *                            untitled one.
 * @param[in] file_suffix     Added to the schematic's name.
 */
void PlotExporter::StartTouchstone(std::string text, const int port_count,
                                   const std::optional<std::filesystem::path> &schematic_path,
                                   const std::string &file_suffix) {
    if (m_Dialog.IsPending()) {
        return;
    }
    m_PendingText = std::move(text);
    m_PendingFigure.reset();
    m_PendingExtension = GetTouchstoneExtension(port_count);
    const std::string stem = schematic_path ? schematic_path->stem().string() : "circuit";
    const std::string file_name = std::format("{}-{}{}", stem, file_suffix, m_PendingExtension);
    const std::filesystem::path location =
        schematic_path ? schematic_path->parent_path() / file_name : std::filesystem::path(file_name);
    m_Dialog.ShowSave(TOUCHSTONE_FILTERS, location.string());
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
    std::optional<std::string> pending_text = std::exchange(m_PendingText, std::nullopt);
    if (!*result || (!figure && !pending_text)) {
        return;
    }
    std::filesystem::path path = **result;
    if (path.extension() != m_PendingExtension) {
        path += m_PendingExtension;
    }
    std::expected<std::string, std::string> text = std::string();
    if (pending_text) {
        text = std::move(*pending_text);
    } else {
        text = m_PendingFormat == ExportFormat::SVG ? RenderSvg(*figure, m_Style) : RenderCsv(*figure);
    }
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
