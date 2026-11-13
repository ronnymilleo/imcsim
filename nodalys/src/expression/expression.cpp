/**
 * @file    expression.cpp
 * @brief   Reads an expression of node voltages, branch currents and time, and evaluates it with its exact partial
 *          derivatives.
 */

#include "nodalys/expression.h"

#include "expression/expression_math.h"
#include "expression/expression_parser.h"
#include "expression/expression_program.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Nodalys {

namespace {

/// The one temperature Nodalys simulates at, in degrees Celsius, which is what `temper` reads.
constexpr double NOMINAL_TEMPERATURE_CELSIUS = 27.0;

/**
 * @class   Evaluator
 * @brief   Computes the value and the partial derivatives of every node of a program, in forward mode: each node
 *          carries its derivative with respect to every input, and an operation combines those of its operands by the
 *          chain rule.
 * @details The ternary evaluates the branch it takes and none else, so a guard such as `v(x) > 0 ? ln(v(x)) : 0` never
 *          meets the domain error of the branch it skips, as in ngspice.
 */
class Evaluator {
public:
    Evaluator(const ExpressionProgram &program, const std::span<const double> inputs, const double time_seconds)
        : m_Program(program), m_Inputs(inputs), m_TimeSeconds(time_seconds), m_InputCount(program.Inputs.size()),
          m_Values(program.Nodes.size(), 0.0), m_Partials(program.Nodes.size() * program.Inputs.size(), 0.0) {}

    std::expected<ExpressionResult, std::string> Run() {
        if (const auto done = Evaluate(m_Program.Root); !done) {
            return std::unexpected(done.error());
        }
        const std::size_t root = static_cast<std::size_t>(m_Program.Root);
        ExpressionResult result{.Value = m_Values[root], .Partials = {}};
        const auto partials = PartialsOf(root);
        result.Partials.assign(partials.begin(), partials.end());
        const bool is_finite = std::isfinite(result.Value) &&
                               std::ranges::all_of(result.Partials, [](const double p) { return std::isfinite(p); });
        if (!is_finite) {
            return std::unexpected("The expression is out of range: its value or a derivative is not finite");
        }
        return result;
    }

private:
    const ExpressionProgram &m_Program;
    std::span<const double> m_Inputs;
    double m_TimeSeconds;
    std::size_t m_InputCount;
    std::vector<double> m_Values;
    std::vector<double> m_Partials;

    std::span<double> PartialsOf(const std::size_t node) {
        return std::span<double>(m_Partials).subspan(node * m_InputCount, m_InputCount);
    }

    std::expected<void, std::string> Evaluate(const int index) {
        const ExpressionNode &node = m_Program.Nodes[static_cast<std::size_t>(index)];
        const std::size_t slot = static_cast<std::size_t>(index);
        switch (node.Kind) {
        case Operation::CONSTANT:
            m_Values[slot] = node.Constant;
            return {};
        case Operation::INPUT:
            m_Values[slot] = m_Inputs[static_cast<std::size_t>(node.Input)];
            PartialsOf(slot)[static_cast<std::size_t>(node.Input)] = 1.0;
            return {};
        case Operation::TIME:
            m_Values[slot] = m_TimeSeconds;
            return {};
        case Operation::TEMPERATURE:
            m_Values[slot] = NOMINAL_TEMPERATURE_CELSIUS;
            return {};
        case Operation::TERNARY:
            return EvaluateTernary(node, slot);
        default:
            return EvaluateOperation(node, slot);
        }
    }

    std::expected<void, std::string> EvaluateTernary(const ExpressionNode &node, const std::size_t slot) {
        if (const auto condition = Evaluate(node.First); !condition) {
            return condition;
        }
        const bool is_true = m_Values[static_cast<std::size_t>(node.First)] != 0.0;
        const int chosen = is_true ? node.Second : node.Third;
        if (const auto taken = Evaluate(chosen); !taken) {
            return taken;
        }
        m_Values[slot] = m_Values[static_cast<std::size_t>(chosen)];
        const auto source = PartialsOf(static_cast<std::size_t>(chosen));
        std::ranges::copy(source, PartialsOf(slot).begin());
        return {};
    }

    /// Applies the chain rule: the derivative of the node is the sum, over its operands, of the partial derivative of
    /// the operation with respect to the operand times the derivative of the operand.
    std::expected<void, std::string> EvaluateOperation(const ExpressionNode &node, const std::size_t slot) {
        if (const auto first = Evaluate(node.First); !first) {
            return first;
        }
        const std::size_t first_slot = static_cast<std::size_t>(node.First);
        double second_value = 0.0;
        if (node.Second >= 0) {
            if (const auto second = Evaluate(node.Second); !second) {
                return second;
            }
            second_value = m_Values[static_cast<std::size_t>(node.Second)];
        }
        const auto applied = ApplyOperation(node.Kind, m_Values[first_slot], second_value);
        if (!applied) {
            return std::unexpected(applied.error());
        }
        const bool is_finite = std::isfinite(applied->Value) && std::isfinite(applied->PartialFirst) &&
                               std::isfinite(applied->PartialSecond);
        if (!is_finite) {
            return std::unexpected(std::format("{} gives a value out of range", GetOperationName(node.Kind)));
        }
        m_Values[slot] = applied->Value;
        const auto partials = PartialsOf(slot);
        const auto first_partials = PartialsOf(first_slot);
        for (std::size_t input = 0; input < m_InputCount; ++input) {
            partials[input] = applied->PartialFirst * first_partials[input];
        }
        if (node.Second >= 0) {
            const auto second_partials = PartialsOf(static_cast<std::size_t>(node.Second));
            for (std::size_t input = 0; input < m_InputCount; ++input) {
                partials[input] += applied->PartialSecond * second_partials[input];
            }
        }
        return {};
    }
};

} // namespace

/**
 * @brief   Reads an expression.
 * @param[in] text  The expression, as in the `V=` or `I=` of a `B` element.
 * @return  The expression, or a message that says what is wrong in it and names what the language does not have.
 */
std::expected<Expression, std::string> Expression::Parse(const std::string_view text) {
    auto program = ParseExpressionProgram(text);
    if (!program) {
        return std::unexpected(std::move(program.error()));
    }
    return Expression(std::make_shared<const ExpressionProgram>(std::move(*program)));
}

/**
 * @brief   Lists the distinct voltages and currents the expression reads.
 * @return  The inputs in the order they first appear; the same voltage written twice is one input.
 */
const std::vector<ExpressionInput> &Expression::GetInputs() const {
    return m_Program->Inputs;
}

/**
 * @brief   Lists every `v(...)` and `i(...)` of the text with its place, for a caller that rewrites the names.
 * @return  The references in the order written.
 */
const std::vector<ExpressionReference> &Expression::GetReferences() const {
    return m_Program->References;
}

/**
 * @brief   Tells whether the expression reads `time`.
 * @return  True when its value can change with the instant of a transient.
 */
bool Expression::IsTimeDependent() const {
    return m_Program->IsTimeDependent;
}

/**
 * @brief   Computes the value of the expression and its derivative with respect to each input.
 * @param[in] inputs        The value of each input, in the order of GetInputs(): the voltage between the nodes of a
 *                          voltage, the current of a source.
 * @param[in] time_seconds  What `time` reads; 0 outside a transient.
 * @return  The value and the partial derivatives, or why the operands are out of range, such as the square root of a
 *          negative number, or a value that is not finite.
 */
std::expected<ExpressionResult, std::string> Expression::Evaluate(const std::span<const double> inputs,
                                                                  const double time_seconds) const {
    if (inputs.size() != m_Program->Inputs.size()) {
        return std::unexpected(
            std::format("The expression reads {} inputs, not {}", m_Program->Inputs.size(), inputs.size()));
    }
    return Evaluator(*m_Program, inputs, time_seconds).Run();
}

Expression::Expression(std::shared_ptr<const ExpressionProgram> program) : m_Program(std::move(program)) {
}

} // namespace Nodalys
