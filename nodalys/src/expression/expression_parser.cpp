/**
 * @file    expression_parser.cpp
 * @brief   Parses the text of an expression into the compact form that is evaluated.
 */

#include "expression/expression_parser.h"

#include "expression/expression_math.h"
#include "expression/expression_vocabulary.h"
#include "text/spice_number.h"
#include "text/text_case.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

/// Deepest nesting of parentheses, operators and calls, so a hostile text cannot exhaust the stack of the parser or of
/// the evaluation that follows its shape.
constexpr int MAX_NESTING_DEPTH = 200;

/**
 * @class   Parser
 * @brief   Recursive descent over the text, one function per level of precedence, lowest first. An error is kept as
 *          the first message and every function after it returns -1.
 */
class Parser {
public:
    explicit Parser(const std::string_view text) : m_Text(text) {}

    std::expected<ExpressionProgram, std::string> Run() {
        SkipSpaces();
        if (m_Position >= m_Text.size()) {
            return std::unexpected("The expression is empty");
        }
        const int root = ParseTernary();
        if (root >= 0) {
            SkipSpaces();
            if (m_Position < m_Text.size()) {
                Fail(DescribeUnexpected());
            }
        }
        if (!m_Error.empty()) {
            return std::unexpected(m_Error);
        }
        m_Program.Root = root;
        return std::move(m_Program);
    }

private:
    std::string_view m_Text;
    std::size_t m_Position = 0;
    int m_Depth = 0;
    std::string m_Error;
    ExpressionProgram m_Program;

    // Character helpers
    static bool IsSpace(const char character) { return std::isspace(static_cast<unsigned char>(character)) != 0; }

    static bool IsDigit(const char character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; }

    static bool IsLetter(const char character) { return std::isalpha(static_cast<unsigned char>(character)) != 0; }

    static bool IsIdentifierCharacter(const char character) {
        return IsLetter(character) || IsDigit(character) || character == '_';
    }

    // Text
    char Peek(const std::size_t offset = 0) const {
        return m_Position + offset < m_Text.size() ? m_Text[m_Position + offset] : '\0';
    }

    void SkipSpaces() {
        while (m_Position < m_Text.size() && IsSpace(m_Text[m_Position])) {
            ++m_Position;
        }
    }

    bool Consume(const std::string_view token) {
        SkipSpaces();
        if (m_Text.substr(m_Position).starts_with(token)) {
            m_Position += token.size();
            return true;
        }
        return false;
    }

    // Errors and nodes
    int Fail(std::string message) {
        if (m_Error.empty()) {
            m_Error = std::move(message);
        }
        return -1;
    }

    std::string DescribeUnexpected() const {
        const char character = Peek();
        if (character == ')') {
            return "A \")\" closes no parenthesis";
        }
        return std::format("\"{}\" cannot be used here, at position {}", character, m_Position + 1);
    }

    bool Enter() {
        if (++m_Depth > MAX_NESTING_DEPTH) {
            Fail("The expression is nested too deeply");
            return false;
        }
        return true;
    }

    void Leave() { --m_Depth; }

    int AddNode(const ExpressionNode &node) {
        m_Program.Nodes.push_back(node);
        return static_cast<int>(m_Program.Nodes.size()) - 1;
    }

    int AddOperation(const Operation kind, const int first, const int second = -1, const int third = -1) {
        if (first < 0 || (second < 0 && third >= 0)) {
            return -1;
        }
        return AddNode({.Kind = kind, .First = first, .Second = second, .Third = third});
    }

    int AddBinary(const Operation kind, const int first, const int second) {
        return first < 0 || second < 0 ? -1 : AddOperation(kind, first, second);
    }

    // Precedence levels, lowest first
    int ParseTernary() {
        if (!Enter()) {
            return -1;
        }
        const int condition = ParseOr();
        int result = condition;
        if (condition >= 0 && Consume("?")) {
            const int then_branch = ParseTernary();
            if (then_branch >= 0 && !Consume(":")) {
                Fail("A \"?\" needs the \":\" of its alternative");
            } else if (then_branch >= 0) {
                const int else_branch = ParseTernary();
                result = else_branch < 0 ? -1 : AddOperation(Operation::TERNARY, condition, then_branch, else_branch);
            } else {
                result = -1;
            }
        }
        Leave();
        return result;
    }

    int ParseOr() {
        int left = ParseAnd();
        while (left >= 0 && Consume("||")) {
            left = AddBinary(Operation::OR, left, ParseAnd());
        }
        return left;
    }

    int ParseAnd() {
        int left = ParseEquality();
        while (left >= 0 && Consume("&&")) {
            left = AddBinary(Operation::AND, left, ParseEquality());
        }
        return left;
    }

    int ParseEquality() {
        int left = ParseRelational();
        while (left >= 0) {
            Operation kind = Operation::EQUAL;
            if (Consume("==")) {
                kind = Operation::EQUAL;
            } else if (Consume("!=") || Consume("<>")) {
                kind = Operation::NOT_EQUAL;
            } else {
                break;
            }
            left = AddBinary(kind, left, ParseRelational());
        }
        return left;
    }

    int ParseRelational() {
        int left = ParseAdditive();
        while (left >= 0) {
            SkipSpaces();
            Operation kind = Operation::LESS;
            if (Consume("<=")) {
                kind = Operation::LESS_EQUAL;
            } else if (Consume(">=")) {
                kind = Operation::GREATER_EQUAL;
            } else if (Peek() == '<' && Peek(1) != '>') {
                ++m_Position;
                kind = Operation::LESS;
            } else if (Peek() == '>') {
                ++m_Position;
                kind = Operation::GREATER;
            } else {
                break;
            }
            left = AddBinary(kind, left, ParseAdditive());
        }
        return left;
    }

    int ParseAdditive() {
        int left = ParseTerm();
        while (left >= 0) {
            if (Consume("+")) {
                left = AddBinary(Operation::ADD, left, ParseTerm());
            } else if (Consume("-")) {
                left = AddBinary(Operation::SUBTRACT, left, ParseTerm());
            } else {
                break;
            }
        }
        return left;
    }

    int ParseTerm() {
        int left = ParseUnary();
        while (left >= 0) {
            if (Consume("*")) {
                left = AddBinary(Operation::MULTIPLY, left, ParseUnary());
            } else if (Consume("/")) {
                left = AddBinary(Operation::DIVIDE, left, ParseUnary());
            } else {
                break;
            }
        }
        return left;
    }

    /**
     * @brief   A sign or a negation binds looser than the power, so -2^2 is -(2^2), as in ngspice.
     */
    int ParseUnary() {
        if (!Enter()) {
            return -1;
        }
        int result = 0;
        if (Consume("-")) {
            result = AddOperation(Operation::NEGATE, ParseUnary());
        } else if (Consume("+")) {
            result = ParseUnary();
        } else if (Peek() == '!' && Peek(1) != '=') {
            ++m_Position;
            result = AddOperation(Operation::NOT, ParseUnary());
        } else {
            result = ParsePower();
        }
        Leave();
        return result;
    }

    /**
     * @brief   Powers group from the left, so 2^3^2 is (2^3)^2 = 64, as in ngspice.
     */
    int ParsePower() {
        int base = ParsePrimary();
        while (base >= 0 && (Consume("**") || Consume("^"))) {
            base = AddBinary(Operation::POWER, base, ParsePowerOperand());
        }
        return base;
    }

    /**
     * @brief   The exponent may carry a sign, 2^-1, which then binds to the number alone.
     */
    int ParsePowerOperand() {
        if (!Enter()) {
            return -1;
        }
        int result = 0;
        if (Consume("-")) {
            result = AddOperation(Operation::NEGATE, ParsePowerOperand());
        } else if (Consume("+")) {
            result = ParsePowerOperand();
        } else {
            result = ParsePrimary();
        }
        Leave();
        return result;
    }

    // Operands
    int ParsePrimary() {
        SkipSpaces();
        const char character = Peek();
        if (IsDigit(character) || (character == '.' && IsDigit(Peek(1)))) {
            return ParseNumber();
        }
        if (IsLetter(character) || character == '_') {
            return ParseName();
        }
        if (character == '(') {
            ++m_Position;
            const int inner = ParseTernary();
            if (inner >= 0 && !Consume(")")) {
                return Fail("A parenthesis is not closed");
            }
            return inner;
        }
        if (m_Position >= m_Text.size()) {
            return Fail("The expression ends where an operand is expected");
        }
        return Fail(DescribeUnexpected());
    }

    /**
     * @brief   A number with its SPICE suffix: digits, a point, an exponent when digits follow it, then the letters of
     * the scale and of any unit, which SPICE reads and ignores after the scale.
     */
    int ParseNumber() {
        const std::size_t start = m_Position;
        while (IsDigit(Peek())) {
            ++m_Position;
        }
        if (Peek() == '.') {
            ++m_Position;
            while (IsDigit(Peek())) {
                ++m_Position;
            }
        }
        const bool has_signed_exponent = (Peek(1) == '+' || Peek(1) == '-') && IsDigit(Peek(2));
        if ((Peek() == 'e' || Peek() == 'E') && (IsDigit(Peek(1)) || has_signed_exponent)) {
            m_Position += 2;
            while (IsDigit(Peek())) {
                ++m_Position;
            }
        }
        while (IsLetter(Peek())) {
            ++m_Position;
        }
        const std::string_view text = m_Text.substr(start, m_Position - start);
        const std::optional<double> value = ParseSpiceNumber(text);
        if (!value) {
            return Fail(std::format("\"{}\" is not a number", text));
        }
        return AddNode({.Kind = Operation::CONSTANT, .Constant = *value});
    }

    /**
     * @brief   A reference, a function call, a constant or a variable.
     */
    int ParseName() {
        const std::size_t start = m_Position;
        while (IsIdentifierCharacter(Peek())) {
            ++m_Position;
        }
        const std::string_view written = m_Text.substr(start, m_Position - start);
        const std::string name = ToLower(written);
        const std::size_t after_name = m_Position;
        SkipSpaces();
        if (Peek() != '(') {
            m_Position = after_name;
            return ParseWord(written, name);
        }
        if (name == "v" || name == "i") {
            return ParseReference(start, name == "v");
        }
        return ParseCall(written, name);
    }

    int ParseWord(const std::string_view written, const std::string &name) {
        if (const std::optional<double> constant = LookupBuiltinConstant(name)) {
            return AddNode({.Kind = Operation::CONSTANT, .Constant = *constant});
        }
        if (name == "time") {
            m_Program.IsTimeDependent = true;
            return AddNode({.Kind = Operation::TIME});
        }
        if (name == "temper") {
            return AddNode({.Kind = Operation::TEMPERATURE});
        }
        if (name == "hertz") {
            return Fail(std::string(HERTZ_REASON));
        }
        if (name == "v" || name == "i") {
            return Fail(std::format("\"{}\" is followed by the name it reads, as in {}(NAME)", written, name));
        }
        if (LookupExpressionFunction(name).has_value()) {
            return Fail(std::format("\"{}\" is a function and takes its operands in parentheses", written));
        }
        return Fail(std::format("\"{}\" is not a number, a constant (pi, e), a variable (time, temper), v(...) or "
                                "i(...)",
                                written));
    }

    /**
     * @brief   Reads `v(node)`, `v(node, reference)` or `i(source)`; the names go up to the closing parenthesis
     * unchanged.
     */
    int ParseReference(const std::size_t start, const bool is_voltage) {
        const std::size_t open = m_Position;
        const std::size_t close = m_Text.find(')', open);
        if (close == std::string_view::npos) {
            return Fail("A parenthesis is not closed");
        }
        const std::string_view arguments = m_Text.substr(open + 1, close - open - 1);
        if (arguments.contains('(')) {
            return Fail(std::format("{}() takes the name of {}, not an expression",
                                    is_voltage ? "v" : "i",
                                    is_voltage ? "a node" : "a source"));
        }
        const std::optional<ExpressionInput> input = ReadReferenceInput(arguments, is_voltage);
        if (!input) {
            return Fail(is_voltage ? "v() takes one or two node names, such as v(out) or v(in, out)"
                                   : "i() takes the name of one source, such as i(vsense)");
        }
        m_Position = close + 1;
        m_Program.References.push_back({.Input = *input, .Start = start, .Length = m_Position - start});
        return AddNode({.Kind = Operation::INPUT, .Input = FindOrAddInput(*input)});
    }

    int FindOrAddInput(const ExpressionInput &input) {
        const auto found = std::ranges::find_if(m_Program.Inputs, [&input](const ExpressionInput &known) {
            return IsSameExpressionInput(known, input);
        });
        if (found != m_Program.Inputs.end()) {
            return static_cast<int>(found - m_Program.Inputs.begin());
        }
        m_Program.Inputs.push_back(input);
        return static_cast<int>(m_Program.Inputs.size()) - 1;
    }

    int ReadCallOperands(std::array<int, 2> &operands) {
        int count = 0;
        if (Consume(")")) {
            return count;
        }
        while (true) {
            const int operand = ParseTernary();
            if (operand < 0) {
                return -1;
            }
            if (count < 2) {
                operands[static_cast<std::size_t>(count)] = operand;
            }
            ++count;
            if (Consume(",")) {
                continue;
            }
            if (!Consume(")")) {
                return Fail("A parenthesis is not closed");
            }
            return count;
        }
    }

    /**
     * @brief   Reads a function and its operands, `max(a, b)`.
     */
    int ParseCall(const std::string_view written, const std::string &name) {
        if (const std::optional<std::string> reason = LookupRefusedFunctionReason(written, name)) {
            return Fail(*reason);
        }
        const std::optional<FunctionEntry> function = LookupExpressionFunction(name);
        if (!function) {
            return Fail(std::format("\"{}\" is not a function Nodalys knows", written));
        }
        ++m_Position;
        std::array<int, 2> operands{-1, -1};
        const int count = ReadCallOperands(operands);
        if (count < 0) {
            return -1;
        }
        if (count != function->OperandCount) {
            return Fail(std::format("{}() takes {} operand{}, not {}",
                                    written,
                                    function->OperandCount,
                                    function->OperandCount == 1 ? "" : "s",
                                    count));
        }
        return AddOperation(function->Kind, operands[0], operands[1]);
    }
};

} // namespace

/**
 * @brief   Reads the text of an expression.
 * @param[in] text  Expression such as "v(1) * v(2)", "1m * tanh(v(in) / 26m)" or "i(vsense) > 1 ? 0 : 5".
 * @return  The compact form of the expression, or a message that says what is wrong and names what the language does
 *          not have: an unknown function, a random function, `hertz`, a parenthesis that does not close or an operand
 *          that is missing.
 */
std::expected<ExpressionProgram, std::string> ParseExpressionProgram(const std::string_view text) {
    return Parser(text).Run();
}

} // namespace Nodalys
