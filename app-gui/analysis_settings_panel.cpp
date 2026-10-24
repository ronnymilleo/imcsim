/**
 * @file    analysis_settings_panel.cpp
 * @brief   The analysis picker and the settings fields of each analysis, shared by the editor toolbar and the Analysis
 *          Settings window.
 */

#include "analysis_settings_panel.h"

#include "analysis_inputs.h"
#include "analysis_suggestions.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "probing.h"
#include "simulation_backend.h"
#include "spice_value.h"
#include "theme.h"
#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace GUI {

namespace {

bool IsPositive(const double value) {
    return value > 0.0;
}

bool AnyValue(double /*value*/) {
    return true;
}

bool IsNonZero(const double value) {
    return value != 0.0;
}

Core::AnalysisKind ToKind(const Analysis analysis) {
    switch (analysis) {
    case Analysis::OPERATING_POINT:
        return Core::AnalysisKind::OPERATING_POINT;
    case Analysis::TRANSIENT:
        return Core::AnalysisKind::TRANSIENT;
    case Analysis::AC_SWEEP:
        return Core::AnalysisKind::AC_SWEEP;
    case Analysis::DC_SWEEP:
        return Core::AnalysisKind::DC_SWEEP;
    case Analysis::NOISE:
        return Core::AnalysisKind::NOISE;
    case Analysis::TRANSFER_FUNCTION:
        return Core::AnalysisKind::TRANSFER_FUNCTION;
    case Analysis::TRUTH_TABLE:
        return Core::AnalysisKind::TRUTH_TABLE;
    default:
        return Core::AnalysisKind::OPERATING_POINT;
    }
}

std::set<Core::ComponentType> ListPartTypes(const Schematic &schematic) {
    std::set<Core::ComponentType> types;
    for (const std::unique_ptr<UIElement> &element : schematic.GetElements()) {
        types.insert(element->GetComponent().GetType());
    }
    return types;
}

void LoadSweepFields(const Core::SweepRange &range, std::array<ValueField, 3> &fields) {
    fields[0].Load(range.Start);
    fields[1].Load(range.Stop);
    fields[2].Load(range.Step);
}

// Draws the source picker and the start, stop and step of one range; sources must not be empty
void DrawSweepRange(const char *id, const char *source_label, const std::vector<SweepSource> &sources,
                    const std::size_t preferred_index, Core::SweepRange &range, std::array<ValueField, 3> &fields) {
    ImGui::PushID(id);
    const std::string shown_source = ResolveSweepSource(range, sources, preferred_index);
    DrawFieldLabel(source_label);
    if (ImGui::BeginCombo("##source", shown_source.c_str())) {
        for (const SweepSource &source : sources) {
            if (ImGui::Selectable(source.Name.c_str(), source.Name == shown_source)) {
                range.Source = source.Name;
            }
        }
        ImGui::EndCombo();
    }
    const char *unit = std::ranges::find(sources, shown_source, &SweepSource::Name)->Unit;
    if (const std::optional<double> start = fields[0].Draw("Start", unit, AnyValue)) {
        range.Start = *start;
    }
    if (const std::optional<double> stop = fields[1].Draw("Stop", unit, AnyValue)) {
        range.Stop = *stop;
    }
    if (const std::optional<double> step = fields[2].Draw("Step", unit, IsNonZero)) {
        range.Step = *step;
    }
    ImGui::PopID();
}

// The values of the range of a step as a list to edit, such as "500, 1k, 1.5k"; empty when the range is invalid
std::string FormatRangeAsList(ParameterStepSettings step, const std::string &unit) {
    step.UsesList = false;
    std::string list;
    if (const auto values = ListStepValues(step, unit)) {
        for (const double value : *values) {
            list += (list.empty() ? "" : ", ") + Core::FormatValue(value);
        }
    }
    return list;
}

// How many runs the step makes, from which value to which, or why it cannot run
void DrawStepSummary(const ParameterStepSettings &step, const std::string &unit) {
    const auto values = ListStepValues(step, unit);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
    if (!values) {
        ImGui::TextColored(GetErrorTextColor(), "%s", values.error().c_str());
    } else {
        const std::string parameter = GetParameterLabel(step);
        ImGui::TextDisabled("%s", std::format("{} runs, {} to {}", values->size(),
                                              FormatStepValue(parameter, values->front(), unit),
                                              FormatStepValue(parameter, values->back(), unit))
                                      .c_str());
    }
    ImGui::PopTextWrapPos();
}

// A Suggest button, disabled with the reason when the circuit gives nothing to base a suggestion on
bool DrawSuggestButton(const bool available, const char *tooltip, const char *unavailable_reason) {
    ImGui::BeginDisabled(!available);
    const bool clicked = ImGui::Button("Suggest");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", available ? tooltip : unavailable_reason);
    return clicked;
}

} // namespace

/**
 * @brief   Creates the panel for a schematic.
 * @param[in] schematic  Schematic whose analyses are set; it must outlive the panel.
 */
AnalysisSettingsPanel::AnalysisSettingsPanel(Schematic &schematic) : m_Schematic(schematic) {
    LoadSettingsFields();
}

/**
 * @brief   Draws one radio button per analysis, in a column, which picks the one Run starts.
 * @note    An analysis the simulation backend cannot run on the circuit is disabled, with the reason as its tooltip.
 */
void AnalysisSettingsPanel::DrawAnalysisPicker() {
    SimulationSettings settings = m_Schematic.GetSimulationSettings();
    const std::set<Core::ComponentType> parts = ListPartTypes(m_Schematic);
    for (std::size_t index = 0; index < ANALYSIS_COUNT; ++index) {
        const std::optional<std::string> unsupported =
            Core::FindUnsupported(Core::GetBackend(), parts, ToKind(static_cast<Analysis>(index)));
        ImGui::BeginDisabled(unsupported.has_value());
        if (ImGui::RadioButton(GetAnalysisName(static_cast<Analysis>(index)),
                               static_cast<std::size_t>(settings.Selected) == index)) {
            settings.Selected = static_cast<Analysis>(index);
        }
        ImGui::EndDisabled();
        if (unsupported) {
            ImGui::SetItemTooltip("%s", unsupported->c_str());
        }
    }
    m_Schematic.SetSimulationSettings(settings);
}

/**
 * @brief   Draws the settings of the selected analysis; the transient and the AC sweep add a Suggest button.
 * @note    The fields edit a copy of the settings, which goes back to the schematic once, at the end.
 */
void AnalysisSettingsPanel::DrawSettings() {
    if (m_LoadedSettingsVersion != m_Schematic.GetSimulationSettingsVersion()) {
        LoadSettingsFields();
    }
    SimulationSettings settings = m_Schematic.GetSimulationSettings();
    switch (settings.Selected) {
    case Analysis::OPERATING_POINT:
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
        ImGui::TextDisabled("No settings: the DC voltages show on the schematic and, with the currents, in the "
                            "Output window");
        ImGui::PopTextWrapPos();
        break;
    case Analysis::TRANSIENT:
        DrawTransientSettings(settings);
        break;
    case Analysis::AC_SWEEP:
        DrawACSweepSettings(settings);
        break;
    case Analysis::DC_SWEEP:
        DrawDCSweepSettings(settings);
        break;
    case Analysis::NOISE:
        DrawNoiseSettings(settings);
        break;
    case Analysis::TRANSFER_FUNCTION:
        DrawTransferFunctionSettings(settings);
        break;
    case Analysis::TRUTH_TABLE:
        DrawTruthTableSettings(settings);
        break;
    }
    if (settings.Selected == Analysis::TRANSIENT || settings.Selected == Analysis::AC_SWEEP ||
        settings.Selected == Analysis::DC_SWEEP) {
        DrawParameterStepSettings(settings);
    }
    m_Schematic.SetSimulationSettings(settings);
}

void AnalysisSettingsPanel::LoadSettingsFields() {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    m_StopTime.Load(settings.Transient.StopTime);
    m_TimeStep.Load(settings.Transient.TimeStep);
    m_LiveSeconds.Load(settings.LiveSeconds);
    m_StartFrequency.Load(settings.ACSweep.StartFrequency);
    m_StopFrequency.Load(settings.ACSweep.StopFrequency);
    LoadSweepFields(settings.SweptRange, m_SweptFields);
    LoadSweepFields(settings.SteppedRange, m_SteppedFields);
    m_NoiseStartFrequency.Load(settings.NoiseRange.StartFrequency);
    m_NoiseStopFrequency.Load(settings.NoiseRange.StopFrequency);
    LoadParameterStepFields(settings.ParameterStep);
    m_LoadedSettingsVersion = m_Schematic.GetSimulationSettingsVersion();
}

// Settings change as soon as a field is valid; ngspice checks how they relate only when the analysis runs
void AnalysisSettingsPanel::DrawTransientSettings(SimulationSettings &settings) {
    if (const std::optional<double> stop_time = m_StopTime.Draw("Stop time", "s", IsPositive)) {
        settings.Transient.StopTime = *stop_time;
    }
    if (const std::optional<double> time_step = m_TimeStep.Draw("Time step", "s", IsPositive)) {
        settings.Transient.TimeStep = *time_step;
    }
    const auto suggested = Core::SuggestTransientSettings(m_Schematic.BuildCircuit());
    if (DrawSuggestButton(suggested.has_value(),
                          "Five periods of the slowest source, or the whole span of a one-shot one",
                          "Needs a source with a waveform") &&
        suggested) {
        settings.Transient = *suggested;
        m_StopTime.Load(suggested->StopTime);
        m_TimeStep.Load(suggested->TimeStep);
    }
    if (const std::optional<double> live_seconds = m_LiveSeconds.Draw("Live run", "s", IsValidLiveSeconds)) {
        settings.LiveSeconds = *live_seconds;
    }
    ImGui::SetItemTooltip("With Live on, the transient runs until it is stopped and covers the stop time in this long "
                          "on the clock, from %.0f to %.0f s; the plot and the schematic show the last stop time",
                          MIN_LIVE_SECONDS, MAX_LIVE_SECONDS);
}

void AnalysisSettingsPanel::DrawACSweepSettings(SimulationSettings &settings) {
    if (const std::optional<double> start = m_StartFrequency.Draw("Start", "Hz", IsPositive)) {
        settings.ACSweep.StartFrequency = *start;
    }
    if (const std::optional<double> stop = m_StopFrequency.Draw("Stop", "Hz", IsPositive)) {
        settings.ACSweep.StopFrequency = *stop;
    }
    DrawFieldLabel("Points");
    // Files reject fewer than one point, so the field never produces a value that would not load back
    if (ImGui::InputInt("per decade", &settings.ACSweep.PointsPerDecade)) {
        settings.ACSweep.PointsPerDecade = std::max(settings.ACSweep.PointsPerDecade, 1);
    }
    const auto suggested = Core::SuggestACSweepSettings(m_Schematic.BuildCircuit());
    if (DrawSuggestButton(suggested.has_value(), "Two decades past the RC, RL and LC corners and the op-amp bandwidths",
                          "Needs a capacitor, an inductor or an op-amp") &&
        suggested) {
        settings.ACSweep.StartFrequency = suggested->StartFrequency;
        settings.ACSweep.StopFrequency = suggested->StopFrequency;
        m_StartFrequency.Load(suggested->StartFrequency);
        m_StopFrequency.Load(suggested->StopFrequency);
    }
}

void AnalysisSettingsPanel::DrawDCSweepSettings(SimulationSettings &settings) {
    const std::vector<SweepSource> sources = ListSweepSources(m_Schematic.BuildCircuit());
    if (sources.empty()) {
        ImGui::TextDisabled("Add a voltage source, current source or VCC to sweep");
        return;
    }
    DrawSweepRange("swept", "Sweep", sources, SWEPT_SOURCE_INDEX, settings.SweptRange, m_SweptFields);
    // A second source and a stepped parameter would both make the family of curves, so only one is on at a time
    ImGui::BeginDisabled(settings.ParameterStep.Enabled && !settings.StepSource);
    ImGui::Checkbox("Step a second source, one curve per value", &settings.StepSource);
    ImGui::EndDisabled();
    if (settings.ParameterStep.Enabled && !settings.StepSource) {
        ImGui::SetItemTooltip("Turn off the stepped parameter below to step a second source");
    }
    if (settings.StepSource) {
        DrawSweepRange("stepped", "Step", sources, STEPPED_SOURCE_INDEX, settings.SteppedRange, m_SteppedFields);
    }
    ImGui::TextDisabled("AC and pulse sources are swept through their DC value");
}

// The output picker lists every node by the label the plots give it; picking one keeps a point of it, so the choice
// follows the node through edits
void AnalysisSettingsPanel::DrawNoiseSettings(SimulationSettings &settings) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::string input = ResolveOptionalInput(settings.NoiseSource, sources);
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (!output) {
        ImGui::TextDisabled("Connect some parts to measure their noise");
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DrawFieldLabel("Output");
    if (ImGui::BeginCombo("##noise_output", GetVoltageLabel(connectivity, *output).c_str())) {
        for (int node = 1; node < circuit.GetNodeCount(); ++node) {
            if (ImGui::Selectable(GetVoltageLabel(connectivity, node).c_str(), node == *output)) {
                settings.NoiseOutput = connectivity.FindPointOfNode(node);
            }
        }
        ImGui::EndCombo();
    }
    DrawFieldLabel("Input");
    if (sources.empty()) {
        ImGui::TextDisabled("Set a voltage or current source to AC");
    } else if (ImGui::BeginCombo("##noise_input", input.c_str())) {
        for (const std::string &source : sources) {
            if (ImGui::Selectable(source.c_str(), source == input)) {
                settings.NoiseSource = source;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The noise is also referred to this source, as the noise it would need at its input");
    if (const std::optional<double> start = m_NoiseStartFrequency.Draw("Start", "Hz", IsPositive)) {
        settings.NoiseRange.StartFrequency = *start;
    }
    if (const std::optional<double> stop = m_NoiseStopFrequency.Draw("Stop", "Hz", IsPositive)) {
        settings.NoiseRange.StopFrequency = *stop;
    }
    DrawFieldLabel("Points");
    if (ImGui::InputInt("per decade##noise", &settings.NoiseRange.PointsPerDecade)) {
        settings.NoiseRange.PointsPerDecade = std::max(settings.NoiseRange.PointsPerDecade, 1);
    }
    const auto suggested = Core::SuggestACSweepSettings(circuit);
    if (DrawSuggestButton(suggested.has_value(), "Two decades past the RC, RL and LC corners and the op-amp bandwidths",
                          "Needs a capacitor, an inductor or an op-amp") &&
        suggested) {
        settings.NoiseRange.StartFrequency = suggested->StartFrequency;
        settings.NoiseRange.StopFrequency = suggested->StopFrequency;
        m_NoiseStartFrequency.Load(suggested->StartFrequency);
        m_NoiseStopFrequency.Load(suggested->StopFrequency);
    }
}

// The output picker lists every node by the label the plots give it, as the noise one does; the input picker lists
// every voltage source, current source and VCC rail, whatever its waveform
void AnalysisSettingsPanel::DrawTransferFunctionSettings(SimulationSettings &settings) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::string input = ResolveOptionalInput(settings.TransferFunctionSource, sources);
    const std::optional<int> output = ResolveOutputNode(settings.TransferFunctionOutput, circuit, m_Schematic);
    if (!output) {
        ImGui::TextDisabled("Connect some parts to measure their transfer function");
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DrawFieldLabel("Output");
    if (ImGui::BeginCombo("##tf_output", GetVoltageLabel(connectivity, *output).c_str())) {
        for (int node = 1; node < circuit.GetNodeCount(); ++node) {
            if (ImGui::Selectable(GetVoltageLabel(connectivity, node).c_str(), node == *output)) {
                settings.TransferFunctionOutput = connectivity.FindPointOfNode(node);
            }
        }
        ImGui::EndCombo();
    }
    DrawFieldLabel("Input");
    if (sources.empty()) {
        ImGui::TextDisabled("Add a voltage source, current source or VCC");
    } else if (ImGui::BeginCombo("##tf_input", input.c_str())) {
        for (const std::string &source : sources) {
            if (ImGui::Selectable(source.c_str(), source == input)) {
                settings.TransferFunctionSource = source;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The DC gain, the input impedance and the output impedance from this source to the output");
}

// One checkbox per logic input, and the nodes the table reads, which are the voltages already measured. Nothing stored
// means every input, and a change stores the whole list
void AnalysisSettingsPanel::DrawTruthTableSettings(SimulationSettings &settings) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::vector<std::string> available = Core::GetTruthTableInputs(circuit);
    if (available.empty()) {
        ImGui::TextDisabled("Add a Logic input to vary");
        return;
    }
    std::vector<std::string> chosen = ResolveTruthTableInputs(settings.TruthTableInputs, available);
    ImGui::TextUnformatted("Inputs to vary");
    for (const std::string &name : available) {
        bool is_chosen = std::ranges::contains(chosen, name);
        if (!ImGui::Checkbox(name.c_str(), &is_chosen)) {
            continue;
        }
        // At least one input stays, since a table of none has no rows to show
        if (!is_chosen && chosen.size() == 1) {
            continue;
        }
        std::vector<std::string> changed;
        for (const std::string &candidate : available) {
            if (candidate == name ? is_chosen : std::ranges::contains(chosen, candidate)) {
                changed.push_back(candidate);
            }
        }
        settings.TruthTableInputs = std::move(changed);
    }
    chosen = ResolveTruthTableInputs(settings.TruthTableInputs, available);
    if (chosen.size() > Core::MAX_TRUTH_TABLE_INPUTS) {
        ImGui::TextColored(GetErrorTextColor(), "At most %zu inputs; clear some", Core::MAX_TRUTH_TABLE_INPUTS);
    } else {
        ImGui::TextDisabled("%zu combinations", std::size_t{1} << chosen.size());
    }
    const std::vector<int> outputs = ListMeasuredNodes(circuit, m_Schematic);
    if (outputs.empty()) {
        ImGui::TextDisabled("Measure a voltage with the Probe tool to read it");
        return;
    }
    std::string labels;
    for (const int node : outputs) {
        labels += labels.empty() ? "" : ", ";
        labels += GetVoltageLabel(m_Schematic.GetConnectivity(), node);
    }
    ImGui::TextDisabled("Reads %s", labels.c_str());
    ImGui::SetItemTooltip("The nodes you measure with the Probe tool; every combination of the inputs is simulated and "
                          "each of these is read as 0, 1 or X");
}

// Below the settings of the transient, the AC sweep and the DC sweep: the part or the temperature, and its values
// as a range or a typed list. The range fields share their labels with those of the analysis above, so they get an
// ID of their own
void AnalysisSettingsPanel::DrawParameterStepSettings(SimulationSettings &settings) {
    ParameterStepSettings &step = settings.ParameterStep;
    ImGui::Separator();
    ImGui::PushID("parameter_step");
    // A second stepped source of the DC sweep would also make the family of curves, so only one is on at a time
    const bool blocked = settings.Selected == Analysis::DC_SWEEP && settings.StepSource && !step.Enabled;
    ImGui::BeginDisabled(blocked);
    ImGui::Checkbox("Step a parameter, one run per value", &step.Enabled);
    ImGui::EndDisabled();
    if (blocked) {
        ImGui::SetItemTooltip("Turn off the second stepped source above to step a parameter");
    }
    if (step.Enabled) {
        const Core::Circuit circuit = m_Schematic.BuildCircuit();
        DrawParameterPicker(step, circuit);
        DrawStepValues(step, GetParameterUnit(circuit, step.Part));
    }
    ImGui::PopID();
}

// The temperature or a resistor, capacitor or inductor; picking a part starts its range around its value, from
// half to twice it
void AnalysisSettingsPanel::DrawParameterPicker(ParameterStepSettings &step, const Core::Circuit &circuit) {
    DrawFieldLabel("Parameter");
    const std::string shown = step.Part.empty() ? std::string("Temperature") : step.Part;
    if (!ImGui::BeginCombo("##step_parameter", shown.c_str())) {
        return;
    }
    if (ImGui::Selectable("Temperature", step.Part.empty()) && !step.Part.empty()) {
        step.Part.clear();
        step.Start = 0.0;
        step.Stop = 100.0;
        step.Step = 25.0;
        LoadParameterStepFields(step);
    }
    const std::vector<std::string> steppable = Core::GetSteppableParts(circuit);
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        const std::string &part = entry.Part->GetName();
        if (!std::ranges::contains(steppable, part)) {
            continue;
        }
        const double value = entry.Part->GetValue();
        const std::string label = std::format("{} ({}{})", part, Core::FormatValue(value), entry.Part->GetUnit());
        if (ImGui::Selectable(label.c_str(), step.Part == part) && step.Part != part) {
            step.Part = part;
            step.Start = value / 2.0;
            step.Stop = value * 2.0;
            step.Step = value / 2.0;
            LoadParameterStepFields(step);
        }
    }
    ImGui::EndCombo();
}

// A range or a typed list, and below it how many runs it makes or why it cannot run. A list starts from the values
// of the range, so it can be edited from there
void AnalysisSettingsPanel::DrawStepValues(ParameterStepSettings &step, const std::string &unit) {
    if (ImGui::RadioButton("Range", !step.UsesList)) {
        step.UsesList = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("List", step.UsesList) && !step.UsesList) {
        step.UsesList = true;
        if (step.List.empty()) {
            step.List = FormatRangeAsList(step, unit);
        }
    }
    if (step.UsesList) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        ImGui::InputTextWithHint("##step_list", "1k, 2.2k, 4.7k", &step.List);
    } else {
        if (const std::optional<double> start = m_ParameterFields[0].Draw("Start", unit, AnyValue)) {
            step.Start = *start;
        }
        if (const std::optional<double> stop = m_ParameterFields[1].Draw("Stop", unit, AnyValue)) {
            step.Stop = *stop;
        }
        if (const std::optional<double> step_size = m_ParameterFields[2].Draw("Step", unit, IsNonZero)) {
            step.Step = *step_size;
        }
    }
    DrawStepSummary(step, unit);
}

void AnalysisSettingsPanel::LoadParameterStepFields(const ParameterStepSettings &step) {
    m_ParameterFields[0].Load(step.Start);
    m_ParameterFields[1].Load(step.Stop);
    m_ParameterFields[2].Load(step.Step);
}

} // namespace GUI
