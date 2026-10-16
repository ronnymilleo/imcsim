/**
 * @file    analysis_controls.cpp
 * @brief   Settings and runs of the analyses, shared by the editor toolbar and the Analysis Settings window.
 */

#include "analysis_controls.h"

#include "analysis_suggestions.h"
#include "imgui.h"
#include "probing.h"
#include "theme.h"
#include <algorithm>
#include <expected>
#include <utility>

namespace GUI {

namespace {

constexpr auto AnalysisNames =
    std::to_array<const char *>({"Operating point", "Transient", "AC sweep", "DC sweep", "Noise"});

bool IsPositive(const double value) {
    return value > 0.0;
}

bool AnyValue(double /*value*/) {
    return true;
}

bool IsNonZero(const double value) {
    return value != 0.0;
}

std::size_t ToIndex(const Analysis analysis) {
    return static_cast<std::size_t>(analysis);
}

/**
 * @struct  SweepSource
 * @brief   A source a DC sweep can drive, and the unit of its values.
 */
struct SweepSource {
    std::string Name;
    const char *Unit;
};

std::vector<SweepSource> ListSweepSources(const Core::Circuit &circuit) {
    std::vector<SweepSource> sources;
    for (const std::string &name : Core::GetSweepableSources(circuit)) {
        const auto entry = std::ranges::find_if(circuit.GetEntries(), [&name](const Core::CircuitEntry &candidate) {
            return candidate.Part->GetName() == name;
        });
        sources.push_back({name, entry->Part->GetType() == Core::ComponentType::CurrentSource ? "A" : "V"});
    }
    return sources;
}

void LoadSweepFields(const Core::SweepRange &range, std::array<ValueField, 3> &fields) {
    fields[0].Load(range.Start);
    fields[1].Load(range.Stop);
    fields[2].Load(range.Step);
}

// The stepped source defaults to the second one, so a transistor curve family needs no picking
constexpr std::size_t SweptSourceIndex = 0;
constexpr std::size_t SteppedSourceIndex = 1;

// A range without a source, or with one that left the circuit, uses the source at preferred_index, so a new
// schematic is ready to run. The stored name is kept, so the choice comes back with its source
const std::string &ResolveSweepSource(const Core::SweepRange &range, const std::vector<SweepSource> &sources,
                                      const std::size_t preferred_index) {
    if (std::ranges::contains(sources, range.Source, &SweepSource::Name)) {
        return range.Source;
    }
    return sources[std::min(preferred_index, sources.size() - 1)].Name;
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

// Hands a result to the schematic, or clears the old one so stale values never sit next to an error
template <typename Result, typename Store>
std::optional<std::string> StoreResult(std::expected<Result, std::string> result, Store store) {
    if (result) {
        store(std::optional<Result>(std::move(*result)));
        return std::nullopt;
    }
    store(std::optional<Result>());
    return std::move(result.error());
}

// A source that is empty, or that left the circuit or its AC setting, falls back to the first source that can be
// the input; sources must not be empty
const std::string &ResolveNoiseSource(const std::string &stored, const std::vector<std::string> &sources) {
    return std::ranges::contains(sources, stored) ? stored : sources.front();
}

// The node the noise is measured at: the one under the stored point, or else the first measured voltage away from
// the input source, which is most likely the output the user looks at, or else any measured voltage, or else the
// last node. No value without a node to measure
std::optional<int> ResolveNoiseOutput(const std::optional<GridPoint> &point, const std::string &input_source,
                                      const Core::Circuit &circuit, Schematic &schematic) {
    if (point) {
        if (const std::optional<int> node = schematic.GetConnectivity().GetNode(*point); node && *node > 0) {
            return node;
        }
    }
    std::vector<int> input_nodes;
    for (const Core::CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetName() == input_source) {
            input_nodes = entry.Nodes;
        }
    }
    std::optional<int> any_measured;
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        if (!schematic.IsVoltageMeasured(node)) {
            continue;
        }
        if (!std::ranges::contains(input_nodes, node)) {
            return node;
        }
        any_measured = any_measured.value_or(node);
    }
    if (any_measured) {
        return any_measured;
    }
    return circuit.GetNodeCount() > 1 ? std::optional<int>(circuit.GetNodeCount() - 1) : std::nullopt;
}

// The input the noise analysis runs with, or an empty name when no source can be one
std::string ResolveNoiseInput(const std::string &stored, const std::vector<std::string> &sources) {
    return sources.empty() ? std::string() : ResolveNoiseSource(stored, sources);
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
 * @brief   Creates the controls for a schematic.
 * @param[in] schematic  Schematic whose analyses are set and run; it must outlive the controls.
 */
AnalysisControls::AnalysisControls(Schematic &schematic) : m_Schematic(schematic) {
    LoadSettingsFields();
}

/**
 * @brief   Draws one radio button per analysis, in a column, which picks the one Run starts.
 */
void AnalysisControls::DrawAnalysisPicker() {
    SimulationSettings settings = m_Schematic.GetSimulationSettings();
    for (std::size_t index = 0; index < AnalysisNames.size(); ++index) {
        if (ImGui::RadioButton(AnalysisNames[index], ToIndex(settings.Selected) == index)) {
            settings.Selected = static_cast<Analysis>(index);
        }
    }
    m_Schematic.SetSimulationSettings(settings);
}

/**
 * @brief   Draws the settings of the selected analysis; the transient and the AC sweep add a Suggest button.
 * @note    The fields edit a copy of the settings, which goes back to the schematic once, at the end.
 */
void AnalysisControls::DrawSettings() {
    if (m_LoadedSettingsVersion != m_Schematic.GetSimulationSettingsVersion()) {
        LoadSettingsFields();
    }
    SimulationSettings settings = m_Schematic.GetSimulationSettings();
    switch (settings.Selected) {
    case Analysis::OperatingPoint:
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
        ImGui::TextDisabled("No settings: the DC voltages show on the schematic and, with the currents, in the "
                            "Output window");
        ImGui::PopTextWrapPos();
        break;
    case Analysis::Transient:
        DrawTransientSettings(settings);
        break;
    case Analysis::ACSweep:
        DrawACSweepSettings(settings);
        break;
    case Analysis::DCSweep:
        DrawDCSweepSettings(settings);
        break;
    case Analysis::Noise:
        DrawNoiseSettings(settings);
        break;
    }
    m_Schematic.SetSimulationSettings(settings);
}

/**
 * @brief   Runs the selected analysis with its settings and stores the result, or the error, in the schematic.
 * @note    ngspice runs synchronously, so the frame waits for it.
 */
void AnalysisControls::RunSelected() {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    const Analysis analysis = settings.Selected;
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    switch (analysis) {
    case Analysis::OperatingPoint: {
        Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
        Finish(analysis,
               StoreResult(std::move(run.Result),
                           [this](auto result) { m_Schematic.SetOperatingPoint(std::move(result)); }),
               std::move(run.Messages));
        break;
    }
    case Analysis::Transient: {
        Core::TransientRun run = Core::RunTransient(circuit, settings.Transient);
        Finish(analysis,
               StoreResult(std::move(run.Result), [this](auto result) { m_Schematic.SetTransient(std::move(result)); }),
               std::move(run.Messages));
        break;
    }
    case Analysis::ACSweep: {
        Core::ACSweepRun run = Core::RunACSweep(circuit, settings.ACSweep);
        Finish(analysis,
               StoreResult(std::move(run.Result), [this](auto result) { m_Schematic.SetACSweep(std::move(result)); }),
               std::move(run.Messages));
        break;
    }
    case Analysis::DCSweep: {
        const std::vector<SweepSource> sources = ListSweepSources(circuit);
        if (sources.empty()) {
            m_Schematic.SetDCSweep(std::nullopt);
            Finish(analysis, "Add a voltage source, current source or VCC to sweep", {});
            break;
        }
        Core::DCSweepSettings run_settings{.Swept = settings.SweptRange};
        run_settings.Swept.Source = ResolveSweepSource(settings.SweptRange, sources, SweptSourceIndex);
        if (settings.StepSource) {
            run_settings.Stepped = settings.SteppedRange;
            run_settings.Stepped->Source = ResolveSweepSource(settings.SteppedRange, sources, SteppedSourceIndex);
        }
        Core::DCSweepRun run = Core::RunDCSweep(circuit, run_settings);
        Finish(analysis,
               StoreResult(std::move(run.Result), [this](auto result) { m_Schematic.SetDCSweep(std::move(result)); }),
               std::move(run.Messages));
        break;
    }
    case Analysis::Noise: {
        const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
        const std::string input = ResolveNoiseInput(settings.NoiseSource, sources);
        const std::optional<int> output = ResolveNoiseOutput(settings.NoiseOutput, input, circuit, m_Schematic);
        if (sources.empty() || !output) {
            m_Schematic.SetNoise(std::nullopt);
            Finish(analysis,
                   sources.empty() ? "A noise analysis needs an AC source as its input; select a voltage or current "
                                     "source and set it to AC"
                                   : "Connect some parts: the noise needs a node to measure",
                   {});
            break;
        }
        const Core::NoiseSettings run_settings{.OutputNode = *output,
                                               .InputSource = input,
                                               .StartFrequency = settings.NoiseRange.StartFrequency,
                                               .StopFrequency = settings.NoiseRange.StopFrequency,
                                               .PointsPerDecade = settings.NoiseRange.PointsPerDecade};
        Core::NoiseRun run = Core::RunNoise(circuit, run_settings);
        Finish(analysis,
               StoreResult(std::move(run.Result), [this](auto result) { m_Schematic.SetNoise(std::move(result)); }),
               std::move(run.Messages));
        break;
    }
    }
}

/**
 * @brief   Returns the analysis that ran last.
 * @return  The analysis, or no value before the first run.
 */
std::optional<Analysis> AnalysisControls::GetLastRun() const {
    return m_LastRun;
}

/**
 * @brief   Tells how the last run of an analysis went.
 * @param[in] analysis  Analysis to check.
 * @return  Failed while its last run has an error; with warnings when the result or ngspice reported some; None
 *          when it has no result, such as after the circuit changed.
 */
RunOutcome AnalysisControls::GetOutcome(const Analysis analysis) const {
    const RunStatus &status = m_Statuses[ToIndex(analysis)];
    if (status.Error) {
        return RunOutcome::Failed;
    }
    if (!HasResult(analysis)) {
        return RunOutcome::None;
    }
    const bool ngspice_warned = std::ranges::any_of(
        status.Messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
    return ngspice_warned || !GetResultWarnings(analysis).empty() ? RunOutcome::SucceededWithWarnings
                                                                  : RunOutcome::Succeeded;
}

/**
 * @brief   Returns why the last run of an analysis failed.
 * @param[in] analysis  Analysis to check.
 * @return  The error, or no value when the last run succeeded or there was none.
 */
const std::optional<std::string> &AnalysisControls::GetError(const Analysis analysis) const {
    return m_Statuses[ToIndex(analysis)].Error;
}

/**
 * @brief   Draws what the last run of an analysis reported: its error or its warnings, and the ngspice output.
 * @param[in] analysis  Analysis whose last run is shown.
 */
void AnalysisControls::DrawRunReport(const Analysis analysis) const {
    const RunStatus &status = m_Statuses[ToIndex(analysis)];
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 30.0f);
    if (status.Error) {
        ImGui::TextColored(GetErrorTextColor(), "%s", status.Error->c_str());
    } else if (HasResult(analysis)) {
        ImGui::Text("%s done", GetAnalysisName(analysis));
        for (const std::string &warning : GetResultWarnings(analysis)) {
            ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
        }
        if (std::ranges::any_of(status.Messages,
                                [](const Core::SimulatorMessage &message) { return message.FromErrorStream; })) {
            ImGui::TextColored(GetWarningTextColor(), "ngspice reported warnings; check its output before trusting "
                                                      "the results");
        }
    } else {
        ImGui::TextDisabled("No results for the current circuit");
    }
    ImGui::PopTextWrapPos();
    if (status.Messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : status.Messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(GetWarningTextColor(), "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
}

void AnalysisControls::LoadSettingsFields() {
    const SimulationSettings &settings = m_Schematic.GetSimulationSettings();
    m_StopTime.Load(settings.Transient.StopTime);
    m_TimeStep.Load(settings.Transient.TimeStep);
    m_StartFrequency.Load(settings.ACSweep.StartFrequency);
    m_StopFrequency.Load(settings.ACSweep.StopFrequency);
    LoadSweepFields(settings.SweptRange, m_SweptFields);
    LoadSweepFields(settings.SteppedRange, m_SteppedFields);
    m_NoiseStartFrequency.Load(settings.NoiseRange.StartFrequency);
    m_NoiseStopFrequency.Load(settings.NoiseRange.StopFrequency);
    m_LoadedSettingsVersion = m_Schematic.GetSimulationSettingsVersion();
}

// Settings change as soon as a field is valid; ngspice checks how they relate only when the analysis runs
void AnalysisControls::DrawTransientSettings(SimulationSettings &settings) {
    if (const std::optional<double> stop_time = m_StopTime.Draw("Stop time", "s", IsPositive)) {
        settings.Transient.StopTime = *stop_time;
    }
    if (const std::optional<double> time_step = m_TimeStep.Draw("Time step", "s", IsPositive)) {
        settings.Transient.TimeStep = *time_step;
    }
    const auto suggested = Core::SuggestTransientSettings(m_Schematic.BuildCircuit());
    if (DrawSuggestButton(suggested.has_value(),
                          "Five periods of the slowest source, 200 points in a period of the fastest",
                          "Needs a sine or pulse source")) {
        settings.Transient = *suggested;
        m_StopTime.Load(suggested->StopTime);
        m_TimeStep.Load(suggested->TimeStep);
    }
}

void AnalysisControls::DrawACSweepSettings(SimulationSettings &settings) {
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
                          "Needs a capacitor, an inductor or an op-amp")) {
        settings.ACSweep.StartFrequency = suggested->StartFrequency;
        settings.ACSweep.StopFrequency = suggested->StopFrequency;
        m_StartFrequency.Load(suggested->StartFrequency);
        m_StopFrequency.Load(suggested->StopFrequency);
    }
}

void AnalysisControls::DrawDCSweepSettings(SimulationSettings &settings) {
    const std::vector<SweepSource> sources = ListSweepSources(m_Schematic.BuildCircuit());
    if (sources.empty()) {
        ImGui::TextDisabled("Add a voltage source, current source or VCC to sweep");
        return;
    }
    DrawSweepRange("swept", "Sweep", sources, SweptSourceIndex, settings.SweptRange, m_SweptFields);
    ImGui::Checkbox("Step a second source, one curve per value", &settings.StepSource);
    if (settings.StepSource) {
        DrawSweepRange("stepped", "Step", sources, SteppedSourceIndex, settings.SteppedRange, m_SteppedFields);
    }
    ImGui::TextDisabled("AC and pulse sources are swept through their DC value");
}

// The output picker lists every node by the label the plots give it; picking one keeps a point of it, so the choice
// follows the node through edits
void AnalysisControls::DrawNoiseSettings(SimulationSettings &settings) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    const std::vector<std::string> sources = Core::GetNoiseInputSources(circuit);
    const std::string input = ResolveNoiseInput(settings.NoiseSource, sources);
    const std::optional<int> output = ResolveNoiseOutput(settings.NoiseOutput, input, circuit, m_Schematic);
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
                          "Needs a capacitor, an inductor or an op-amp")) {
        settings.NoiseRange.StartFrequency = suggested->StartFrequency;
        settings.NoiseRange.StopFrequency = suggested->StopFrequency;
        m_NoiseStartFrequency.Load(suggested->StartFrequency);
        m_NoiseStopFrequency.Load(suggested->StopFrequency);
    }
}

void AnalysisControls::Finish(const Analysis analysis, std::optional<std::string> error,
                              std::vector<Core::SimulatorMessage> messages) {
    m_Statuses[ToIndex(analysis)] = {.Error = std::move(error), .Messages = std::move(messages)};
    m_LastRun = analysis;
}

// Rating warnings, such as a transistor past its maximum voltage; only the operating point and the transient check
std::vector<std::string> AnalysisControls::GetResultWarnings(const Analysis analysis) const {
    if (analysis == Analysis::OperatingPoint && m_Schematic.GetOperatingPoint()) {
        return m_Schematic.GetOperatingPoint()->Warnings;
    }
    if (analysis == Analysis::Transient && m_Schematic.GetTransient()) {
        return m_Schematic.GetTransient()->Warnings;
    }
    return {};
}

bool AnalysisControls::HasResult(const Analysis analysis) const {
    switch (analysis) {
    case Analysis::OperatingPoint:
        return m_Schematic.GetOperatingPoint().has_value();
    case Analysis::Transient:
        return m_Schematic.GetTransient().has_value();
    case Analysis::ACSweep:
        return m_Schematic.GetACSweep().has_value();
    case Analysis::DCSweep:
        return m_Schematic.GetDCSweep().has_value();
    case Analysis::Noise:
        return m_Schematic.GetNoise().has_value();
    }
    return false;
}

/**
 * @brief   Returns the name of an analysis, as menus and messages show it.
 * @param[in] analysis  Analysis to name.
 * @return  Such as "Operating point" or "AC sweep".
 */
const char *GetAnalysisName(const Analysis analysis) {
    return AnalysisNames[ToIndex(analysis)];
}

} // namespace GUI
