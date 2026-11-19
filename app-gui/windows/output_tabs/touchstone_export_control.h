/**
 * @file    touchstone_export_control.h
 * @brief   The button of the S-parameters tab that saves the whole matrix as a Touchstone file, with its options.
 */

#ifndef IMCSIM_TOUCHSTONE_EXPORT_CONTROL_H
#define IMCSIM_TOUCHSTONE_EXPORT_CONTROL_H

#include "plots/plot_exporter.h"
#include "plots/touchstone.h"
#include "simulation/simulation_results.h"
#include <filesystem>
#include <optional>
#include <string>

namespace GUI {

/**
 * @class   TouchstoneExportControl
 * @brief   Saves the S matrix of a result as a Touchstone file, in the frequency unit and number format the user picks
 *          in a popup, and keeps the reason the last file could not be written under the button.
 */
class TouchstoneExportControl {
public:
    void Draw(const Core::SParameters &result,
              PlotExporter &exporter,
              const std::optional<std::filesystem::path> &schematic_path);

private:
    TouchstoneOptions m_Options;
    /// Why the last Touchstone export could not be written, or empty.
    std::string m_ExportError;

    void DrawOptions();
    void Save(const Core::SParameters &result,
              PlotExporter &exporter,
              const std::optional<std::filesystem::path> &schematic_path);
};

} // namespace GUI

#endif // IMCSIM_TOUCHSTONE_EXPORT_CONTROL_H
