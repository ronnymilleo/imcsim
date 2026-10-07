/**
 * @file    simulation_window.cpp
 * @brief   Window that runs the schematic through ngspice and shows the results and its output.
 */

#include "simulation_window.h"

#include "spice_value.h"
#include "theme.h"
#include <algorithm>
#include <format>
#include <utility>

namespace GUI {

namespace {

bool IsPositive(const double value) {
    return value > 0.0;
}

bool AnyValue(double /*value*/) {
    return true;
}

// Shows why there is nothing to plot, or a warning when ngspice complained; returns whether to show the results
bool DrawRunStatus(const std::optional<std::string> &error, const std::vector<Core::SimulatorMessage> &messages,
                   const bool has_result) {
    if (error) {
        ImGui::TextColored(GetErrorTextColor(), "%s", error->c_str());
        return false;
    }
    if (!has_result) {
        ImGui::TextDisabled("No results for the current circuit; run the simulation");
        return false;
    }
    const bool has_warnings =
        std::ranges::any_of(messages, [](const Core::SimulatorMessage &message) { return message.FromErrorStream; });
    if (has_warnings) {
        ImGui::TextColored(GetWarningTextColor(), "ngspice reported warnings; check its output before trusting the "
                                                  "results");
    }
    return true;
}

void DrawResultWarnings(const std::vector<std::string> &warnings) {
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string &warning : warnings) {
        ImGui::TextColored(GetWarningTextColor(), "%s", warning.c_str());
    }
    ImGui::PopTextWrapPos();
}

void DrawOutput(const std::vector<Core::SimulatorMessage> &messages) {
    if (messages.empty() || !ImGui::CollapsingHeader("ngspice output")) {
        return;
    }
    for (const Core::SimulatorMessage &message : messages) {
        if (message.FromErrorStream) {
            ImGui::TextColored(GetWarningTextColor(), "%s", message.Text.c_str());
        } else {
            ImGui::TextUnformatted(message.Text.c_str());
        }
    }
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

bool IsNonZero(const double value) {
    return value != 0.0;
}

// Draws the source picker and the start, stop and step of one range. A range without a source, or with one that
// left the circuit, takes the source at preferred_index, so a new schematic is ready to run
void DrawSweepRange(const char *id, const char *source_label, const std::vector<SweepSource> &sources,
                    const std::size_t preferred_index, Core::SweepRange &range, std::array<ValueField, 3> &fields) {
    ImGui::PushID(id);
    const auto selected = std::ranges::find(sources, range.Source, &SweepSource::Name);
    if (selected == sources.end() && !sources.empty()) {
        range.Source = sources[std::min(preferred_index, sources.size() - 1)].Name;
    }
    DrawFieldLabel(source_label);
    if (ImGui::BeginCombo("##source", range.Source.empty() ? "(no source)" : range.Source.c_str())) {
        for (const SweepSource &source : sources) {
            if (ImGui::Selectable(source.Name.c_str(), source.Name == range.Source)) {
                range.Source = source.Name;
            }
        }
        ImGui::EndCombo();
    }
    const auto current = std::ranges::find(sources, range.Source, &SweepSource::Name);
    const char *unit = current != sources.end() ? current->Unit : "V";
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

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic to simulate; it must outlive the window.
 */
SimulationWindow::SimulationWindow(Schematic &schematic) : AppWindow("Simulation", true), m_Schematic(schematic) {
    m_StopTime.Load(m_TransientSettings.StopTime);
    m_TimeStep.Load(m_TransientSettings.TimeStep);
    m_StartFrequency.Load(m_ACSweepSettings.StartFrequency);
    m_StopFrequency.Load(m_ACSweepSettings.StopFrequency);
    LoadSweepFields(m_SweptRange, m_SweptFields);
    LoadSweepFields(m_SteppedRange, m_SteppedFields);
}

void SimulationWindow::Draw() {
    if (!ImGui::BeginTabBar("analyses")) {
        return;
    }
    if (ImGui::BeginTabItem("Operating point")) {
        DrawOperatingPointTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Transient")) {
        DrawTransientTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("AC sweep")) {
        DrawACSweepTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("DC sweep")) {
        DrawDCSweepTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void SimulationWindow::DrawOperatingPointTab() {
    if (PrimaryButton("Run operating point (.op)")) {
        RunOperatingPoint();
    }
    const auto &operating_point = m_Schematic.GetOperatingPoint();
    if (DrawRunStatus(m_OperatingPointStatus.Error, m_OperatingPointStatus.Messages, operating_point.has_value())) {
        DrawResultWarnings(operating_point->Warnings);
        DrawNodeVoltages(*operating_point);
        DrawCurrents(*operating_point);
    }
    DrawOutput(m_OperatingPointStatus.Messages);
}

// Settings change as soon as a field is valid; ngspice checks how they relate only when the analysis runs
void SimulationWindow::DrawTransientTab() {
    if (const std::optional<double> stop_time = m_StopTime.Draw("Stop time", "s", IsPositive)) {
        m_TransientSettings.StopTime = *stop_time;
    }
    if (const std::optional<double> time_step = m_TimeStep.Draw("Time step", "s", IsPositive)) {
        m_TransientSettings.TimeStep = *time_step;
    }
    if (PrimaryButton("Run transient (.tran)")) {
        RunTransient();
    }
    const auto &transient = m_Schematic.GetTransient();
    if (DrawRunStatus(m_TransientStatus.Error, m_TransientStatus.Messages, transient.has_value())) {
        DrawResultWarnings(transient->Warnings);
        ImGui::TextUnformatted("Done; the voltages and currents are in the Output window");
    }
    DrawOutput(m_TransientStatus.Messages);
}

void SimulationWindow::DrawACSweepTab() {
    if (const std::optional<double> start = m_StartFrequency.Draw("Start", "Hz", IsPositive)) {
        m_ACSweepSettings.StartFrequency = *start;
    }
    if (const std::optional<double> stop = m_StopFrequency.Draw("Stop", "Hz", IsPositive)) {
        m_ACSweepSettings.StopFrequency = *stop;
    }
    DrawFieldLabel("Points");
    ImGui::InputInt("per decade", &m_ACSweepSettings.PointsPerDecade);
    if (PrimaryButton("Run AC sweep (.ac)")) {
        RunACSweep();
    }
    const auto &sweep = m_Schematic.GetACSweep();
    if (DrawRunStatus(m_ACSweepStatus.Error, m_ACSweepStatus.Messages, sweep.has_value())) {
        ImGui::TextUnformatted("Done; the Bode plot is in the Output window");
    }
    DrawOutput(m_ACSweepStatus.Messages);
}

// A rejected run also clears the previous result, so stale numbers never sit next to an error
// The stepped source defaults to the second one, so a transistor curve family needs no picking
void SimulationWindow::DrawDCSweepTab() {
    const std::vector<SweepSource> sources = ListSweepSources(m_Schematic.BuildCircuit());
    if (sources.empty()) {
        ImGui::TextDisabled("Add a voltage source, current source or VCC to sweep");
        return;
    }
    DrawSweepRange("swept", "Sweep", sources, 0, m_SweptRange, m_SweptFields);
    ImGui::Checkbox("Step a second source, one curve per value", &m_StepSource);
    if (m_StepSource) {
        DrawSweepRange("stepped", "Step", sources, 1, m_SteppedRange, m_SteppedFields);
    }
    ImGui::TextDisabled("AC and pulse sources are swept through their DC value");
    if (PrimaryButton("Run DC sweep (.dc)")) {
        RunDCSweep();
    }
    const auto &sweep = m_Schematic.GetDCSweep();
    if (DrawRunStatus(m_DCSweepStatus.Error, m_DCSweepStatus.Messages, sweep.has_value())) {
        ImGui::TextUnformatted("Done; the curves are in the Output window");
    }
    DrawOutput(m_DCSweepStatus.Messages);
}

void SimulationWindow::RunOperatingPoint() {
    Core::OperatingPointRun run = Core::RunOperatingPoint(m_Schematic.BuildCircuit());
    m_OperatingPointStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_OperatingPointStatus.Error.reset();
        m_Schematic.SetOperatingPoint(std::move(*run.Result));
    } else {
        m_OperatingPointStatus.Error = std::move(run.Result.error());
        m_Schematic.SetOperatingPoint(std::nullopt);
    }
}

void SimulationWindow::RunTransient() {
    Core::TransientRun run = Core::RunTransient(m_Schematic.BuildCircuit(), m_TransientSettings);
    m_TransientStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_TransientStatus.Error.reset();
        m_Schematic.SetTransient(std::move(*run.Result));
    } else {
        m_TransientStatus.Error = std::move(run.Result.error());
        m_Schematic.SetTransient(std::nullopt);
    }
}

void SimulationWindow::RunACSweep() {
    Core::ACSweepRun run = Core::RunACSweep(m_Schematic.BuildCircuit(), m_ACSweepSettings);
    m_ACSweepStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_ACSweepStatus.Error.reset();
        m_Schematic.SetACSweep(std::move(*run.Result));
    } else {
        m_ACSweepStatus.Error = std::move(run.Result.error());
        m_Schematic.SetACSweep(std::nullopt);
    }
}

void SimulationWindow::RunDCSweep() {
    Core::DCSweepSettings settings{.Swept = m_SweptRange};
    if (m_StepSource) {
        settings.Stepped = m_SteppedRange;
    }
    Core::DCSweepRun run = Core::RunDCSweep(m_Schematic.BuildCircuit(), settings);
    m_DCSweepStatus.Messages = std::move(run.Messages);
    if (run.Result) {
        m_DCSweepStatus.Error.reset();
        m_Schematic.SetDCSweep(std::move(*run.Result));
    } else {
        m_DCSweepStatus.Error = std::move(run.Result.error());
        m_Schematic.SetDCSweep(std::nullopt);
    }
}

void SimulationWindow::DrawNodeVoltages(const Core::OperatingPoint &operating_point) const {
    if (!ImGui::BeginTable("node_voltages", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Node");
    ImGui::TableSetupColumn("Voltage");
    ImGui::TableHeadersRow();
    for (std::size_t node = 0; node < operating_point.NodeVoltages.size(); ++node) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(node == 0 ? "0 (ground)" : std::format("{}", node).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(std::format("{}V", Core::FormatValue(operating_point.NodeVoltages[node])).c_str());
    }
    ImGui::EndTable();
}

void SimulationWindow::DrawCurrents(const Core::OperatingPoint &operating_point) const {
    if (operating_point.Currents.empty() ||
        !ImGui::BeginTable("currents", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return;
    }
    ImGui::TableSetupColumn("Component");
    ImGui::TableSetupColumn("Current");
    ImGui::TableHeadersRow();
    for (const Core::ComponentCurrent &current : operating_point.Currents) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(current.Name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(std::format("{}A", Core::FormatValue(current.Current)).c_str());
    }
    ImGui::EndTable();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("As in SPICE: positive through a two-terminal part from its first terminal to its second, "
                        "and into each terminal of a transistor");
    ImGui::PopTextWrapPos();
}

} // namespace GUI
