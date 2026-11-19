/**
 * @file    trace_math_parser.cpp
 * @brief   Compiles the expressions of math channels, such as V(1)-V(OUT), into programs that can run on any result.
 */

#include "results/trace_math_parser.h"

#include "circuit/components/net_label.h"
#include "spice/spice_text.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>

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

constexpr auto FUNCTIONS = std::to_array<FunctionName>({
    {"abs", Kind::ABS},
    {"sqrt", Kind::SQUARE_ROOT},
    {"ddt", Kind::DERIVATIVE},
    {"integ", Kind::INTEGRAL},
});

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

/**
 * @class   Parser
 * @brief   Recursive descent parser that compiles an expression straight into postfix instructions.
 * @details Grammar, from the lowest precedence:
 *          sum := product (('+' | '-') product)*
 *          product := unary (('*' | '/') unary)*
 *          unary := '-' unary | primary
 *          primary := number | V(node) | V(node, node) | I(name) | function(sum) | (sum)
 *          node := number | net name
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

    /**
     * @brief   Consumes the characters that satisfy the predicate, from the current position.
     * @return  What was consumed, possibly nothing.
     */
    template <typename Predicate>
    std::string_view ReadWhile(const Predicate &is_part) {
        const std::size_t start = m_Position;
        while (m_Position < m_Text.size() && is_part(m_Text[m_Position])) {
            ++m_Position;
        }
        return m_Text.substr(start, m_Position - start);
    }

    void SkipSpaces() { ReadWhile(IsSpace); }

    /**
     * @brief   Consumes the character when it comes next, after any spaces.
     */
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
            Emit(add ? Kind::ADD : Kind::SUBTRACT);
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
            Emit(multiply ? Kind::MULTIPLY : Kind::DIVIDE);
        }
    }

    Step ParseUnary() {
        if (Accept('-')) {
            if (const Step step = ParseUnary(); !step) {
                return step;
            }
            Emit(Kind::NEGATE);
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
        const std::string lower = Core::ToLower(name);
        if (lower == "v") {
            return ParseVoltage();
        }
        if (lower == "i") {
            return ParseCurrent();
        }
        const auto function = std::ranges::find(FUNCTIONS, std::string_view(lower), &FunctionName::Name);
        if (function == FUNCTIONS.end()) {
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

    /**
     * @brief   Numbers take SPICE suffixes, such as 1k or 10m, like every value the user types.
     */
    Step ParseNumber() {
        const std::size_t start = m_Position;
        const std::string_view text = ReadWhile([this, start](const char character) {
            return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '.'
                || IsExponentSign(start);
        });
        const std::optional<double> value = Core::ParseValue(text, "");
        if (!value) {
            return std::unexpected(std::format("\"{}\" is not a number", text));
        }
        m_Program.Instructions.push_back({.Type = Kind::NUMBER, .Number = *value});
        return {};
    }

    /**
     * @brief   Tells whether the character at the current position is the sign of an exponent, as in 1e-3.
     * @param[in] start  Where the number being read begins.
     */
    bool IsExponentSign(const std::size_t start) const {
        const char character = m_Text[m_Position];
        return (character == '+' || character == '-') && m_Position > start
            && (m_Text[m_Position - 1] == 'e' || m_Text[m_Position - 1] == 'E')
            && std::isdigit(static_cast<unsigned char>(m_Text[m_Position - 2])) != 0;
    }

    /**
     * @brief   V(a) is the voltage of a node and V(a, b) the voltage between two, as in SPICE.
     */
    Step ParseVoltage() {
        if (const Step step = Expect('('); !step) {
            return step;
        }
        if (const Step step = ParseNode(); !step) {
            return step;
        }
        if (Accept(',')) {
            if (const Step step = ParseNode(); !step) {
                return step;
            }
            Emit(Kind::SUBTRACT);
        }
        return Expect(')');
    }

    /**
     * @brief   A node is a number, or a name a net label gave it, which stays valid when the nodes are numbered again.
     * @details A name of digits alone reads as a number.
     */
    Step ParseNode() {
        SkipSpaces();
        const std::string_view text =
            ReadWhile([](const char character) { return character != ',' && character != ')' && !IsSpace(character); });
        if (text.empty()) {
            return std::unexpected("V() takes a node number or a net name, such as V(2) or V(OUT)");
        }
        if (std::ranges::all_of(text, [](const char character) {
                return std::isdigit(static_cast<unsigned char>(character)) != 0;
            })) {
            int node = 0;
            if (std::from_chars(text.data(), text.data() + text.size(), node).ec != std::errc()) {
                return std::unexpected(std::format("The circuit has no node {}", text));
            }
            m_Program.Instructions.push_back({.Type = Kind::VOLTAGE, .Node = node});
            return {};
        }
        if (!Core::NetLabel::IsValidNet(text)) {
            return std::unexpected(std::format("\"{}\" is not a node number or a net name", text));
        }
        m_Program.Instructions.push_back({.Type = Kind::VOLTAGE, .Net = std::string(text)});
        return {};
    }

    Step ParseCurrent() {
        if (const Step step = Expect('('); !step) {
            return step;
        }
        SkipSpaces();
        const std::string_view name =
            ReadWhile([](const char character) { return character != ')' && !IsSpace(character); });
        if (name.empty()) {
            return std::unexpected("I() takes the name of a part, such as I(R1) or I(Q1.C)");
        }
        m_Program.Instructions.push_back({.Type = Kind::CURRENT, .Current = std::string(name)});
        return Expect(')');
    }

    std::string ReadWord() {
        SkipSpaces();
        return std::string(
            ReadWhile([](const char character) { return std::isalpha(static_cast<unsigned char>(character)) != 0; }));
    }
};

} // namespace

/**
 * @brief   Writes the expression of an operator channel, such as V(1)-V(2).
 * @param[in] first          First trace, such as "V(1)".
 * @param[in] math_operator  Operation between the traces.
 * @param[in] second         Second trace.
 * @return  The expression, which compiles like a typed one.
 */
std::string
BuildOperatorExpression(const std::string_view first, const MathOperator math_operator, const std::string_view second) {
    return std::format("{}{}{}", first, GetOperatorSymbol(math_operator), second);
}

/**
 * @brief   Compiles an expression typed by the user.
 * @param[in] text  Expression such as V(1)-V(2), V(OUT)*I(R1), V(1,2), abs(I(D1)), ddt(V(2)) or integ(I(C1));
 *                  numbers take SPICE suffixes, and names of functions, V and I ignore case.
 * @return  The compiled program, or a message saying what is wrong and where.
 * @note    Net names are kept as written; ResolveNetNames() finds their nodes before the program is evaluated.
 */
std::expected<MathProgram, std::string> CompileExpression(const std::string_view text) {
    return Parser(text).Parse();
}

/**
 * @brief   Turns the net names of a compiled expression into the nodes they have in the circuit now.
 * @param[in] program    Compiled expression.
 * @param[in] find_node  Finds the node of a net name.
 * @return  The program with every voltage on a node number, or an error naming a net the circuit no longer has.
 */
std::expected<MathProgram, std::string> ResolveNetNames(MathProgram program, const NetResolver &find_node) {
    for (MathInstruction &instruction : program.Instructions) {
        if (instruction.Type != Kind::VOLTAGE || instruction.Net.empty()) {
            continue;
        }
        const std::optional<int> node = find_node(instruction.Net);
        if (!node) {
            return std::unexpected(std::format("The circuit has no net named {}", instruction.Net));
        }
        instruction.Node = *node;
        instruction.Net.clear();
    }
    return program;
}

} // namespace GUI
