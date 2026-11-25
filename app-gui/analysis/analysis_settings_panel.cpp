/**
 * @file    analysis_settings_panel.cpp
 * @brief   The analysis picker and the settings fields of each analysis, shared by the editor toolbar and the Analysis
 *          Settings window.
 */

#include "analysis/analysis_settings_panel.h"

#include "analysis/analysis_inputs.h"
#include "analysis/harmonic_balance_samples.h"
#include "base/theme.h"
#include "imgui.h"
#include "schematic/probing.h"
#include "simulation/analysis_sources.h"
#include "simulation/analysis_suggestions.h"
#include "simulation/check_s_parameters.h"
#include "simulation/check_solver_options.h"
#include "simulation/simulation_backend.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace GUI {

namespace {

bool IsPositive(const double value) {
    return value > 0.0;
}

/// What the input picker of an analysis driven through an AC source says when the circuit has none.
constexpr const char *AC_INPUT_HINT = "Set a voltage or current source to AC";

constexpr std::array<const char *, 3> INTEGRATION_METHOD_NAMES = {"Default", "Trapezoidal", "Gear"};

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
void DrawSweepRange(const char *id,
                    const char *source_label,
                    const std::vector<SweepSource> &sources,
                    const std::size_t preferred_index,
                    Core::SweepRange &range,
                    std::array<ValueField, 3> &fields) {
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
 * @brief   A button that suggests settings, disabled with the reason when the circuit gives nothing to base a
 *          suggestion on.
 */
bool DrawSuggestButton(const char *label, const bool available, const char *tooltip, const char *unavailable_reason) {
    ImGui::BeginDisabled(!available);
    const bool clicked = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", available ? tooltip : unavailable_reason);
    return clicked;
}

void LoadFrequencyFields(FrequencyRangeFields &fields, const double start, const double stop) {
    fields.Start.Load(start);
    fields.Stop.Load(stop);
}

/**
 * @brief   Draws the start and stop of a frequency range; each changes as soon as its field is valid.
 */
void DrawFrequencyFields(FrequencyRangeFields &fields, double &start, double &stop) {
    if (const std::optional<double> value = fields.Start.Draw("Start", "Hz", IsPositive)) {
        start = *value;
    }
    if (const std::optional<double> value = fields.Stop.Draw("Stop", "Hz", IsPositive)) {
        stop = *value;
    }
}

void DrawPointsPerDecade(int &points) {
    // Files reject fewer than one point, so the field never produces a value that would not load back
    if (ImGui::InputInt("per decade", &points)) {
        points = std::max(points, 1);
    }
}

/**
 * @brief   Draws the start, stop and points per decade of a logarithmic sweep, and a Suggest button that sets the range
 *          from the corners of the circuit.
 * @details The id tells the fields of one analysis from those of another.
 */
void DrawLogarithmicSweep(const char *id,
                          Core::ACSweepSettings &range,
                          FrequencyRangeFields &fields,
                          const Core::Circuit &circuit) {
    ImGui::PushID(id);
    DrawFrequencyFields(fields, range.StartFrequency, range.StopFrequency);
    DrawFieldLabel("Points");
    DrawPointsPerDecade(range.PointsPerDecade);
    const auto suggested = Core::SuggestACSweepSettings(circuit);
    if (DrawSuggestButton("Suggest",
                          suggested.has_value(),
                          "Two decades past the RC, RL and LC corners and the op-amp bandwidths",
                          "Needs a capacitor, an inductor or an op-amp")
        && suggested) {
        range.StartFrequency = suggested->StartFrequency;
        range.StopFrequency = suggested->StopFrequency;
        LoadFrequencyFields(fields, suggested->StartFrequency, suggested->StopFrequency);
    }
    ImGui::PopID();
}

/**
 * @brief   A combo over the sources that can be the input of an analysis, or the text that says how to get one.
 * @param[in]     id          Hidden label of the combo, which tells it from the combos of the other analyses.
 * @param[in,out] stored      Stored input source; changed when the user picks another.
 * @param[in]     sources     Sources to pick from; the combo shows the first one when the stored one is not among them.
 * @param[in]     empty_text  Shown in place of the combo when there is no source.
 */
void DrawInputPicker(const char *id,
                     std::string &stored,
                     const std::vector<std::string> &sources,
                     const std::string &empty_text) {
    DrawFieldLabel("Input");
    if (sources.empty()) {
        ImGui::TextDisabled("%s", empty_text.c_str());
        return;
    }
    const std::string input = ResolveOptionalInput(stored, sources);
    if (ImGui::BeginCombo(id, input.c_str())) {
        for (const std::string &source : sources) {
            if (ImGui::Selectable(source.c_str(), source == input)) {
                stored = source;
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

/**
 * @brief   Creates the panel for a schematic.
 * @param[in] schematic  Schematic whose analyses are set; it must outlive the panel.
 */
AnalysisSettingsPanel::AnalysisSettingsPanel(Schematic &schematic)
    : m_Schematic(schematic), m_RunModeSettings(schematic) {
    LoadSettingsFields();
}

/**
 * @brief   Draws one radio button per analysis, in a column, which picks the one Run starts.
 * @note    An analysis the simulation backend cannot run on the circuit is disabled, with the reason as its tooltip.
 */
void AnalysisSettingsPanel::DrawAnalysisPicker() {
    SimulationSettings settings = m_Schematic.GetSimulationSettings();
    const std::set<Core::ComponentType> parts = ListPartTypes(m_Schematic);
    for (const Analysis analysis : ALL_ANALYSES) {
        const std::optional<std::string> unsupported =
            Core::FindUnsupported(Core::GetBackend(), parts, ToKind(analysis));
        ImGui::BeginDisabled(unsupported.has_value());
        if (ImGui::RadioButton(GetAnalysisName(analysis), settings.Selected == analysis)) {
            settings.Selected = analysis;
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
    DrawSelectedSettings(settings);
    if (IsRepeatable(settings.Selected)) {
        m_RunModeSettings.Draw(settings);
    }
    DrawSolverSettings(settings.Solver);
    m_Schematic.SetSimulationSettings(settings);
}

/**
 * @brief   Draws the fields of the analysis the settings select.
 */
void AnalysisSettingsPanel::DrawSelectedSettings(SimulationSettings &settings) {
    switch (settings.Selected) {
    case Analysis::OPERATING_POINT:
        DrawOperatingPointSettings();
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
    case Analysis::HARMONIC_BALANCE:
        DrawHarmonicBalanceSettings(settings);
        break;
    default:
        break;
    }
}

void AnalysisSettingsPanel::LoadSettingsFields() {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    m_StopTime.Load(settings.Transient.StopTime);
    m_TimeStep.Load(settings.Transient.TimeStep);
    m_LiveSeconds.Load(settings.LiveSeconds);
    LoadFrequencyFields(m_ACSweepRange, settings.ACSweep.StartFrequency, settings.ACSweep.StopFrequency);
    LoadSweepFields(settings.SweptRange, m_SweptFields);
    LoadSweepFields(settings.SteppedRange, m_SteppedFields);
    LoadFrequencyFields(m_NoiseRange, settings.NoiseRange.StartFrequency, settings.NoiseRange.StopFrequency);
    LoadFrequencyFields(m_ACSensitivityRange,
                        settings.ACSensitivityRange.StartFrequency,
                        settings.ACSensitivityRange.StopFrequency);
    LoadFrequencyFields(m_SParameterRange, settings.SParameters.StartFrequency, settings.SParameters.StopFrequency);
    m_HBFundamental.Load(settings.HarmonicBalance.FundamentalFrequency);
    m_RunModeSettings.Load(settings);
    LoadSolverFields(settings.Solver);
    m_LoadedSettingsVersion = m_Schematic.GetSimulationSettingsVersion();
}

/**
 * @brief   The operating point has no settings, so it says where its results show.
 */
void AnalysisSettingsPanel::DrawOperatingPointSettings() {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
    ImGui::TextDisabled("The DC voltages show on the schematic and, with the currents, in the Output window");
    ImGui::PopTextWrapPos();
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
    const auto suggested = Core::SuggestTransientSettings(m_Schematic.GetCircuit());
    if (DrawSuggestButton("Suggest",
                          suggested.has_value(),
                          "Five periods of the slowest source, or the whole span of a one-shot one",
                          "Needs a source with a waveform")
        && suggested) {
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
                          MIN_LIVE_SECONDS,
                          MAX_LIVE_SECONDS);
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
    DrawLogarithmicSweep("ac_sweep", settings.ACSweep, m_ACSweepRange, m_Schematic.GetCircuit());
}

void AnalysisSettingsPanel::DrawDCSweepSettings(SimulationSettings &settings) {
    const std::vector<SweepSource> sources = ListSweepSources(m_Schematic.GetCircuit());
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
    const Core::Circuit &circuit = m_Schematic.GetCircuit();
    const std::optional<int> output = ResolveOutputNode(settings.NoiseOutput, circuit, m_Schematic);
    if (!output) {
        ImGui::TextDisabled("Connect some parts to measure their noise");
        return;
    }
    DrawOutputPicker("##noise_output", settings.NoiseOutput, *output, circuit);
    DrawInputPicker("##noise_input", settings.NoiseSource, Core::GetNoiseInputSources(circuit), AC_INPUT_HINT);
    ImGui::SetItemTooltip("The noise is also referred to this source, as the noise it would need at its input");
    DrawLogarithmicSweep("noise", settings.NoiseRange, m_NoiseRange, circuit);
}

/**
 * @brief   The output picker lists every node by the label the plots give it, as the noise one does; the input picker
 *          lists every source a DC sweep could drive, whatever its waveform.
 */
void AnalysisSettingsPanel::DrawTransferFunctionSettings(SimulationSettings &settings) {
    DrawSourceAndOutputPickers(
        settings.TransferFunctionSource,
        settings.TransferFunctionOutput,
        "tf",
        "Connect some parts to measure their transfer function",
        "The DC gain, the input impedance and the output impedance from this source to the output");
}

/**
 * @brief   The frequency range of the S-parameter analysis, and the ports the circuit has.
 * @details The ports are the Block pin markers set as ports in their properties, so there is nothing to pick here.
 */
void AnalysisSettingsPanel::DrawSParameterSettings(SimulationSettings &settings) {
    Core::SParameterSettings &range = settings.SParameters;
    DrawFrequencyFields(m_SParameterRange, range.StartFrequency, range.StopFrequency);
    DrawFieldLabel("Sweep");
    if (ImGui::RadioButton("Log", range.IsLogarithmic)) {
        range.IsLogarithmic = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Lin", !range.IsLogarithmic)) {
        range.IsLogarithmic = false;
    }
    DrawFieldLabel("Points");
    // Files reject too few points for any backend, so the field never produces a value that would not load back
    if (range.IsLogarithmic) {
        DrawPointsPerDecade(range.PointsPerDecade);
    } else if (ImGui::InputInt("in all", &range.LinearPoints)) {
        range.LinearPoints =
            std::max(range.LinearPoints, Core::GetBackend().GetCapabilities().SParameters.MinLinearPoints);
    }
    DrawPortSummary();
}

/**
 * @brief   Lists the ports of the circuit, or says how to make them.
 */
void AnalysisSettingsPanel::DrawPortSummary() {
    std::vector<Core::CircuitPort> ports = m_Schematic.GetCircuit().GetPorts();
    std::ranges::sort(ports, {}, &Core::CircuitPort::Number);
    if (const std::optional<std::string> missing =
            Core::CheckSParameterPortCount(ports.size(), Core::GetBackend().GetCapabilities().SParameters)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
        ImGui::TextColored(GetErrorTextColor(), "%s", missing->c_str());
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
    DrawSourceAndOutputPickers(settings.PoleZeroSource,
                               settings.PoleZeroOutput,
                               "pole_zero",
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
void AnalysisSettingsPanel::DrawSourceAndOutputPickers(std::string &source,
                                                       std::optional<GridPoint> &output,
                                                       const std::string &id,
                                                       const char *empty_text,
                                                       const char *tooltip) {
    const Core::Circuit &circuit = m_Schematic.GetCircuit();
    const std::optional<int> output_node = ResolveOutputNode(output, circuit, m_Schematic);
    if (!output_node) {
        ImGui::TextDisabled("%s", empty_text);
        return;
    }
    DrawOutputPicker(std::format("##{}_output", id).c_str(), output, *output_node, circuit);
    DrawInputPicker(std::format("##{}_input", id).c_str(),
                    source,
                    Core::GetTransferFunctionInputSources(circuit),
                    std::format("Add {}", Core::SWEEPABLE_SOURCES));
    ImGui::SetItemTooltip("%s", tooltip);
}

/**
 * @brief   A combo over every node of the circuit, labelled as the plots label them; picking one stores a point of it,
 *          so the choice follows the node through edits.
 * @param[in]     id          Hidden label of the combo, which tells it from the combos of the other analyses.
 * @param[in,out] output      Stored output point; changed when the user picks another node.
 * @param[in]     shown_node  Node the analysis measures now, which the combo shows.
 * @param[in]     circuit     Circuit whose nodes are listed.
 */
void AnalysisSettingsPanel::DrawOutputPicker(const char *id,
                                             std::optional<GridPoint> &output,
                                             const int shown_node,
                                             const Core::Circuit &circuit) {
    const Connectivity &connectivity = m_Schematic.GetConnectivity();
    DrawFieldLabel("Output");
    if (!ImGui::BeginCombo(id, GetVoltageLabel(connectivity, shown_node).c_str())) {
        return;
    }
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        if (ImGui::Selectable(GetVoltageLabel(connectivity, node).c_str(), node == shown_node)) {
            output = connectivity.FindPointOfNode(node);
        }
    }
    ImGui::EndCombo();
}

/**
 * @brief   The kind, DC or AC, and the output picker, which lists every node by the label the plots give it, as the
 *          transfer function one does; the AC kind adds its input and its frequency range.
 * @note    The AC kind is disabled, with the reason as its tooltip, when the backend in use cannot run it.
 */
void AnalysisSettingsPanel::DrawSensitivitySettings(SimulationSettings &settings) {
    const Core::Circuit &circuit = m_Schematic.GetCircuit();
    const std::optional<int> output = ResolveOutputNode(settings.SensitivityOutput, circuit, m_Schematic);
    if (!output) {
        ImGui::TextDisabled("Connect some parts to measure their sensitivity");
        return;
    }
    const std::optional<std::string> ac_unsupported =
        Core::FindUnsupported(Core::GetBackend(), ListPartTypes(m_Schematic), Core::AnalysisKind::AC_SENSITIVITY);
    DrawFieldLabel("Kind");
    if (ImGui::RadioButton("DC", settings.SensitivityType == SensitivityKind::DC)) {
        settings.SensitivityType = SensitivityKind::DC;
    }
    ImGui::SetItemTooltip("How much the DC voltage of the output changes with the value of each part");
    ImGui::SameLine();
    ImGui::BeginDisabled(ac_unsupported.has_value());
    if (ImGui::RadioButton("AC", settings.SensitivityType == SensitivityKind::AC)) {
        settings.SensitivityType = SensitivityKind::AC;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s",
                          ac_unsupported ? ac_unsupported->c_str()
                                         : "How much the gain and the phase at the output change with the "
                                           "value of each part, across frequency");
    DrawOutputPicker("##sensitivity_output", settings.SensitivityOutput, *output, circuit);
    ImGui::SetItemTooltip("The voltage of this node is the output of the analysis");
    if (settings.SensitivityType == SensitivityKind::AC) {
        DrawACSensitivityInputs(settings, circuit);
    }
}

/**
 * @brief   The AC source that drives the circuit and the frequency range of the AC kind of the sensitivity analysis.
 */
void AnalysisSettingsPanel::DrawACSensitivityInputs(SimulationSettings &settings, const Core::Circuit &circuit) {
    DrawInputPicker("##ac_sensitivity_input",
                    settings.ACSensitivitySource,
                    Core::GetNoiseInputSources(circuit),
                    AC_INPUT_HINT);
    ImGui::SetItemTooltip("The response measured is the one to this source");
    DrawLogarithmicSweep("ac_sensitivity", settings.ACSensitivityRange, m_ACSensitivityRange, circuit);
}

/**
 * @brief   One checkbox per logic input, and the nodes the table reads, which are the voltages already measured.
 * @details Nothing stored means every input, and a change stores the whole list.
 */
void AnalysisSettingsPanel::DrawTruthTableSettings(SimulationSettings &settings) {
    const Core::Circuit &circuit = m_Schematic.GetCircuit();
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
        if (std::optional<std::vector<std::string>> changed =
                ToggleTruthTableInput(available, chosen, name, is_chosen)) {
            settings.TruthTableInputs = std::move(*changed);
        }
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
    ImGui::TextDisabled("Reads %s", FormatMeasuredNodeLabels(m_Schematic.GetConnectivity(), outputs).c_str());
    ImGui::SetItemTooltip("The nodes you measure with the Probe tool; every combination of the inputs is simulated and "
                          "each of these is read as 0, 1 or X");
}

/**
 * @brief   The fundamental, the number of harmonics and the time samples per period, and a button that takes the
 *          fundamental and the harmonics from the periodic sources of the circuit.
 * @details An explicit sample count stays above twice the harmonics, so a harmonic never aliases onto another one.
 */
void AnalysisSettingsPanel::DrawHarmonicBalanceSettings(SimulationSettings &settings) {
    Core::HarmonicBalanceSettings &harmonic_balance = settings.HarmonicBalance;
    if (const std::optional<double> fundamental = m_HBFundamental.Draw("Fundamental", "Hz", IsPositive)) {
        harmonic_balance.FundamentalFrequency = *fundamental;
    }
    DrawFieldLabel("Harmonics");
    if (ImGui::InputInt("##hb_harmonics", &harmonic_balance.Harmonics)) {
        harmonic_balance.Harmonics = std::clamp(harmonic_balance.Harmonics, 1, Core::MAX_HARMONIC_BALANCE_HARMONICS);
    }
    ImGui::SetItemTooltip("Number of harmonics above DC (1 to %d)", Core::MAX_HARMONIC_BALANCE_HARMONICS);
    harmonic_balance.Samples = NormalizeSampleCount(harmonic_balance.Samples, harmonic_balance.Harmonics);
    DrawFieldLabel("Samples");
    const std::string samples_label =
        harmonic_balance.Samples == 0 ? "Auto" : std::format("{}", harmonic_balance.Samples);
    if (ImGui::BeginCombo("##hb_samples", samples_label.c_str())) {
        if (ImGui::Selectable("Auto", harmonic_balance.Samples == 0)) {
            harmonic_balance.Samples = 0;
        }
        ImGui::SetItemTooltip("The smallest power of two of at least 16 and 4 times the harmonics");
        for (const int candidate : ListSampleCounts(harmonic_balance.Harmonics)) {
            const std::string label = std::format("{}", candidate);
            if (ImGui::Selectable(label.c_str(), harmonic_balance.Samples == candidate)) {
                harmonic_balance.Samples = candidate;
            }
        }
        ImGui::EndCombo();
    }
    const auto suggested = Core::SuggestHarmonicBalanceSettings(m_Schematic.GetCircuit());
    if (DrawSuggestButton("From sources",
                          suggested.has_value(),
                          "Set the fundamental frequency and harmonic count from the circuit's periodic sources",
                          "Needs a sine, AC, pulse or clock source with commensurable frequencies")
        && suggested) {
        harmonic_balance.FundamentalFrequency = suggested->FundamentalFrequency;
        harmonic_balance.Harmonics = suggested->Harmonics;
        harmonic_balance.Samples = suggested->Samples;
        m_HBFundamental.Load(suggested->FundamentalFrequency);
    }
}

/**
 * @brief   The numerical options every analysis shares, which both engines honour: the tolerances, the integration
 *          method of a transient and the temperature the models are given at.
 * @details The section starts closed, since the defaults suit most circuits; each field says its default in its
 *          tooltip, and Reset puts them all back.
 */
void AnalysisSettingsPanel::DrawSolverSettings(Core::SolverOptions &options) {
    ImGui::Separator();
    ImGui::PushID("solver");
    if (ImGui::CollapsingHeader("Solver")) {
        const auto is_between = [](const double minimum, const double maximum) {
            return [minimum, maximum](const double value) { return value >= minimum && value <= maximum; };
        };
        if (const std::optional<double> value =
                m_RelativeTolerance.Draw("Rel. tolerance",
                                         "",
                                         is_between(Core::MIN_RELATIVE_TOLERANCE, Core::MAX_RELATIVE_TOLERANCE))) {
            options.RelativeTolerance = *value;
        }
        ImGui::SetItemTooltip("%s",
                              std::format("reltol: how close two Newton iterations must be, relative to the "
                                          "value, and how fast a transient may change its step; default {}, "
                                          "{} to {}",
                                          Core::FormatValue(Core::DEFAULT_RELATIVE_TOLERANCE),
                                          Core::FormatValue(Core::MIN_RELATIVE_TOLERANCE),
                                          Core::FormatValue(Core::MAX_RELATIVE_TOLERANCE))
                                  .c_str());
        if (const std::optional<double> value = m_CurrentTolerance.Draw(
                "Current tolerance",
                "A",
                is_between(Core::MIN_CURRENT_TOLERANCE_AMPERES, Core::MAX_CURRENT_TOLERANCE_AMPERES))) {
            options.CurrentToleranceAmperes = *value;
        }
        ImGui::SetItemTooltip("%s",
                              std::format("abstol: the smallest current that counts; default {}A, {}A to {}A",
                                          Core::FormatValue(Core::DEFAULT_CURRENT_TOLERANCE_AMPERES),
                                          Core::FormatValue(Core::MIN_CURRENT_TOLERANCE_AMPERES),
                                          Core::FormatValue(Core::MAX_CURRENT_TOLERANCE_AMPERES))
                                  .c_str());
        if (const std::optional<double> value = m_VoltageTolerance.Draw(
                "Voltage tolerance",
                "V",
                is_between(Core::MIN_VOLTAGE_TOLERANCE_VOLTS, Core::MAX_VOLTAGE_TOLERANCE_VOLTS))) {
            options.VoltageToleranceVolts = *value;
        }
        ImGui::SetItemTooltip("%s",
                              std::format("vntol: the smallest voltage that counts; default {}V, {}V to {}V",
                                          Core::FormatValue(Core::DEFAULT_VOLTAGE_TOLERANCE_VOLTS),
                                          Core::FormatValue(Core::MIN_VOLTAGE_TOLERANCE_VOLTS),
                                          Core::FormatValue(Core::MAX_VOLTAGE_TOLERANCE_VOLTS))
                                  .c_str());
        DrawFieldLabel("Integration");
        if (ImGui::BeginCombo("##method", INTEGRATION_METHOD_NAMES[static_cast<std::size_t>(options.Method)])) {
            for (std::size_t index = 0; index < INTEGRATION_METHOD_NAMES.size(); ++index) {
                const auto candidate = static_cast<Core::IntegrationMethod>(index);
                if (ImGui::Selectable(INTEGRATION_METHOD_NAMES[index], candidate == options.Method)) {
                    options.Method = candidate;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("How a transient steps capacitors and inductors in time. Default: trapezoidal, or Gear "
                              "when the circuit has a fuse. Gear damps the ringing the trapezoidal rule leaves after "
                              "a sudden change, at a little more error");
        if (const std::optional<double> value = m_NominalTemperature.Draw(
                "Nominal temp.",
                "C",
                is_between(Core::MIN_NOMINAL_TEMPERATURE_CELSIUS, Core::MAX_NOMINAL_TEMPERATURE_CELSIUS))) {
            options.NominalTemperatureCelsius = *value;
        }
        ImGui::SetItemTooltip("%s",
                              std::format("tnom: the temperature the model parameters are given at; a run at "
                                          "another temperature scales them from it; default {} C",
                                          Core::FormatValue(Core::DEFAULT_NOMINAL_TEMPERATURE_CELSIUS))
                                  .c_str());
        ImGui::BeginDisabled(options == Core::SolverOptions{});
        if (ImGui::Button("Reset to defaults")) {
            options = {};
            LoadSolverFields(options);
        }
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

void AnalysisSettingsPanel::LoadSolverFields(const Core::SolverOptions &options) {
    m_RelativeTolerance.Load(options.RelativeTolerance);
    m_CurrentTolerance.Load(options.CurrentToleranceAmperes);
    m_VoltageTolerance.Load(options.VoltageToleranceVolts);
    m_NominalTemperature.Load(options.NominalTemperatureCelsius);
}

} // namespace GUI
