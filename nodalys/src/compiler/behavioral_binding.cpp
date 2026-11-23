/**
 * @file    behavioral_binding.cpp
 * @brief   Parses behavioral source expressions during collection and resolves node-voltage input bindings.
 */

#include "compiler/behavioral_binding.h"

#include "devices/sources/behavioral_device.h"
#include "nodalys/expression.h"
#include "text/text_case.h"
#include <charconv>
#include <expected>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace Nodalys {

namespace {

int MapTopNode(const NodeId node) {
    return node <= 0 ? -1 : node - 1;
}

std::optional<NodeId> ReadExpressionNode(const std::string &text) {
    const std::string lowered = ToLower(text);
    if (lowered == "gnd" || lowered == "ground") {
        return GROUND_NODE;
    }
    NodeId node = 0;
    const auto [end, error] = std::from_chars(lowered.data(), lowered.data() + lowered.size(), node);
    if (error != std::errc() || end != lowered.data() + lowered.size() || node < 0) {
        return std::nullopt;
    }
    return node;
}

std::optional<std::string> BindExpressionInputs(CircuitDescription &description,
                                                const std::string &name,
                                                const Expression &expression,
                                                std::vector<BehavioralBinding> &bindings,
                                                std::vector<BehavioralCurrentInput> &currents) {
    for (const ExpressionInput &input : expression.GetInputs()) {
        BehavioralBinding binding;
        if (input.Type == ExpressionInput::Kind::CURRENT) {
            binding.IsCurrent = true;
            currents.push_back({.Owner = name, .Input = bindings.size(), .Source = input.First});
        } else {
            const std::optional<NodeId> node = ReadExpressionNode(input.First);
            const std::optional<NodeId> reference =
                input.Second.empty() ? std::optional<NodeId>(GROUND_NODE) : ReadExpressionNode(input.Second);
            if (!node || !reference) {
                return std::format("{}: v({}{}{}) must name nodes by their number",
                                   name,
                                   input.First,
                                   input.Second.empty() ? "" : ", ",
                                   input.Second);
            }
            description.TrackNodes({*node, *reference});
            binding.Unknown = MapTopNode(*node);
            binding.Reference = MapTopNode(*reference);
        }
        bindings.push_back(binding);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Parses a behavioral source expression during collection, tracks referenced nodes, and appends a
 *          BehavioralSpec (or records an error on the description).
 * @param[in,out] description  Circuit description receiving the element or diagnostic error.
 * @param[in]     name         Behavioral element designator.
 * @param[in]     pos          Positive terminal node.
 * @param[in]     neg          Negative terminal node.
 * @param[in]     text         Raw mathematical expression string.
 * @param[in]     is_voltage   True for a voltage source (`V=...`), false for a current source (`I=...`).
 */
void AppendBehavioralElement(CircuitDescription &description,
                             std::string name,
                             const NodeId pos,
                             const NodeId neg,
                             const std::string &text,
                             const bool is_voltage) {
    description.TrackNodes({pos, neg});
    std::expected<Expression, std::string> expression = Expression::Parse(text);
    if (!expression) {
        description.Errors.push_back(std::format("{}: {}", name, expression.error()));
        return;
    }
    std::vector<BehavioralBinding> bindings;
    std::vector<BehavioralCurrentInput> currents;
    if (const std::optional<std::string> error =
            BindExpressionInputs(description, name, *expression, bindings, currents)) {
        description.Errors.push_back(*error);
        return;
    }
    description.Elements.push_back(BehavioralSpec{
        .Name = std::move(name),
        .Pos = pos,
        .Neg = neg,
        .IsVoltage = is_voltage,
        .Expr = std::move(*expression),
        .Bindings = std::move(bindings),
        .CurrentInputs = std::move(currents),
    });
}

} // namespace Nodalys
