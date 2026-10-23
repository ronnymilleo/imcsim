/**
 * @file    analysis_settings_panel.h
 * @brief   The analysis picker and the settings fields of each analysis, shared by the editor toolbar and the Analysis
 *          Settings window.
 */

#ifndef IMCSIM_ANALYSIS_SETTINGS_PANEL_H
#define IMCSIM_ANALYSIS_SETTINGS_PANEL_H

#include "parameter_step.h"
#include "schematic.h"
#include "simulation_settings.h"
#include "value_field.h"
#include <array>
#include <cstddef>
#include <string>

namespace GUI {

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

    // Settings fields, reloaded whenever the schematic replaces its settings
    std::size_t m_LoadedSettingsVersion = 0;
    ValueField m_StopTime;
    ValueField m_TimeStep;
    ValueField m_StartFrequency;
    ValueField m_StopFrequency;
    // Start, stop and step of each DC sweep range
    std::array<ValueField, 3> m_SweptFields;
    std::array<ValueField, 3> m_SteppedFields;
    ValueField m_NoiseStartFrequency;
    ValueField m_NoiseStopFrequency;
    // Start, stop and step of the stepped parameter
    std::array<ValueField, 3> m_ParameterFields;

    // Settings fields
    void LoadSettingsFields();
    void DrawTransientSettings(SimulationSettings &settings);
    void DrawACSweepSettings(SimulationSettings &settings);
    void DrawDCSweepSettings(SimulationSettings &settings);
    void DrawNoiseSettings(SimulationSettings &settings);
    void DrawTransferFunctionSettings(SimulationSettings &settings);
    void DrawTruthTableSettings(SimulationSettings &settings);
    void DrawParameterStepSettings(SimulationSettings &settings);
    void DrawParameterPicker(ParameterStepSettings &step, const Core::Circuit &circuit);
    void DrawStepValues(ParameterStepSettings &step, const std::string &unit);
    void LoadParameterStepFields(const ParameterStepSettings &step);
};

} // namespace GUI

#endif // IMCSIM_ANALYSIS_SETTINGS_PANEL_H
