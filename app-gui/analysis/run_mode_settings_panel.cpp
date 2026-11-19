/**
 * @file    run_mode_settings_panel.cpp
 * @brief   The run mode of the analyses that repeat, with the fields of a parameter step and of a Monte Carlo run.
 */

#include "analysis/run_mode_settings_panel.h"

#include "analysis/analysis_inputs.h"
#include "base/theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "results/parameter_step.h"
#include "simulation/simulation_backend.h"
#include "simulation/stepped_circuit.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <format>
#include <memory>
#include <random>
#include <vector>

namespace GUI {

namespace {

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

/// The kinds of part whose tolerance the buttons of Monte Carlo set all at once.
constexpr std::array<Core::ComponentType, 3> TOLERANCE_TYPES = {Core::ComponentType::RESISTOR,
                                                                Core::ComponentType::CAPACITOR,
                                                                Core::ComponentType::INDUCTOR};

/// The label of the button that sets the tolerance of each kind in TOLERANCE_TYPES.
constexpr std::array<const char *, 3> TOLERANCE_TYPE_LABELS = {"Resistors", "Capacitors", "Inductors"};

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
        ImGui::TextDisabled("%s",
                            std::format("{} runs, {} to {}",
                                        values->size(),
                                        FormatStepValue(parameter, values->front(), unit),
                                        FormatStepValue(parameter, values->back(), unit))
                                .c_str());
    }
    ImGui::PopTextWrapPos();
}

} // namespace

/**
 * @brief   Creates the panel for a schematic.
 * @param[in] schematic  Schematic whose run mode is set and whose parts the tolerance buttons change; it must outlive
 *                       the panel.
 */
RunModeSettingsPanel::RunModeSettingsPanel(Schematic &schematic) : m_Schematic(schematic) {
    m_ToleranceAllField.Load(m_ToleranceAllPercent);
}

/**
 * @brief   Below the settings of the operating point, the transient, the AC sweep and the DC sweep: how they repeat,
 * which is not at all, once per value of a parameter or once per draw of the parts within their tolerances.
 * @param[in,out] settings  Settings being edited, which go back to the schematic once the panel is drawn.
 * @details The fields share their labels with those of the analysis above, so they get an ID of their own.
 */
void RunModeSettingsPanel::Draw(SimulationSettings &settings) {
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
 * @brief   Loads the text of the fields from the settings, as when the schematic replaces them.
 * @param[in] settings  Settings whose step range the fields show.
 */
void RunModeSettingsPanel::Load(const SimulationSettings &settings) {
    LoadParameterStepFields(settings.ParameterStep);
}

/**
 * @brief   The part or the temperature, and its values as a range or a typed list.
 */
void RunModeSettingsPanel::DrawParameterStepSettings(ParameterStepSettings &step) {
    const Core::Circuit circuit = m_Schematic.BuildCircuit();
    DrawParameterPicker(step, circuit);
    DrawStepValues(step, GetParameterUnit(circuit, step.Part));
}

/**
 * @brief   The temperature or a resistor, capacitor or inductor; picking a part starts its range around its value, from
 *          half to twice it.
 */
void RunModeSettingsPanel::DrawParameterPicker(ParameterStepSettings &step, const Core::Circuit &circuit) {
    DrawFieldLabel("Parameter");
    const std::string shown = step.Part.empty() ? std::string("Temperature") : step.Part;
    if (!ImGui::BeginCombo("##step_parameter", shown.c_str())) {
        return;
    }
    const std::optional<std::string> &no_temperature = Core::GetBackend().GetCapabilities().TemperatureUnsupported;
    ImGui::BeginDisabled(no_temperature.has_value());
    if (ImGui::Selectable("Temperature", step.Part.empty()) && !step.Part.empty()) {
        step.Part.clear();
        step.Start = 0.0;
        step.Stop = 100.0;
        step.Step = 25.0;
        LoadParameterStepFields(step);
    }
    ImGui::EndDisabled();
    if (no_temperature) {
        ImGui::SetItemTooltip("%s", no_temperature->c_str());
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
void RunModeSettingsPanel::DrawStepValues(ParameterStepSettings &step, const std::string &unit) {
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

void RunModeSettingsPanel::LoadParameterStepFields(const ParameterStepSettings &step) {
    m_ParameterFields[0].Load(step.Start);
    m_ParameterFields[1].Load(step.Stop);
    m_ParameterFields[2].Load(step.Step);
}

/**
 * @brief   The number of runs, how the values spread, the seed, and the tolerances of the parts.
 */
void RunModeSettingsPanel::DrawMonteCarloSettings(Core::MonteCarloSettings &monte_carlo) {
    DrawFieldLabel("Runs");
    if (ImGui::InputInt("##runs", &monte_carlo.Runs, 1, 10)) {
        monte_carlo.Runs = std::clamp(monte_carlo.Runs, Core::MIN_MONTE_CARLO_RUNS, Core::MAX_MONTE_CARLO_RUNS);
    }
    ImGui::SetItemTooltip("%s",
                          std::format("Draws, besides the nominal run that keeps every value; {} to {}",
                                      Core::MIN_MONTE_CARLO_RUNS,
                                      Core::MAX_MONTE_CARLO_RUNS)
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
void RunModeSettingsPanel::DrawToleranceButtons() {
    ImGui::TextDisabled("Tolerance of all");
    if (const std::optional<double> percent = m_ToleranceAllField.Draw("Percent", "%", [](const double value) {
            return value > 0.0 && value <= Core::MAX_TOLERANCE_PERCENT;
        })) {
        m_ToleranceAllPercent = *percent;
    }
    for (std::size_t index = 0; index < TOLERANCE_TYPES.size(); ++index) {
        if (ImGui::Button(TOLERANCE_TYPE_LABELS[index])) {
            ApplyToleranceToAll(std::span(&TOLERANCE_TYPES[index], 1), m_ToleranceAllPercent);
        }
        ImGui::SetItemTooltip("Sets the tolerance of every %s to the percent above", TOLERANCE_TYPE_LABELS[index]);
        ImGui::SameLine();
    }
    if (ImGui::Button("Clear all")) {
        ApplyToleranceToAll(TOLERANCE_TYPES, std::nullopt);
    }
    ImGui::SetItemTooltip("Takes the tolerance from every resistor, capacitor and inductor");
}

/**
 * @brief   Gives every part of the schematic of the kinds listed one tolerance, or takes it from them, and marks the
 *          schematic modified when a part changed.
 * @param[in] types    Kinds of part to change.
 * @param[in] percent  Tolerance in percent, or none to take it away.
 */
void RunModeSettingsPanel::ApplyToleranceToAll(const std::span<const Core::ComponentType> types,
                                               const std::optional<double> percent) {
    std::vector<Core::Component *> parts;
    for (const std::unique_ptr<UIElement> &element : m_Schematic.GetElements()) {
        parts.push_back(&element->GetComponent());
    }
    std::size_t changed = 0;
    for (const Core::ComponentType type : types) {
        changed += Core::SetTolerances(parts, type, percent);
    }
    if (changed > 0) {
        m_Schematic.MarkModified();
    }
}

/**
 * @brief   Which parts vary, or why the run cannot start.
 */
void RunModeSettingsPanel::DrawMonteCarloSummary(const Core::MonteCarloSettings &monte_carlo) {
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

} // namespace GUI
