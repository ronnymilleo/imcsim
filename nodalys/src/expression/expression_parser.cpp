/**
 * @file    expression_parser.cpp
 * @brief   Parses the text of an expression into the compact form that is evaluated.
 */

#include "expression/expression_parser.h"

#include "expression/expression_math.h"
#include "parser/spice_number.h"
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

/// A function the language knows, and how many operands it takes.
struct FunctionEntry {
    std::string_view Name;
    Operation Kind;
    int OperandCount;
};

constexpr auto FUNCTIONS = std::to_array<FunctionEntry>({
    {"abs", Operation::ABS, 1},
    {"sqrt", Operation::SQRT, 1},
    {"exp", Operation::EXP, 1},
    // ngspice reads log() as the natural logarithm, like ln(), and log10() as the decimal one
    {"ln", Operation::LN, 1},
    {"log", Operation::LN, 1},
    {"log10", Operation::LOG10, 1},
    {"sin", Operation::SIN, 1},
    {"cos", Operation::COS, 1},
    {"tan", Operation::TAN, 1},
    {"asin", Operation::ASIN, 1},
    {"acos", Operation::ACOS, 1},
    {"atan", Operation::ATAN, 1},
    {"sinh", Operation::SINH, 1},
    {"cosh", Operation::COSH, 1},
    {"tanh", Operation::TANH, 1},
    {"asinh", Operation::ASINH, 1},
    {"acosh", Operation::ACOSH, 1},
    {"atanh", Operation::ATANH, 1},
    {"sgn", Operation::SIGN, 1},
    {"u", Operation::STEP, 1},
    {"u2", Operation::RAMP_STEP, 1},
    {"uramp", Operation::RAMP, 1},
    {"floor", Operation::FLOOR, 1},
    {"ceil", Operation::CEIL, 1},
    {"nint", Operation::NEAREST_INTEGER, 1},
    {"min", Operation::MIN, 2},
    {"max", Operation::MAX, 2},
    {"pow", Operation::POW, 2},
    {"pwr", Operation::PWR, 2},
});

/// A name that ngspice reads and Nodalys does not, with what to tell the user.
struct RefusedName {
    std::string_view Name;
    std::string_view Reason;
};

constexpr std::string_view RANDOM_REASON = "draws random numbers, which Nodalys does not run in an expression";

constexpr auto REFUSED_FUNCTIONS = std::to_array<RefusedName>({
    {"limit", "limit() is a Monte Carlo function of ngspice (the nominal value plus or minus a deviation, chosen at "
              "random), not a clamp; clamp with min(max(x, low), high)"},
    {"atan2", "atan2() is not available in a behavioral expression of ngspice either; use atan(y / x)"},
    {"gauss", RANDOM_REASON},
    {"agauss", RANDOM_REASON},
    {"unif", RANDOM_REASON},
    {"aunif", RANDOM_REASON},
    {"rnd", RANDOM_REASON},
    {"random", RANDOM_REASON},
    {"exponential", RANDOM_REASON},
});

constexpr std::string_view HERTZ_REASON =
    "hertz, the frequency, is not supported by Nodalys in an expression: it would make the small-signal gain depend on "
    "the frequency, which an AC sweep cannot stamp once";

constexpr double NOMINAL_E = 2.718281828459045;
constexpr double NOMINAL_PI = 3.141592653589793;

std::string LowerCase(std::string_view text) {
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(),
                           [](const char character) { return static_cast<char>(std::tolower(character)); });
    return lowered;
}

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

bool IsDigit(const char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool IsLetter(const char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0;
}

bool IsIdentifierCharacter(const char character) {
    return IsLetter(character) || IsDigit(character) || character == '_';
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool IsSameInput(const ExpressionInput &first, const ExpressionInput &second) {
    return first.Type == second.Type && LowerCase(first.First) == LowerCase(second.First) &&
           LowerCase(first.Second) == LowerCase(second.Second);
}

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

    /// A sign or a negation binds looser than the power, so -2^2 is -(2^2), as in ngspice.
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

    /// Powers group from the left, so 2^3^2 is (2^3)^2 = 64, as in ngspice.
    int ParsePower() {
        int base = ParsePrimary();
        while (base >= 0 && (Consume("**") || Consume("^"))) {
            base = AddBinary(Operation::POWER, base, ParsePowerOperand());
        }
        return base;
    }

    /// The exponent may carry a sign, 2^-1, which then binds to the number alone.
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

    /// A number with its SPICE suffix: digits, a point, an exponent when digits follow it, then the letters of the
    /// scale and of any unit, which SPICE reads and ignores after the scale.
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

    /// A reference, a function call, a constant or a variable.
    int ParseName() {
        const std::size_t start = m_Position;
        while (IsIdentifierCharacter(Peek())) {
            ++m_Position;
        }
        const std::string_view written = m_Text.substr(start, m_Position - start);
        const std::string name = LowerCase(written);
        const std::size_t after_name = m_Position;
        SkipSpaces();
        const bool has_parenthesis = Peek() == '(';
        if (!has_parenthesis) {
            m_Position = after_name;
            return ParseWord(written, name);
        }
        if (name == "v" || name == "i") {
            return ParseReference(start, name == "v");
        }
        return ParseCall(written, name);
    }

    int ParseWord(const std::string_view written, const std::string &name) {
        if (name == "pi") {
            return AddNode({.Kind = Operation::CONSTANT, .Constant = NOMINAL_PI});
        }
        if (name == "e") {
            return AddNode({.Kind = Operation::CONSTANT, .Constant = NOMINAL_E});
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
        const bool is_function =
            std::ranges::any_of(FUNCTIONS, [&name](const FunctionEntry &entry) { return entry.Name == name; });
        if (is_function) {
            return Fail(std::format("\"{}\" is a function and takes its operands in parentheses", written));
        }
        return Fail(std::format("\"{}\" is not a number, a constant (pi, e), a variable (time, temper), v(...) or "
                                "i(...)",
                                written));
    }

    /// Reads `v(node)`, `v(node, reference)` or `i(source)`; the names go up to the closing parenthesis unchanged.
    int ParseReference(const std::size_t start, const bool is_voltage) {
        const std::size_t open = m_Position;
        const std::size_t close = m_Text.find(')', open);
        if (close == std::string_view::npos) {
            return Fail("A parenthesis is not closed");
        }
        const std::string_view arguments = m_Text.substr(open + 1, close - open - 1);
        const std::string_view reader = is_voltage ? "v" : "i";
        if (arguments.contains('(')) {
            return Fail(std::format("{}() takes the name of {}, not an expression", reader,
                                    is_voltage ? "a node" : "a source"));
        }
        ExpressionInput input{.Type = is_voltage ? ExpressionInput::Kind::VOLTAGE : ExpressionInput::Kind::CURRENT};
        const std::size_t comma = arguments.find(',');
        input.First = std::string(Trim(arguments.substr(0, comma)));
        if (comma != std::string_view::npos) {
            input.Second = std::string(Trim(arguments.substr(comma + 1)));
        }
        const auto has_space = [](const std::string &name) { return std::ranges::any_of(name, IsSpace); };
        const bool is_malformed = input.First.empty() || has_space(input.First) || has_space(input.Second) ||
                                  (comma != std::string_view::npos && input.Second.empty()) ||
                                  input.Second.contains(',') || (!is_voltage && comma != std::string_view::npos);
        if (is_malformed) {
            return Fail(is_voltage ? "v() takes one or two node names, such as v(out) or v(in, out)"
                                   : "i() takes the name of one source, such as i(vsense)");
        }
        m_Position = close + 1;
        m_Program.References.push_back({.Input = input, .Start = start, .Length = m_Position - start});
        return AddNode({.Kind = Operation::INPUT, .Input = FindOrAddInput(input)});
    }

    int FindOrAddInput(const ExpressionInput &input) {
        const auto found = std::ranges::find_if(
            m_Program.Inputs, [&input](const ExpressionInput &known) { return IsSameInput(known, input); });
        if (found != m_Program.Inputs.end()) {
            return static_cast<int>(found - m_Program.Inputs.begin());
        }
        m_Program.Inputs.push_back(input);
        return static_cast<int>(m_Program.Inputs.size()) - 1;
    }

    /// Reads a function and its operands, `max(a, b)`.
    int ParseCall(const std::string_view written, const std::string &name) {
        const auto refused =
            std::ranges::find_if(REFUSED_FUNCTIONS, [&name](const RefusedName &entry) { return entry.Name == name; });
        if (refused != REFUSED_FUNCTIONS.end()) {
            const bool is_random = refused->Reason == RANDOM_REASON;
            return Fail(is_random ? std::format("{}() {}", written, RANDOM_REASON) : std::string(refused->Reason));
        }
        const auto function =
            std::ranges::find_if(FUNCTIONS, [&name](const FunctionEntry &entry) { return entry.Name == name; });
        if (function == FUNCTIONS.end()) {
            return Fail(std::format("\"{}\" is not a function Nodalys knows", written));
        }
        ++m_Position;
        std::array<int, 2> operands{-1, -1};
        int count = 0;
        if (!Consume(")")) {
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
                break;
            }
        }
        if (count != function->OperandCount) {
            return Fail(std::format("{}() takes {} operand{}, not {}", written, function->OperandCount,
                                    function->OperandCount == 1 ? "" : "s", count));
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
