/**
 * @file    math_channels.cpp
 * @brief   Traces computed from others, like the math channels of an oscilloscope: their list, editor and evaluation.
 */

#include "math_channels.h"

#include "misc/cpp/imgui_stdlib.h"
#include "theme.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace GUI {

namespace {

// Math channels take their own colors, apart from those of nodes and currents
constexpr auto MATH_COLORS = std::to_array<ImU32>({
    IM_COL32(230, 110, 230, 255),
    IM_COL32(150, 230, 90, 255),
    IM_COL32(235, 235, 235, 255),
    IM_COL32(90, 220, 190, 255),
});
// Widths in the math editor, in font sizes
constexpr float OPERAND_WIDTH = 10.0f;
constexpr float EXPRESSION_WIDTH = 20.0f;
constexpr auto MATH_OPERATORS = std::to_array<MathOperator>({
    MathOperator::ADD,
    MathOperator::SUBTRACT,
    MathOperator::MULTIPLY,
    MathOperator::DIVIDE,
});

// Voltages and currents share the axes of the probed traces; any other unit, or none, goes on the third axis
ImAxis AxisForUnit(const Dimension unit) {
    if (unit == Dimension{.Volts = 1}) {
        return ImAxis_Y1;
    }
    if (unit == Dimension{.Amperes = 1}) {
        return ImAxis_Y2;
    }
    return ImAxis_Y3;
}

std::string GetChannelExpression(const MathChannel &channel) {
    if (channel.UsesExpression) {
        return channel.Expression;
    }
    return BuildOperatorExpression(channel.First, channel.Operator, channel.Second);
}

void CompileChannel(MathChannel &channel) {
    channel.Program = CompileExpression(GetChannelExpression(channel));
}

// Returns whether the user picked another trace
bool DrawOperandCombo(const char *id, std::string &operand, const std::vector<std::string> &operands) {
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * OPERAND_WIDTH);
    if (ImGui::BeginCombo(id, operand.empty() ? "(none)" : operand.c_str())) {
        for (const std::string &candidate : operands) {
            if (ImGui::Selectable(candidate.c_str(), candidate == operand)) {
                operand = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

/**
 * @brief   Draws the list of channels: each shows its check box, an Edit button and, when its expression does not
 *          compile, a warning mark. Add creates a channel and opens its editor right away.
 * @param[in] operands  Traces a channel may combine, such as "V(2)" or "I(R1)".
 */
void MathChannels::Draw(const std::vector<std::string> &operands) {
    ImGui::TextDisabled("Math");
    bool open_new_editor = false;
    if (ImGui::SmallButton("Add")) {
        MathChannel channel;
        channel.Name = std::format("M{}", m_NextNumber++);
        channel.First = operands.empty() ? "" : operands.front();
        channel.Second = operands.size() > 1 ? operands[1] : channel.First;
        CompileChannel(channel);
        m_Channels.push_back(std::move(channel));
        open_new_editor = true;
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < m_Channels.size(); ++index) {
        MathChannel &channel = m_Channels[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, AdaptToBackground(MATH_COLORS[index % MATH_COLORS.size()]));
        ImGui::Checkbox(channel.Name.c_str(), &channel.Shown);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", GetChannelExpression(channel).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit") || (open_new_editor && index + 1 == m_Channels.size())) {
            ImGui::OpenPopup("math_editor");
        }
        if (!channel.Program) {
            ImGui::SameLine();
            ImGui::TextColored(GetErrorTextColor(), "!");
            ImGui::SetItemTooltip("%s", channel.Program.error().c_str());
        }
        if (ImGui::BeginPopup("math_editor")) {
            if (!DrawEditor(channel, operands)) {
                removed = index;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removed) {
        m_Channels.erase(m_Channels.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
}

/**
 * @brief   Tells whether there is any channel, shown or not.
 * @return  True when the user has added none, or removed them all.
 */
bool MathChannels::IsEmpty() const {
    return m_Channels.empty();
}

/**
 * @brief   Describes the channels, so a result evaluated with them can tell whether they changed since.
 * @return  A text that differs whenever a channel is added, removed, shown or hidden, renamed or edited.
 */
std::string MathChannels::GetSignature() const {
    std::string signature;
    for (const MathChannel &channel : m_Channels) {
        signature += std::format("{}|{}|{}|{};", channel.Name, channel.Shown, GetChannelExpression(channel),
                                 channel.Program.has_value());
    }
    return signature;
}

/**
 * @brief   Evaluates every shown channel over one result.
 * @param[in]  xs             X values of the result: times, or the swept values of a DC sweep.
 * @param[in]  x_unit         Unit of the X values, which ddt() and integ() take into the unit of their result.
 * @param[in]  node_voltages  Voltage of every node at every X value, ground first.
 * @param[in]  currents       Currents of the result, which I(name) reads.
 * @param[out] errors         Gets a line for each channel that names a node or part the result lacks.
 * @return  The traces of the shown channels that compile and evaluate; the others are left out.
 */
std::vector<MathTrace> MathChannels::Evaluate(const std::span<const double> xs, const Dimension x_unit,
                                              const std::vector<std::vector<double>> &node_voltages,
                                              const std::vector<Core::ComponentTrace> &currents,
                                              std::vector<std::string> &errors) const {
    std::vector<MathTrace> traces;
    for (std::size_t index = 0; index < m_Channels.size(); ++index) {
        const MathChannel &channel = m_Channels[index];
        if (!channel.Shown || !channel.Program) {
            continue;
        }
        auto result = EvaluateExpression(*channel.Program, xs, x_unit, node_voltages, currents);
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

// Like the math menu of an oscilloscope: two traces and an operator button, or a typed expression. Switching to
// an expression starts from the operation, so it can be extended. Returns false when the channel is removed
bool MathChannels::DrawEditor(MathChannel &channel, const std::vector<std::string> &operands) {
    ImGui::TextUnformatted(channel.Name.c_str());
    bool changed = false;
    if (ImGui::RadioButton("Operation", !channel.UsesExpression)) {
        channel.UsesExpression = false;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Expression", channel.UsesExpression) && !channel.UsesExpression) {
        channel.Expression = GetChannelExpression(channel);
        channel.UsesExpression = true;
        changed = true;
    }

    if (channel.UsesExpression) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * EXPRESSION_WIDTH);
        changed |= ImGui::InputText("##expression", &channel.Expression);
        ImGui::TextDisabled("V(2), V(1,2), I(R1), I(Q1.C), numbers such as 1k");
        ImGui::TextDisabled("+ - * / ( ), abs(), sqrt(), ddt() and integ()");
    } else {
        changed |= DrawOperandCombo("##first", channel.First, operands);
        for (std::size_t index = 0; index < MATH_OPERATORS.size(); ++index) {
            const MathOperator math_operator = MATH_OPERATORS[index];
            if (index > 0) {
                ImGui::SameLine();
            }
            const char *symbol = GetOperatorSymbol(math_operator);
            if (channel.Operator == math_operator ? PrimaryButton(symbol) : ImGui::Button(symbol)) {
                channel.Operator = math_operator;
                changed = true;
            }
        }
        changed |= DrawOperandCombo("##second", channel.Second, operands);
    }
    if (changed) {
        CompileChannel(channel);
    }
    if (!channel.Program) {
        ImGui::TextColored(GetErrorTextColor(), "%s", channel.Program.error().c_str());
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
