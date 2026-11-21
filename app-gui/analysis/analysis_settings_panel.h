/**
 * @file    analysis_settings_panel.h
 * @brief   The analysis picker and the settings fields of each analysis, shared by the editor toolbar and the Analysis
 *          Settings window.
 */

#ifndef IMCSIM_ANALYSIS_SETTINGS_PANEL_H
#define IMCSIM_ANALYSIS_SETTINGS_PANEL_H

#include "analysis/run_mode_settings_panel.h"
#include "base/value_field.h"
#include "schematic/schematic.h"
#include "schematic/simulation_settings.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace GUI {

/**
 * @struct  FrequencyRangeFields
 * @brief   The start and stop fields of a frequency range, which keep the text being typed.
 */
struct FrequencyRangeFields {
    ValueField Start;
    ValueField Stop;
};

/**
 * @class   AnalysisSettingsPanel
 * @brief   Picks the analysis Run starts and edits the settings of each analysis.
 * @details The settings live in the Schematic, which saves them with the circuit. The Application owns one instance,
 *          which the editor toolbar and the Analysis Settings window both draw, so the fields being typed are the same
 *          in both.
 */
class AnalysisSettingsPanel {
public:
    explicit AnalysisSettingsPanel(Schematic &schematic);

    // Drawing
    void DrawAnalysisPicker();
    void DrawSettings();

private:
    Schematic &m_Schematic;

    // Settings fields
    /// Version of the settings the fields were loaded from, so they reload whenever the schematic replaces its
    /// settings.
    std::size_t m_LoadedSettingsVersion = 0;
    ValueField m_StopTime;
    ValueField m_TimeStep;
    ValueField m_LiveSeconds;
    FrequencyRangeFields m_ACSweepRange;
    /// Start, stop and step of the swept DC sweep range.
    std::array<ValueField, 3> m_SweptFields;
    /// Start, stop and step of the stepped DC sweep range.
    std::array<ValueField, 3> m_SteppedFields;
    FrequencyRangeFields m_NoiseRange;
    FrequencyRangeFields m_ACSensitivityRange;
    FrequencyRangeFields m_SParameterRange;
    ValueField m_HBFundamental;
    ValueField m_RelativeTolerance;
    ValueField m_CurrentTolerance;
    ValueField m_VoltageTolerance;
    ValueField m_NominalTemperature;

    // Run mode
    RunModeSettingsPanel m_RunModeSettings;

    // Settings fields
    void DrawSelectedSettings(SimulationSettings &settings);
    void LoadSettingsFields();
    void DrawOperatingPointSettings();
    void DrawTransientSettings(SimulationSettings &settings);
    void DrawACSweepSettings(SimulationSettings &settings);
    void DrawDCSweepSettings(SimulationSettings &settings);
    void DrawNoiseSettings(SimulationSettings &settings);
    void DrawTransferFunctionSettings(SimulationSettings &settings);
    void DrawSParameterSettings(SimulationSettings &settings);
    void DrawPortSummary();
    void DrawPoleZeroSettings(SimulationSettings &settings);
    void DrawSourceAndOutputPickers(std::string &source,
                                    std::optional<GridPoint> &output,
                                    const std::string &id,
                                    const char *empty_text,
                                    const char *tooltip);
    void
    DrawOutputPicker(const char *id, std::optional<GridPoint> &output, int shown_node, const Core::Circuit &circuit);
    void DrawSensitivitySettings(SimulationSettings &settings);
    void DrawACSensitivityInputs(SimulationSettings &settings, const Core::Circuit &circuit);
    void DrawTruthTableSettings(SimulationSettings &settings);
    void DrawHarmonicBalanceSettings(SimulationSettings &settings);
    void DrawSolverSettings(Core::SolverOptions &options);
    void LoadSolverFields(const Core::SolverOptions &options);
};

} // namespace GUI

#endif // IMCSIM_ANALYSIS_SETTINGS_PANEL_H
