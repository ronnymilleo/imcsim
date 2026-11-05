/**
 * @file    analysis_settings_panel.cpp
 * @brief   The analysis picker and the settings fields of each analysis, shared by the editor toolbar and the Analysis
 *          Settings window.
 */

#include "analysis/analysis_settings_panel.h"

#include "analysis/analysis_inputs.h"
#include "drawing/theme.h"
#include "editing/probing.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "simulation/analysis_suggestions.h"
#include "simulation/circuit_checks.h"
#include "simulation/simulation_backend.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <random>
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

/**
 * @enum    RunMode
 * @brief   How the transient, AC sweep and DC sweep repeat: not at all, once per value of a parameter, or once per draw
 *          of the values of the parts within their tolerances.
 */
enum class RunMode {
    SINGLE,
    STEP,
    MONTE_CARLO,
};

constexpr std::array<const char *, 3> RUN_MODE_NAMES = {"Single run", "Step a parameter", "Monte Carlo"};

RunMode GetRunMode(const SimulationSettings &settings) {
    if (settings.MonteCarlo.Enabled) {
        return RunMode::MONTE_CARLO;
    }
    return settings.ParameterStep.Enabled ? RunMode::STEP : RunMode::SINGLE;
}

/**
 * @brief   Turns one mode on and the others off, so the step and Monte Carlo are never both on; the first time Monte
 *          Carlo is on it gets a random seed.
 */
void SetRunMode(SimulationSettings &settings, const RunMode mode) {
    settings.ParameterStep.Enabled = mode == RunMode::STEP;
    settings.MonteCarlo.Enabled = mode == RunMode::MONTE_CARLO;
    if (mode == RunMode::MONTE_CARLO && settings.MonteCarlo.Seed == 0) {
        std::random_device device;
        settings.MonteCarlo.Seed = device() | 1U;
    }
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
    case Analysis::SENSITIVITY:
        return Core::AnalysisKind::SENSITIVITY;
    case Analysis::POLE_ZERO:
        return Core::AnalysisKind::POLE_ZERO;
    case Analysis::S_PARAMETERS:
        return Core::AnalysisKind::S_PARAMETERS;
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

/**
 * @brief   Draws the source picker and the start, stop and step of one range; sources must not be empty.
 */
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

/**
 * @brief   The values of the range of a step as a list to edit, such as "500, 1k, 1.5k"; empty when the range is
 *          invalid.
 */
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

/**
 * @brief   How many runs the step makes, from which value to which, or why it cannot run.
 */
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

/**
 * @brief   A Suggest button, disabled with the reason when the circuit gives nothing to base a suggestion on.
 */
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
    m_ToleranceAllField.Load(m_ToleranceAllPercent);
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
    case Analysis::SENSITIVITY:
        DrawSensitivitySettings(settings);
        break;
    case Analysis::POLE_ZERO:
        DrawPoleZeroSettings(settings);
        break;
    case Analysis::S_PARAMETERS:
        DrawSParameterSettings(settings);
        break;
    case Analysis::TRUTH_TABLE:
        DrawTruthTableSettings(settings);
        break;
    }
    if (settings.Selected == Analysis::TRANSIENT || settings.Selected == Analysis::AC_SWEEP ||
        settings.Selected == Analysis::DC_SWEEP) {
        DrawRunModeSettings(settings);
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
    m_SStartFrequency.Load(settings.SParameters.StartFrequency);
    m_SStopFrequency.Load(settings.SParameters.StopFrequency);
    LoadParameterStepFields(settings.ParameterStep);
    m_LoadedSettingsVersion = m_Schematic.GetSimulationSettingsVersion();
}

/**
 * @brief   Settings change as soon as a field is valid; ngspice checks how they relate only when the analysis runs.
 */
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
        settings.Transient.StopTime = suggested->StopTime;
        settings.Transient.TimeStep = suggested->TimeStep;
        m_StopTime.Load(suggested->StopTime);
        m_TimeStep.Load(suggested->TimeStep);
    }
    if (const std::optional<double> live_seconds = m_LiveSeconds.Draw("Live run", "s", IsValidLiveSeconds)) {
        settings.LiveSeconds = *live_seconds;
    }
    ImGui::SetItemTooltip("With Live on, the transient runs until it is stopped and covers the stop time in this long "
                          "on the clock, from %.0f to %.0f s; the plot and the schematic show the last stop time",
                          MIN_LIVE_SECONDS, MAX_LIVE_SECONDS);
    DrawFieldLabel("Seed");
    // Files reject a negative seed, so the field never produces a value that would not load back
    int seed = static_cast<int>(settings.Transient.Seed);
    if (ImGui::InputInt("##seed", &seed, 0)) {
        settings.Transient.Seed = static_cast<unsigned int>(std::max(seed, 0));
    }
    ImGui::SetItemTooltip(
        "0 draws new random numbers each run. Another number repeats the values of the Random sources "
        "and the telegraph noise of the Noise sources. The white and 1/f noise of a Noise source "
        "change on each run whatever the seed");
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
        ImGui::TextDisabled("%s", std::format("Add {} to sweep", Core::SWEEPABLE_SOURCES).c_str());
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

/**
 * @brief   The output picker lists every node by the label the plots give it; picking one keeps a point of it, so the
 *          choice follows the node through edits.
 */
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

/**
 * @brief   The output picker lists every node by the label the plots give it, as the noise one does; the input picker
 *          lists every source a DC sweep could drive, whatever its waveform.
 */
void AnalysisSettingsPanel::DrawTransferFunctionSettings(SimulationSettings &settings) {
    DrawSourceAndOutputPickers(
        settings.TransferFunctionSource, settings.TransferFunctionOutput, "tf",
        "Connect some parts to measure their transfer function",
        "The DC gain, the input impedance and the output impedance from this source to the output");
}

/**
 * @brief   The frequency range of the S-parameter analysis, and the ports the circuit has.
 * @details The ports are the Block pin markers set as ports in their properties, so there is nothing to pick here.
 */
void AnalysisSettingsPanel::DrawSParameterSettings(SimulationSettings &settings) {
    Core::SParameterSettings &range = settings.SParameters;
    if (const std::optional<double> start = m_SStartFrequency.Draw("Start", "Hz", IsPositive)) {
        range.StartFrequency = *start;
    }
    if (const std::optional<double> stop = m_SStopFrequency.Draw("Stop", "Hz", IsPositive)) {
        range.StopFrequency = *stop;
    }
    DrawFieldLabel("Sweep");
    if (ImGui::RadioButton("Log", range.IsLogarithmic)) {
        range.IsLogarithmic = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Lin", !range.IsLogarithmic)) {
        range.IsLogarithmic = false;
    }
    DrawFieldLabel("Points");
    // Files reject too few points, so the fields never produce a value that would not load back
    if (range.IsLogarithmic) {
        if (ImGui::InputInt("per decade", &range.PointsPerDecade)) {
            range.PointsPerDecade = std::max(range.PointsPerDecade, 1);
        }
    } else if (ImGui::InputInt("in all", &range.LinearPoints)) {
        range.LinearPoints = std::max(range.LinearPoints, 3);
    }
    DrawPortSummary();
}

/**
 * @brief   Lists the ports of the circuit, or says how to make them.
 */
void AnalysisSettingsPanel::DrawPortSummary() {
    std::vector<Core::CircuitPort> ports = m_Schematic.BuildCircuit().GetPorts();
    std::ranges::sort(ports, {}, &Core::CircuitPort::Number);
    if (ports.size() < Core::MIN_S_PARAMETER_PORTS) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
        ImGui::TextColored(GetErrorTextColor(), "%s",
                           "Needs two or more ports: turn on S-parameter port in the properties of Block pin markers");
        ImGui::PopTextWrapPos();
        return;
    }
    for (const Core::CircuitPort &port : ports) {
        ImGui::TextDisabled("%s",
                            std::format("Port {}: {} ohm", port.Number, Core::FormatValue(port.ReferenceOhms)).c_str());
    }
}

/**
 * @brief   The pickers of the transfer function: the poles and zeros are those between a source and an output node.
 */
void AnalysisSettingsPanel::DrawPoleZeroSettings(SimulationSettings &settings) {
    DrawSourceAndOutputPickers(settings.PoleZeroSource, settings.PoleZeroOutput, "pole_zero",
                               "Connect some parts to find their poles and zeros",
                               "The poles and zeros of the transfer function from this source to the output; a voltage "
                               "source gives a voltage gain and a current source a transimpedance");
}

/**
 * @brief   An output picker over every node, labelled as the plots label them, and an input picker over the sources.
 * @param[in,out] source       Stored input source; changed when the user picks another.
 * @param[in,out] output       Stored output point; changed when the user picks another node.
 * @param[in]     id           Prefix that tells the combos of this analysis from those of the others.
 * @param[in]     empty_text   Shown in place of the pickers when the circuit has no node to pick.
 * @param[in]     tooltip      What the input picker is for.
 */
void AnalysisSettingsPanel::DrawSourceAndOutputPickers(std::string &source, std::optional<GridPoint> &output,
                                                       const std::string &id, const char *empty_text,
                                                       const char *tooltip) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::vector<std::string> sources = Core::GetTransferFunctionInputSources(circuit);
    const std::string input = ResolveOptionalInput(source, sources);
    const std::optional<int> output_node = ResolveOutputNode(output, circuit, m_Schematic);
    if (!output_node) {
        ImGui::TextDisabled("%s", empty_text);
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DrawFieldLabel("Output");
    if (ImGui::BeginCombo(std::format("##{}_output", id).c_str(),
                          GetVoltageLabel(connectivity, *output_node).c_str())) {
        for (int node = 1; node < circuit.GetNodeCount(); ++node) {
            if (ImGui::Selectable(GetVoltageLabel(connectivity, node).c_str(), node == *output_node)) {
                output = connectivity.FindPointOfNode(node);
            }
        }
        ImGui::EndCombo();
    }
    DrawFieldLabel("Input");
    if (sources.empty()) {
        ImGui::TextDisabled("%s", std::format("Add {}", Core::SWEEPABLE_SOURCES).c_str());
    } else if (ImGui::BeginCombo(std::format("##{}_input", id).c_str(), input.c_str())) {
        for (const std::string &candidate : sources) {
            if (ImGui::Selectable(candidate.c_str(), candidate == input)) {
                source = candidate;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tooltip);
}

/**
 * @brief   The output picker lists every node by the label the plots give it, as the transfer function one does.
 */
void AnalysisSettingsPanel::DrawSensitivitySettings(SimulationSettings &settings) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (!output) {
        ImGui::TextDisabled("Connect some parts to measure their sensitivity");
        return;
    }
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DrawFieldLabel("Output");
    if (ImGui::BeginCombo("##sensitivity_output", GetVoltageLabel(connectivity, *output).c_str())) {
        for (int node = 1; node < circuit.GetNodeCount(); ++node) {
            if (ImGui::Selectable(GetVoltageLabel(connectivity, node).c_str(), node == *output)) {
                settings.SensitivityOutput = connectivity.FindPointOfNode(node);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("How much the DC voltage of this node changes with the value of each part");
}

/**
 * @brief   One checkbox per logic input, and the nodes the table reads, which are the voltages already measured.
 * @details Nothing stored means every input, and a change stores the whole list.
 */
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

/**
 * @brief   Below the settings of the transient, the AC sweep and the DC sweep: how they repeat, which is not at all,
 * once per value of a parameter or once per draw of the parts within their tolerances.
 * @details The fields share their labels with those of the analysis above, so they get an ID of their own.
 */
void AnalysisSettingsPanel::DrawRunModeSettings(SimulationSettings &settings) {
    ImGui::Separator();
    ImGui::PushID("run_mode");
    // A second stepped source of the DC sweep would also make the family of curves, so a repeated run waits for it
    const bool is_blocked = settings.Selected == Analysis::DC_SWEEP && settings.StepSource;
    const RunMode mode = GetRunMode(settings);
    DrawFieldLabel("Run mode");
    if (ImGui::BeginCombo("##run_mode", RUN_MODE_NAMES[static_cast<std::size_t>(mode)])) {
        for (std::size_t index = 0; index < RUN_MODE_NAMES.size(); ++index) {
            const auto candidate = static_cast<RunMode>(index);
            ImGui::BeginDisabled(is_blocked && candidate != RunMode::SINGLE);
            if (ImGui::Selectable(RUN_MODE_NAMES[index], candidate == mode) && candidate != mode) {
                SetRunMode(settings, candidate);
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Single run: the analysis once. Step a parameter: once per value of a part or of the "
                          "temperature. Monte Carlo: once with every value as it is, then with the values of the parts "
                          "drawn within their tolerances");
    if (is_blocked) {
        ImGui::TextDisabled("Turn off the second stepped source above to repeat the run");
    }
    if (settings.ParameterStep.Enabled) {
        DrawParameterStepSettings(settings.ParameterStep);
    } else if (settings.MonteCarlo.Enabled) {
        DrawMonteCarloSettings(settings.MonteCarlo);
    }
    ImGui::PopID();
}

/**
 * @brief   The part or the temperature, and its values as a range or a typed list.
 */
void AnalysisSettingsPanel::DrawParameterStepSettings(ParameterStepSettings &step) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    DrawParameterPicker(step, circuit);
    DrawStepValues(step, GetParameterUnit(circuit, step.Part));
}

/**
 * @brief   The number of runs, how the values spread, the seed, and the tolerances of the parts.
 */
void AnalysisSettingsPanel::DrawMonteCarloSettings(Core::MonteCarloSettings &monte_carlo) {
    DrawFieldLabel("Runs");
    if (ImGui::InputInt("##runs", &monte_carlo.Runs, 1, 10)) {
        monte_carlo.Runs = std::clamp(monte_carlo.Runs, Core::MIN_MONTE_CARLO_RUNS, Core::MAX_MONTE_CARLO_RUNS);
    }
    ImGui::SetItemTooltip("%s", std::format("Draws, besides the nominal run that keeps every value; {} to {}",
                                            Core::MIN_MONTE_CARLO_RUNS, Core::MAX_MONTE_CARLO_RUNS)
                                    .c_str());
    DrawFieldLabel("Distribution");
    if (ImGui::BeginCombo("##distribution", Core::GetDistributionName(monte_carlo.Distribution))) {
        for (const Core::ToleranceDistribution candidate :
             {Core::ToleranceDistribution::GAUSSIAN, Core::ToleranceDistribution::UNIFORM}) {
            if (ImGui::Selectable(Core::GetDistributionName(candidate), candidate == monte_carlo.Distribution)) {
                monte_carlo.Distribution = candidate;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Gaussian: the tolerance is three standard deviations, and no value falls beyond it. "
                          "Uniform: every value within the tolerance is as likely");
    DrawFieldLabel("Seed");
    ImGui::InputScalar("##seed", ImGuiDataType_U32, &monte_carlo.Seed);
    ImGui::SetItemTooltip("The same seed draws the same values, whichever the computer, so a run can be repeated");
    if (ImGui::Button("New seed")) {
        std::random_device device;
        monte_carlo.Seed = device() | 1U;
    }
    DrawToleranceButtons();
    DrawMonteCarloSummary(monte_carlo);
}

/**
 * @brief   Gives every resistor, capacitor or inductor one tolerance, or takes it from them, as a shortcut over setting
 *          it part by part in the properties.
 */
void AnalysisSettingsPanel::DrawToleranceButtons() {
    ImGui::TextDisabled("Tolerance of all");
    if (const std::optional<double> percent = m_ToleranceAllField.Draw(
            "Percent", "%", [](const double value) { return value > 0.0 && value <= Core::MAX_TOLERANCE_PERCENT; })) {
        m_ToleranceAllPercent = *percent;
    }
    struct Button {
        const char *Label;
        Core::ComponentType Type;
    };
    const std::array<Button, 3> buttons = {{{"Resistors", Core::ComponentType::RESISTOR},
                                            {"Capacitors", Core::ComponentType::CAPACITOR},
                                            {"Inductors", Core::ComponentType::INDUCTOR}}};
    std::vector<Core::Component *> parts;
    for (const std::unique_ptr<UIElement> &element : m_Schematic.GetElements()) {
        parts.push_back(&element->GetComponent());
    }
    bool is_changed = false;
    for (const Button &button : buttons) {
        if (ImGui::Button(button.Label)) {
            is_changed = Core::SetTolerances(parts, button.Type, m_ToleranceAllPercent) > 0 || is_changed;
        }
        ImGui::SetItemTooltip("Sets the tolerance of every %s to the percent above", button.Label);
        ImGui::SameLine();
    }
    if (ImGui::Button("Clear all")) {
        for (const Button &button : buttons) {
            is_changed = Core::SetTolerances(parts, button.Type, std::nullopt) > 0 || is_changed;
        }
    }
    ImGui::SetItemTooltip("Takes the tolerance from every resistor, capacitor and inductor");
    if (is_changed) {
        m_Schematic.MarkModified();
    }
}

/**
 * @brief   Which parts vary, or why the run cannot start.
 */
void AnalysisSettingsPanel::DrawMonteCarloSummary(const Core::MonteCarloSettings &monte_carlo) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
    if (const std::optional<std::string> problem = Core::CheckMonteCarlo(circuit, monte_carlo)) {
        ImGui::TextColored(GetErrorTextColor(), "%s", problem->c_str());
    } else {
        std::string names;
        for (const Core::Component *part : Core::FindVariedParts(circuit)) {
            names += (names.empty() ? "" : ", ") + part->GetName();
        }
        ImGui::TextDisabled("%s", std::format("{} runs; these parts vary: {}", monte_carlo.Runs, names).c_str());
    }
    ImGui::PopTextWrapPos();
}

/**
 * @brief   The temperature or a resistor, capacitor or inductor; picking a part starts its range around its value, from
 *          half to twice it.
 */
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

/**
 * @brief   A range or a typed list, and below it how many runs it makes or why it cannot run.
 * @details A list starts from the values of the range, so it can be edited from there.
 */
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
