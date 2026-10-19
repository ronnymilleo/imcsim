/**
 * @file    plot_exporter.h
 * @brief   Export of a plot of the Output window: its popup, its save dialog and the file it writes.
 */

#ifndef IMCSIM_PLOT_EXPORTER_H
#define IMCSIM_PLOT_EXPORTER_H

#include "file_dialog.h"
#include "plot_export.h"
#include <filesystem>
#include <optional>
#include <string>

namespace GUI {

/**
 * @enum    ExportFormat
 * @brief   File types a plot is exported to: an SVG image of the plot, or a CSV table of its samples.
 */
enum class ExportFormat {
    SVG,
    CSV
};

/**
 * @class   PlotExporter
 * @brief   Asks how to export a plot, captures its figure, and writes it once the user picks a file.
 * @details The figure is captured when the user asks, so the file holds what the plot showed then, whatever happens
 *          while the dialog is open. The dialog answers asynchronously; Update() picks its answer up on a later frame.
 */
class PlotExporter {
public:
    // Popup
    void OpenPopup();
    std::optional<ExportFormat> DrawPopup(const std::string &plot_name, bool can_export);

    // Saving
    void Start(ExportFigure figure, ExportFormat format, const std::optional<std::filesystem::path> &schematic_path,
               const std::string &file_suffix);
    void Update();
    void DrawError();

private:
    ExportStyle m_Style;
    FileDialog m_Dialog;
    std::optional<ExportFigure> m_PendingFigure;
    ExportFormat m_PendingFormat = ExportFormat::SVG;
    std::string m_Error;
};

} // namespace GUI

#endif // IMCSIM_PLOT_EXPORTER_H
