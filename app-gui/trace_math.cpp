/**
 * @file    trace_math.cpp
 * @brief   Math channels: expressions of voltages and currents, such as V(1)-V(2), evaluated over a result.
 */

#include "trace_math.h"

#include "spice_value.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <utility>

namespace GUI {

namespace {

using Kind = MathInstruction::Kind;

/**
 * @struct  FunctionName
 * @brief   A function an expression may call, as typed, and the instruction it compiles to.
 */
struct FunctionName {
    std::string_view Name;
    Kind Type;
};

constexpr auto Functions = std::to_array<FunctionName>({
    {"abs", Kind::Absolute},
    {"sqrt", Kind::SquareRoot},
    {"ddt", Kind::Derivative},
    {"integ", Kind::Integral},
});

/**
 * @class   Parser
 * @brief   Recursive descent parser that compiles an expression straight into postfix instructions.
 * @details Grammar, from the lowest precedence:
 *          sum := product (('+' | '-') product)*
 *          product := unary (('*' | '/') unary)*
 *          unary := '-' unary | primary
 *          primary := number | V(node) | V(node, node) | I(name) | function(sum) | (sum)
 */
class Parser {
public:
    explicit Parser(const std::string_view text) : m_Text(text) {}

    std::expected<MathProgram, std::string> Parse() {
        if (const auto parsed = ParseSum(); !parsed) {
            return std::unexpected(parsed.error());
        }
        SkipSpaces();
        if (m_Position < m_Text.size()) {
            return std::unexpected(std::format("Unexpected \"{}\"", m_Text.substr(m_Position)));
        }
        return std::move(m_Program);
    }

private:
    using Step = std::expected<void, std::string>;

    std::string_view m_Text;
    std::size_t m_Position = 0;
    MathProgram m_Program;

    void SkipSpaces() {
        while (m_Position < m_Text.size() && std::isspace(static_cast<unsigned char>(m_Text[m_Position])) != 0) {
            ++m_Position;
        }
    }

    // Consumes the character when it comes next, after any spaces
    bool Accept(const char character) {
        SkipSpaces();
        if (m_Position < m_Text.size() && m_Text[m_Position] == character) {
            ++m_Position;
            return true;
        }
        return false;
    }

    Step Expect(const char character) {
        if (!Accept(character)) {
            return std::unexpected(std::format("Expected \"{}\"", character));
        }
        return {};
    }

    void Emit(const Kind type) { m_Program.Instructions.push_back({.Type = type}); }

    Step ParseSum() {
        if (const Step step = ParseProduct(); !step) {
            return step;
        }
        while (true) {
            const bool add = Accept('+');
            if (!add && !Accept('-')) {
                return {};
            }
            if (const Step step = ParseProduct(); !step) {
                return step;
            }
            Emit(add ? Kind::Add : Kind::Subtract);
        }
    }

    Step ParseProduct() {
        if (const Step step = ParseUnary(); !step) {
            return step;
        }
        while (true) {
            const bool multiply = Accept('*');
            if (!multiply && !Accept('/')) {
                return {};
            }
            if (const Step step = ParseUnary(); !step) {
                return step;
            }
            Emit(multiply ? Kind::Multiply : Kind::Divide);
        }
    }

    Step ParseUnary() {
        if (Accept('-')) {
            if (const Step step = ParseUnary(); !step) {
                return step;
            }
            Emit(Kind::Negate);
            return {};
        }
        return ParsePrimary();
    }

    Step ParsePrimary() {
        SkipSpaces();
        if (m_Position >= m_Text.size()) {
            return std::unexpected("The expression ends too early");
        }
        if (Accept('(')) {
            if (const Step step = ParseSum(); !step) {
                return step;
            }
            return Expect(')');
        }
        const char first = m_Text[m_Position];
        if (std::isdigit(static_cast<unsigned char>(first)) != 0 || first == '.') {
            return ParseNumber();
        }
        const std::string name = ReadWord();
        if (name.empty()) {
            return std::unexpected(std::format("Unexpected \"{}\"", m_Text.substr(m_Position)));
        }
        const std::string lower = ToLower(name);
        if (lower == "v") {
            return ParseVoltage();
        }
        if (lower == "i") {
            return ParseCurrent();
        }
        const auto function = std::ranges::find(Functions, std::string_view(lower), &FunctionName::Name);
        if (function == Functions.end()) {
            return std::unexpected(std::format("Unknown name \"{}\"; use V(node), I(part) or a function", name));
        }
        if (const Step step = Expect('('); !step) {
            return step;
        }
        if (const Step step = ParseSum(); !step) {
            return step;
        }
        if (const Step step = Expect(')'); !step) {
            return step;
        }
        Emit(function->Type);
        return {};
    }

    // Numbers take SPICE suffixes, such as 1k or 10m, like every value the user types
    Step ParseNumber() {
        const std::size_t start = m_Position;
        while (m_Position < m_Text.size()) {
            const char character = m_Text[m_Position];
            const bool exponent_sign = (character == '+' || character == '-') && m_Position > start &&
                                       (m_Text[m_Position - 1] == 'e' || m_Text[m_Position - 1] == 'E') &&
                                       std::isdigit(static_cast<unsigned char>(m_Text[m_Position - 2])) != 0;
            if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '.' && !exponent_sign) {
                break;
            }
            ++m_Position;
        }
        const std::string_view text = m_Text.substr(start, m_Position - start);
        const std::optional<double> value = Core::ParseValue(text, "");
        if (!value) {
            return std::unexpected(std::format("\"{}\" is not a number", text));
        }
        m_Program.Instructions.push_back({.Type = Kind::Number, .Number = *value});
        return {};
    }

    // V(a) is the voltage of a node and V(a, b) the voltage between two, as in SPICE
    Step ParseVoltage() {
        if (const Step step = Expect('('); !step) {
            return step;
        }
        const std::optional<int> node = ReadNode();
        if (!node) {
            return std::unexpected("V() takes a node number, such as V(2)");
        }
        m_Program.Instructions.push_back({.Type = Kind::Voltage, .Node = *node});
        if (Accept(',')) {
            const std::optional<int> second = ReadNode();
            if (!second) {
                return std::unexpected("V(a, b) takes two node numbers");
            }
            m_Program.Instructions.push_back({.Type = Kind::Voltage, .Node = *second});
            Emit(Kind::Subtract);
        }
        return Expect(')');
    }

    Step ParseCurrent() {
        if (const Step step = Expect('('); !step) {
            return step;
        }
        SkipSpaces();
        const std::size_t start = m_Position;
        while (m_Position < m_Text.size() && m_Text[m_Position] != ')' &&
               std::isspace(static_cast<unsigned char>(m_Text[m_Position])) == 0) {
            ++m_Position;
        }
        if (m_Position == start) {
            return std::unexpected("I() takes the name of a part, such as I(R1) or I(Q1.C)");
        }
        m_Program.Instructions.push_back(
            {.Type = Kind::Current, .Current = std::string(m_Text.substr(start, m_Position - start))});
        return Expect(')');
    }

    std::optional<int> ReadNode() {
        SkipSpaces();
        const std::size_t start = m_Position;
        while (m_Position < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_Position])) != 0) {
            ++m_Position;
        }
        if (m_Position == start) {
            return std::nullopt;
        }
        return std::stoi(std::string(m_Text.substr(start, m_Position - start)));
    }

    std::string ReadWord() {
        SkipSpaces();
        const std::size_t start = m_Position;
        while (m_Position < m_Text.size() && std::isalpha(static_cast<unsigned char>(m_Text[m_Position])) != 0) {
            ++m_Position;
        }
        return std::string(m_Text.substr(start, m_Position - start));
    }

    static std::string ToLower(std::string text) {
        std::ranges::transform(text, text.begin(),
                               [](const char character) { return static_cast<char>(std::tolower(character)); });
        return text;
    }
};

Dimension Combine(const Dimension first, const Dimension second, const int sign) {
    return {.Volts = first.Volts + sign * second.Volts,
            .Amperes = first.Amperes + sign * second.Amperes,
            .Seconds = first.Seconds + sign * second.Seconds,
            .Known = first.Known && second.Known};
}

// Adding a number to a trace, such as V(1)+1, keeps the unit of the trace
Dimension AddUnits(const Dimension first, const bool first_is_number, const Dimension second,
                   const bool second_is_number) {
    if (first_is_number) {
        return second;
    }
    if (second_is_number || first == second) {
        return first;
    }
    return {.Known = false};
}

// Central differences inside, one-sided at the ends; repeated X values give a slope of zero
std::vector<double> Differentiate(const std::span<const double> xs, const std::vector<double> &values) {
    std::vector<double> slopes(values.size(), 0.0);
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t before = index == 0 ? 0 : index - 1;
        const std::size_t after = std::min(index + 1, values.size() - 1);
        const double width = xs[after] - xs[before];
        if (width != 0.0) {
            slopes[index] = (values[after] - values[before]) / width;
        }
    }
    return slopes;
}

// Running trapezoidal integral from the first sample, which starts at zero
std::vector<double> Integrate(const std::span<const double> xs, const std::vector<double> &values) {
    std::vector<double> integral(values.size(), 0.0);
    for (std::size_t index = 1; index < values.size(); ++index) {
        integral[index] = integral[index - 1] + (values[index - 1] + values[index]) / 2.0 * (xs[index] - xs[index - 1]);
    }
    return integral;
}

/**
 * @struct  StackEntry
 * @brief   A value on the evaluation stack: a trace or a number spread over every sample, and its unit.
 */
struct StackEntry {
    std::vector<double> Values;
    Dimension Unit;
    bool IsNumber = false;
};

template <typename Operation>
StackEntry ApplyBinary(StackEntry first, const StackEntry &second, const Operation &operation, const Dimension unit) {
    for (std::size_t index = 0; index < first.Values.size(); ++index) {
        first.Values[index] = operation(first.Values[index], second.Values[index]);
    }
    first.Unit = unit;
    first.IsNumber = first.IsNumber && second.IsNumber;
    return first;
}

} // namespace

/**
 * @brief   Compiles an expression typed by the user.
 * @param[in] text  Expression such as V(1)-V(2), V(3)*I(R1), V(1,2), abs(I(D1)), ddt(V(2)) or integ(I(C1)); numbers
 *                  take SPICE suffixes, and names of functions, V and I ignore case.
 * @return  The compiled program, or a message saying what is wrong and where.
 */
std::expected<MathProgram, std::string> CompileExpression(const std::string_view text) {
    return Parser(text).Parse();
}

/**
 * @brief   Evaluates a compiled expression over every sample of a result.
 * @param[in] program        Compiled expression.
 * @param[in] xs             X value of each sample: times, or the values of a swept source.
 * @param[in] x_unit         Unit of the X values, which derivatives divide by and integrals multiply by.
 * @param[in] node_voltages  Voltage of each node at each sample, indexed by node number.
 * @param[in] currents       Currents of the result, found by name.
 * @return  One value per sample and their unit, or an error naming a node or part the result does not have.
 * @note    Dividing by zero gives infinite or NaN values, which plots skip.
 */
std::expected<MathResult, std::string> EvaluateExpression(const MathProgram &program, const std::span<const double> xs,
                                                          const Dimension x_unit,
                                                          const std::vector<std::vector<double>> &node_voltages,
                                                          const std::vector<Core::ComponentTrace> &currents) {
    const std::size_t count = xs.size();
    std::vector<StackEntry> stack;
    for (const MathInstruction &instruction : program.Instructions) {
        switch (instruction.Type) {
        case Kind::Number:
            stack.push_back({std::vector<double>(count, instruction.Number), {}, true});
            continue;
        case Kind::Voltage:
            if (instruction.Node < 0 || static_cast<std::size_t>(instruction.Node) >= node_voltages.size()) {
                return std::unexpected(std::format("The circuit has no node {}", instruction.Node));
            }
            stack.push_back({node_voltages[static_cast<std::size_t>(instruction.Node)], {.Volts = 1}, false});
            continue;
        case Kind::Current: {
            const auto current = std::ranges::find_if(currents, [&instruction](const Core::ComponentTrace &trace) {
                return trace.Name == instruction.Current;
            });
            if (current == currents.end()) {
                return std::unexpected(std::format("The circuit has no current I({})", instruction.Current));
            }
            stack.push_back({current->Values, {.Amperes = 1}, false});
            continue;
        }
        default:
            break;
        }

        // Every other instruction takes its operands from the stack, which the parser always fills
        StackEntry operand = std::move(stack.back());
        stack.pop_back();
        switch (instruction.Type) {
        case Kind::Negate:
            std::ranges::transform(operand.Values, operand.Values.begin(), [](const double value) { return -value; });
            break;
        case Kind::Absolute:
            std::ranges::transform(operand.Values, operand.Values.begin(),
                                   [](const double value) { return std::abs(value); });
            break;
        case Kind::SquareRoot:
            std::ranges::transform(operand.Values, operand.Values.begin(),
                                   [](const double value) { return std::sqrt(value); });
            operand.Unit = {.Known = operand.Unit == Dimension{}};
            break;
        case Kind::Derivative:
            operand.Values = Differentiate(xs, operand.Values);
            operand.Unit = Combine(operand.Unit, x_unit, -1);
            operand.IsNumber = false;
            break;
        case Kind::Integral:
            operand.Values = Integrate(xs, operand.Values);
            operand.Unit = Combine(operand.Unit, x_unit, 1);
            operand.IsNumber = false;
            break;
        default: {
            StackEntry first = std::move(stack.back());
            stack.pop_back();
            const Dimension first_unit = first.Unit;
            const bool first_is_number = first.IsNumber;
            switch (instruction.Type) {
            case Kind::Add:
                operand = ApplyBinary(std::move(first), operand, std::plus<>(),
                                      AddUnits(first_unit, first_is_number, operand.Unit, operand.IsNumber));
                break;
            case Kind::Subtract:
                operand = ApplyBinary(std::move(first), operand, std::minus<>(),
                                      AddUnits(first_unit, first_is_number, operand.Unit, operand.IsNumber));
                break;
            case Kind::Multiply:
                operand =
                    ApplyBinary(std::move(first), operand, std::multiplies<>(), Combine(first_unit, operand.Unit, 1));
                break;
            default:
                operand =
                    ApplyBinary(std::move(first), operand, std::divides<>(), Combine(first_unit, operand.Unit, -1));
                break;
            }
            break;
        }
        }
        stack.push_back(std::move(operand));
    }
    return MathResult{.Values = std::move(stack.back().Values), .Unit = stack.back().Unit};
}

/**
 * @brief   Writes the expression of an operator channel, such as V(1)-V(2).
 * @param[in] first          First trace, such as "V(1)".
 * @param[in] math_operator  Operation between the traces.
 * @param[in] second         Second trace.
 * @return  The expression, which compiles like a typed one.
 */
std::string BuildOperatorExpression(const std::string_view first, const MathOperator math_operator,
                                    const std::string_view second) {
    return std::format("{}{}{}", first, GetOperatorSymbol(math_operator), second);
}

/**
 * @brief   Names a unit as plot axes and readouts show it.
 * @param[in] unit  Powers of volts, amperes and seconds.
 * @return  "V", "A", "W", "Ohm", "S" or a product such as "V/s" or "A*s"; empty when there is no unit or it is
 *          not known.
 */
std::string FormatUnit(const Dimension unit) {
    if (!unit.Known) {
        return "";
    }
    const std::array<std::pair<Dimension, const char *>, 3> named = {{
        {{.Volts = 1, .Amperes = 1}, "W"},
        {{.Volts = 1, .Amperes = -1}, "Ohm"},
        {{.Volts = -1, .Amperes = 1}, "S"},
    }};
    for (const auto &[dimension, name] : named) {
        if (unit == dimension) {
            return name;
        }
    }
    std::string numerator;
    std::string denominator;
    const std::array<std::pair<int, const char *>, 3> powers = {{
        {unit.Volts, "V"},
        {unit.Amperes, "A"},
        {unit.Seconds, "s"},
    }};
    for (const auto &[power, symbol] : powers) {
        if (power == 0) {
            continue;
        }
        std::string &part = power > 0 ? numerator : denominator;
        if (!part.empty()) {
            part += "*";
        }
        part += symbol;
        if (std::abs(power) > 1) {
            part += std::format("^{}", std::abs(power));
        }
    }
    if (denominator.empty()) {
        return numerator;
    }
    return std::format("{}/{}", numerator.empty() ? "1" : numerator, denominator);
}

/**
 * @brief   Returns how an operation is typed in an expression.
 * @param[in] math_operator  Operation.
 * @return  "+", "-", "*" or "/".
 */
const char *GetOperatorSymbol(const MathOperator math_operator) {
    switch (math_operator) {
    case MathOperator::Add:
        return "+";
    case MathOperator::Subtract:
        return "-";
    case MathOperator::Multiply:
        return "*";
    case MathOperator::Divide:
        return "/";
    }
    return "+";
}

} // namespace GUI
