/**
 * @file    expression.h
 * @brief   Reads an expression of node voltages, branch currents and time, and evaluates it with its exact partial
 *          derivatives.
 * @details The language is the one of the ngspice behavioral source (the `B` element), so a netlist written for one
 *          engine means the same on the other: numbers with SPICE suffixes (`4.7k`, `1meg`, `10n`), the constants `pi`
 *          and `e`, the variables `time` (seconds, 0 outside a transient) and `temper` (27, the one temperature
 *          Nodalys simulates at), the references `v(node)`, `v(node, reference)` and `i(source)`, the operators
 *          `^` and `**` (power), unary `-` `+` `!`, `*` `/`, `+` `-`, `<` `>` `<=` `>=`, `==` `!=` `<>`, `&&`, `||`
 *          and the ternary `condition ? then : otherwise`, and the functions `abs`, `sqrt`, `exp`, `ln`, `log`
 *          (natural, as in ngspice), `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`,
 *          `asinh`, `acosh`, `atanh`, `sgn`, `u`, `u2`, `uramp`, `floor`, `ceil`, `nint`, `min`, `max`, `pow` and
 *          `pwr`. Anything else is refused with a message that names it, never evaluated as something close.
 *
 *          Evaluation gives the value and its derivative with respect to every input at once, by forward-mode
 *          automatic differentiation, so a Newton iteration linearizes the expression exactly.
 */

#ifndef NODALYS_EXPRESSION_H
#define NODALYS_EXPRESSION_H

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  ExpressionInput
 * @brief   A voltage or a current an expression reads, as it is named in the text.
 */
struct ExpressionInput {
    enum class Kind {
        VOLTAGE,
        CURRENT
    };

    Kind Type = Kind::VOLTAGE;
    /// The node of a voltage, or the source of a current, as written.
    std::string First;
    /// The reference node of `v(node, reference)`, or empty for a voltage to ground and for a current.
    std::string Second;
};

/**
 * @struct  ExpressionReference
 * @brief   One `v(...)` or `i(...)` of the text, with where it is written, so a caller can rewrite the names.
 */
struct ExpressionReference {
    ExpressionInput Input;
    /// Character of the `v` or the `i`.
    std::size_t Start = 0;
    /// Characters from Start to the closing parenthesis, included.
    std::size_t Length = 0;
};

/**
 * @struct  ExpressionResult
 * @brief   The value of an expression and its derivative with respect to each input.
 */
struct ExpressionResult {
    double Value = 0.0;
    /// One per input, in the order of Expression::GetInputs().
    std::vector<double> Partials;
};

struct ExpressionProgram;

/**
 * @class   Expression
 * @brief   A parsed expression, cheap to copy, that is evaluated at the values of its inputs.
 */
class Expression {
public:
    static std::expected<Expression, std::string> Parse(std::string_view text);

    const std::vector<ExpressionInput> &GetInputs() const;
    const std::vector<ExpressionReference> &GetReferences() const;
    bool IsTimeDependent() const;

    std::expected<ExpressionResult, std::string> Evaluate(std::span<const double> inputs, double time_seconds) const;

private:
    explicit Expression(std::shared_ptr<const ExpressionProgram> program);

    std::shared_ptr<const ExpressionProgram> m_Program;
};

} // namespace Nodalys

#endif // NODALYS_EXPRESSION_H
