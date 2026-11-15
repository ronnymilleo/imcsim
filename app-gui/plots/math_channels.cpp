/**
 * @file    math_channels.cpp
 * @brief   Traces computed from others, like the math channels of an oscilloscope: their list, editor and evaluation.
 */

#include "plots/math_channels.h"

#include "base/theme.h"
#include "base/trace_swatch.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace GUI {

namespace {

/// Math channels take their own colors, apart from those of nodes and currents.
constexpr auto MATH_COLORS = std::to_array<ImU32>({
    IM_COL32(230, 110, 230, 255),
    IM_COL32(150, 230, 90, 255),
    IM_COL32(235, 235, 235, 255),
    IM_COL32(90, 220, 190, 255),
});
/// Width of an operand field in the math editor, in font sizes.
constexpr float OPERAND_WIDTH = 10.0f;
/// Width of an expression field in the math editor, in font sizes.
constexpr float EXPRESSION_WIDTH = 20.0f;

/**
 * @brief   Voltages and currents share the axes of the probed traces; any other unit, or none, goes on the third axis.
 */
ImAxis AxisForUnit(const Dimension unit) {
    if (unit == Dimension{.Volts = 1}) {
        return ImAxis_Y1;
    }
    if (unit == Dimension{.Amperes = 1}) {
        return ImAxis_Y2;
    }
    return ImAxis_Y3;
}

/**
 * @brief   The voltages of an AC sweep read as gains, relative to sources of magnitude 1, so a ratio without a unit,
 * such as V(OUT)/V(IN), shares their axis.
 */
ImAxis AxisForACUnit(const Dimension unit) {
    if (unit == Dimension{}) {
        return ImAxis_Y1;
    }
    return AxisForUnit(unit);
}

void DrawOperandCombo(const char *id, std::string &operand, const std::vector<std::string> &operands) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * OPERAND_WIDTH);
    if (ImGui::BeginCombo(id, operand.empty() ? "(none)" : operand.c_str())) {
        for (const std::string &candidate : operands) {
            if (ImGui::Selectable(candidate.c_str(), candidate == operand)) {
                operand = candidate;
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

/**
 * @brief   Creates the panel of the math channels of a schematic.
 * @param[in,out] schematic  Schematic that keeps the channels; it must outlive the panel.
 */
MathChannels::MathChannels(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the list of channels: each shows its check box and an Edit button. A channel whose expression does
 *          not compile, or names a net the circuit no longer has, reads in the error color, with the reason in its
 *          tooltip. Add creates a channel and opens its editor right away.
 * @param[in] operands  Traces a channel may combine, such as "V(2)", "V(OUT)" or "I(R1)".
 */
void MathChannels::Draw(const std::vector<std::string> &operands) {
    ImGui::TextDisabled("Math");
    std::vector<MathChannel> channels = m_Schematic.GetMathChannels();
    const NetResolver find_node = GetNetResolver();
    bool open_new_editor = false;
    if (ImGui::SmallButton("Add")) {
        MathChannel channel{.Name = GetNextChannelName(channels)};
        channel.First = operands.empty() ? "" : operands.front();
        channel.Second = operands.size() > 1 ? operands[1] : channel.First;
        channels.push_back(std::move(channel));
        open_new_editor = true;
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < channels.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const bool opens_editor = open_new_editor && index + 1 == channels.size();
        if (!DrawChannel(channels[index], index, operands, find_node, opens_editor)) {
            removed = index;
        }
        ImGui::PopID();
    }
    if (removed) {
        channels.erase(channels.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
    if (channels != m_Schematic.GetMathChannels()) {
        m_Schematic.SetMathChannels(std::move(channels));
    }
}

/**
 * @brief   Describes the channels, so a result evaluated with them can tell whether they changed since.
 * @return  A text that differs whenever a channel is added, removed, shown or hidden, renamed or edited.
 * @note    The nodes its net names stand for are not part of it: an edit that numbers the nodes again also drops the
 *          results the channels were evaluated over.
 */
std::string MathChannels::GetSignature() const {
    std::string signature;
    for (const MathChannel &channel : m_Schematic.GetMathChannels()) {
        signature += std::format("{}|{}|{};", channel.Name, channel.Shown, GetChannelExpression(channel));
    }
    return signature;
}

/**
 * @brief   Evaluates every shown channel over one result.
 * @param[in]  xs             X values of the result: times, or the swept values of a DC sweep.
 * @param[in]  x_unit         Unit of the X values, which ddt() and integ() take into the unit of their result.
 * @param[in]  node_voltages  Voltage of every node at every X value, ground first.
 * @param[in]  currents       Currents of the result, which I(name) reads.
 * @param[out] errors         Gets a line for each channel that names a net, node or part the result lacks.
 * @return  The traces of the shown channels that compile and evaluate; the others are left out.
 */
std::vector<MathTrace> MathChannels::Evaluate(const std::span<const double> xs, const Dimension x_unit,
                                              const std::vector<std::vector<double>> &node_voltages,
                                              const std::vector<Core::ComponentTrace> &currents,
                                              std::vector<std::string> &errors) const {
    std::vector<MathTrace> traces;
    const std::vector<MathChannel> &channels = m_Schematic.GetMathChannels();
    const NetResolver find_node = GetNetResolver();
    for (std::size_t index = 0; index < channels.size(); ++index) {
        const MathChannel &channel = channels[index];
        const std::optional<MathProgram> program = GetShownProgram(channel, find_node, errors);
        if (!program) {
            continue;
        }
        auto result = EvaluateExpression(*program, xs, x_unit, node_voltages, currents);
        if (!result) {
            errors.push_back(std::format("{}: {}", channel.Name, result.error()));
            continue;
        }
        traces.push_back({.Label = std::format("{}: {}", channel.Name, GetChannelExpression(channel)),
                          .Values = std::move(result->Values),
                          .Unit = FormatUnit(result->Unit),
                          .Axis = AxisForUnit(result->Unit),
                          .Color = AdaptToBackground(MATH_COLORS[index % MATH_COLORS.size()])});
    }
    return traces;
}

/**
 * @brief   Evaluates every shown channel over an AC sweep, on the complex values of its traces.
 * @param[in]  sweep   The AC sweep, whose magnitudes and phases make the phasors.
 * @param[out] errors  Gets a line for each channel that names a net, node or part the sweep lacks.
 * @return  The magnitude and phase traces of the shown channels that compile and evaluate; the others are left out.
 */
ACMathTraces MathChannels::EvaluateAC(const Core::ACSweep &sweep, std::vector<std::string> &errors) const {
    ACMathTraces traces;
    const std::vector<MathChannel> &channels = m_Schematic.GetMathChannels();
    const NetResolver find_node = GetNetResolver();
    for (std::size_t index = 0; index < channels.size(); ++index) {
        const MathChannel &channel = channels[index];
        const std::optional<MathProgram> program = GetShownProgram(channel, find_node, errors);
        if (!program) {
            continue;
        }
        auto result = EvaluateACExpression(*program, sweep);
        if (!result) {
            errors.push_back(std::format("{}: {}", channel.Name, result.error()));
            continue;
        }
        const MathTrace trace{.Label = std::format("{}: {}", channel.Name, GetChannelExpression(channel)),
                              .Values = {},
                              .Unit = FormatUnit(result->Unit),
                              .Axis = AxisForACUnit(result->Unit),
                              .Color = AdaptToBackground(MATH_COLORS[index % MATH_COLORS.size()])};
        traces.Magnitudes.push_back(trace);
        traces.Magnitudes.back().Values = std::move(result->MagnitudesDecibels);
        traces.Phases.push_back(trace);
        traces.Phases.back().Values = std::move(result->PhasesDegrees);
    }
    return traces;
}

/**
 * @brief   Net names are looked up in the connectivity of the moment, which the schematic keeps up to date.
 */
NetResolver MathChannels::GetNetResolver() const {
    return [&schematic = m_Schematic](const std::string_view net) {
        return schematic.GetConnectivity().FindNodeOfNet(net);
    };
}

/**
 * @brief   Compiles a shown channel and finds the nodes of its net names.
 * @details A channel that does not compile has its mark in the panel and nothing to add to the errors; one that names
 *          a net the circuit lacks adds a line. Returns no program for a hidden channel or either failure.
 */
std::optional<MathProgram> MathChannels::GetShownProgram(const MathChannel &channel, const NetResolver &find_node,
                                                         std::vector<std::string> &errors) const {
    const std::expected<MathProgram, std::string> compiled = CompileExpression(GetChannelExpression(channel));
    if (!channel.Shown || !compiled) {
        return std::nullopt;
    }
    std::expected<MathProgram, std::string> program = ResolveNetNames(*compiled, find_node);
    if (!program) {
        errors.push_back(std::format("{}: {}", channel.Name, program.error()));
        return std::nullopt;
    }
    return std::move(*program);
}

/**
 * @brief   Draws one channel of the list: its Edit button, its check box in the color of its place, and its editor
 *          while open.
 * @param[in,out] channel       The channel, which the check box and the editor change.
 * @param[in]     index         Its place in the list, which picks its color.
 * @param[in]     operands      Traces the channel may combine.
 * @param[in]     find_node     Looks up the nets its expression names.
 * @param[in]     opens_editor  Whether its editor opens now, as for a channel just added.
 * @return  False when the user removed the channel in its editor.
 */
bool MathChannels::DrawChannel(MathChannel &channel, const std::size_t index, const std::vector<std::string> &operands,
                               const NetResolver &find_node, const bool opens_editor) const {
    // The list is as narrow as the trace names, so a mark after the Edit button would be cut off; the name of a
    // channel in error takes the error color instead
    const auto program = CompileChannel(channel, find_node);
    // Edit comes first, so a long name is what the narrow list clips, not the button
    ImGui::AlignTextToFramePadding();
    const bool is_edit_pressed = ImGui::SmallButton("Edit");
    ImGui::SameLine();
    if (!program) {
        ImGui::PushStyleColor(ImGuiCol_Text, GetErrorTextColor());
    }
    DrawTraceCheckbox(channel.Name.c_str(), &channel.Shown, AdaptToBackground(MATH_COLORS[index % MATH_COLORS.size()]),
                      TraceStyle::HEAVY);
    if (!program) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s\n%s", GetChannelExpression(channel).c_str(), program.error().c_str());
    } else {
        ImGui::SetItemTooltip("%s", GetChannelExpression(channel).c_str());
    }
    if (is_edit_pressed || opens_editor) {
        ImGui::OpenPopup("math_editor");
    }
    bool is_kept = true;
    if (ImGui::BeginPopup("math_editor")) {
        is_kept = DrawEditor(channel, operands);
        ImGui::EndPopup();
    }
    return is_kept;
}

/**
 * @brief   Like the math menu of an oscilloscope: two traces and an operator button, or a typed expression.
 * @details Switching to an expression starts from the operation, so it can be extended. Returns false when the channel
 *          is removed.
 */
bool MathChannels::DrawEditor(MathChannel &channel, const std::vector<std::string> &operands) const {
    ImGui::TextUnformatted(channel.Name.c_str());
    if (ImGui::RadioButton("Operation", !channel.UsesExpression)) {
        channel.UsesExpression = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Expression", channel.UsesExpression) && !channel.UsesExpression) {
        channel.Expression = GetChannelExpression(channel);
        channel.UsesExpression = true;
    }

    if (channel.UsesExpression) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * EXPRESSION_WIDTH);
        ImGui::InputText("##expression", &channel.Expression);
        ImGui::TextDisabled("V(2), V(OUT), V(1,2), I(R1), I(Q1.C), numbers such as 1k");
        ImGui::TextDisabled("+ - * / ( ), abs(), sqrt(), ddt() and integ()");
    } else {
        DrawOperandCombo("##first", channel.First, operands);
        for (std::size_t index = 0; index < MATH_OPERATORS.size(); ++index) {
            const MathOperator math_operator = MATH_OPERATORS[index];
            if (index > 0) {
                ImGui::SameLine();
            }
            const char *symbol = GetOperatorSymbol(math_operator);
            if (channel.Operator == math_operator ? PrimaryButton(symbol) : ImGui::Button(symbol)) {
                channel.Operator = math_operator;
            }
        }
        DrawOperandCombo("##second", channel.Second, operands);
    }
    if (const auto program = CompileChannel(channel, GetNetResolver()); !program) {
        ImGui::TextColored(GetErrorTextColor(), "%s", program.error().c_str());
    }
    ImGui::Separator();
    if (ImGui::Button("Remove")) {
        ImGui::CloseCurrentPopup();
        return false;
    }
    return true;
}

/**
 * @brief   Tells whether any math trace goes on an axis.
 * @param[in] math  Math traces of a plot.
 * @param[in] axis  ImAxis_Y1 for volts, ImAxis_Y2 for amperes, ImAxis_Y3 for any other unit.
 * @return  True when some trace uses it.
 */
bool UsesAxis(const std::vector<MathTrace> &math, const ImAxis axis) {
    return std::ranges::contains(math, axis, &MathTrace::Axis);
}

/**
 * @brief   Finds the unit of the third axis.
 * @param[in] math  Math traces of a plot.
 * @return  The unit its traces share, or an empty string when they differ, have none, or there are none.
 */
std::string ThirdAxisUnit(const std::vector<MathTrace> &math) {
    std::optional<std::string> unit;
    for (const MathTrace &trace : math) {
        if (trace.Axis != ImAxis_Y3) {
            continue;
        }
        if (unit && *unit != trace.Unit) {
            return "";
        }
        unit = trace.Unit;
    }
    return unit.value_or("");
}

/**
 * @brief   Names the math traces of a plot, so it can tell when they change.
 * @param[in] math  Math traces of a plot.
 * @return  The label and unit of each trace, in order.
 */
std::vector<std::string> ListMathLabels(const std::vector<MathTrace> &math) {
    std::vector<std::string> labels;
    for (const MathTrace &trace : math) {
        labels.push_back(trace.Label + trace.Unit);
    }
    return labels;
}

} // namespace GUI
