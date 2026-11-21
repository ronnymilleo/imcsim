/**
 * @file    run_mode_settings_panel.h
 * @brief   The run mode of the analyses that repeat, with the fields of a parameter step and of a Monte Carlo run.
 */

#ifndef IMCSIM_RUN_MODE_SETTINGS_PANEL_H
#define IMCSIM_RUN_MODE_SETTINGS_PANEL_H

#include "base/value_field.h"
#include "schematic/parameter_step_settings.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include "simulation/monte_carlo.h"
#include <array>
#include <optional>
#include <span>
#include <string>

namespace GUI {

/**
 * @class   RunModeSettingsPanel
 * @brief   Picks how the operating point, the transient, the AC sweep and the DC sweep repeat, and edits the step or
 *          the Monte Carlo settings of that mode.
 * @details The AnalysisSettingsPanel owns one and draws it below the settings of a repeatable analysis; the fields
 *          keep the text being typed, so they reload with the settings of the panel.
 */
class RunModeSettingsPanel {
public:
    explicit RunModeSettingsPanel(Schematic &schematic);

    // Drawing
    void Draw(SimulationSettings &settings);

    // Fields
    void Load(const SimulationSettings &settings);

private:
    Schematic &m_Schematic;

    // Fields
    /// Start, stop and step of the stepped parameter.
    std::array<ValueField, 3> m_ParameterFields;
    /// The tolerance in percent that the buttons of Monte Carlo give every part of a kind.
    ValueField m_ToleranceAllField;
    double m_ToleranceAllPercent = 5.0;

    // Parameter step
    void DrawParameterStepSettings(ParameterStepSettings &step);
    void DrawParameterPicker(ParameterStepSettings &step, const Core::Circuit &circuit);
    void DrawStepValues(ParameterStepSettings &step, const std::string &unit);
    void LoadParameterStepFields(const ParameterStepSettings &step);

    // Monte Carlo
    void DrawMonteCarloSettings(Core::MonteCarloSettings &monte_carlo);
    void DrawToleranceButtons();
    void ApplyToleranceToAll(std::span<const Core::ComponentType> types, std::optional<double> percent);
    void DrawMonteCarloSummary(const Core::MonteCarloSettings &monte_carlo);
};

} // namespace GUI

#endif // IMCSIM_RUN_MODE_SETTINGS_PANEL_H
