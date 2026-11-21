/**
 * @file    source_line.cpp
 * @brief   Reads the words of an independent source line after its nodes, "[DC] <value> [AC <magnitude> [<phase>]]"
 *          and a waveform, the same way for a netlist and for a subcircuit kept as SPICE text.
 */

#include "parser/source_line.h"

#include "parser/spice_text.h"
#include "text/text_case.h"

#include <format>
#include <utility>

namespace Nodalys {

namespace {

/// The DC value a DC word takes when its number does not read and the line skips what it cannot read.
constexpr double DEFAULT_DC = 0.0;
/// The magnitude an AC word takes when its number does not read and the line skips what it cannot read, the unit
/// magnitude SPICE gives an AC word alone.
constexpr double DEFAULT_AC_MAGNITUDE = 1.0;
/// The length of "DC=", the form that writes the DC value in the same word.
constexpr std::size_t DC_ASSIGNMENT_LENGTH = 3;

} // namespace

/**
 * @param[in] tokens       Tokens of the line; they must outlive the reader.
 * @param[in] first        Index of the first word after the nodes.
 * @param[in] read_number  Reads one number of the line.
 * @param[in] policy       What the line does with what it cannot read.
 */
SourceLineReader::SourceLineReader(const std::vector<std::string> &tokens,
                                   const std::size_t first,
                                   SourceNumberReader read_number,
                                   const SourceWordPolicy policy)
    : m_Tokens(tokens), m_First(first), m_Index(first), m_ReadNumber(std::move(read_number)), m_Policy(policy) {
}

/**
 * @brief   Tells whether every word of the line has been read.
 */
bool SourceLineReader::IsAtEnd() const {
    return m_Index >= m_Tokens.size();
}

/**
 * @brief   The index of the word the reader reads next.
 */
std::size_t SourceLineReader::GetIndex() const {
    return m_Index;
}

/**
 * @brief   Moves past words the caller read itself.
 */
void SourceLineReader::Skip(const std::size_t count) {
    m_Index += count;
}

/**
 * @brief   Reads the next word of the line with the values that belong to it, and moves past them.
 * @param[in,out] values  What the line gives so far; the word sets its part of it.
 * @return  Nothing, or why the word cannot be read when the policy refuses what it does not know: anything but a DC
 *          value, an AC value or a waveform, or a number that does not read.
 * @note    The reader must not be at its end.
 */
std::expected<void, std::string> SourceLineReader::ReadWord(SourceValues &values) {
    const std::string upper = ToUpper(m_Tokens[m_Index]);
    if (upper == "DC" && HasNext()) {
        return ReadDc(values);
    }
    if (upper.starts_with("DC=")) {
        return ReadDcAssignment(values);
    }
    if (upper == "AC" && HasNext()) {
        return ReadAc(values);
    }
    if (const SourceWaveform waveform = FindSourceWaveform(m_Tokens[m_Index]); waveform != SourceWaveform::NONE) {
        return ReadWaveform(waveform, values);
    }
    return ReadBareValue(values);
}

/**
 * @brief   Reads "DC <value>".
 */
SourceLineReader::Result SourceLineReader::ReadDc(SourceValues &values) {
    const auto value = ReadNumberOr(m_Tokens[m_Index + 1], DEFAULT_DC);
    m_Index += 2;
    if (!value) {
        return std::unexpected(value.error());
    }
    values.Levels.Dc = *value;
    return {};
}

/**
 * @brief   Reads "DC=<value>", the DC value written in the same word.
 */
SourceLineReader::Result SourceLineReader::ReadDcAssignment(SourceValues &values) {
    const auto value = ReadNumberOr(std::string_view(m_Tokens[m_Index]).substr(DC_ASSIGNMENT_LENGTH), DEFAULT_DC);
    ++m_Index;
    if (!value) {
        return std::unexpected(value.error());
    }
    values.Levels.Dc = *value;
    return {};
}

/**
 * @brief   Reads "AC <magnitude>" and the phase when the word after it reads as a number.
 * @details The word after the magnitude is read once: when it is no number it is left for the next word.
 */
SourceLineReader::Result SourceLineReader::ReadAc(SourceValues &values) {
    const auto magnitude = ReadNumberOr(m_Tokens[m_Index + 1], DEFAULT_AC_MAGNITUDE);
    m_Index += 2;
    if (!magnitude) {
        return std::unexpected(magnitude.error());
    }
    values.Levels.AcMagnitude = *magnitude;
    if (IsAtEnd()) {
        return {};
    }
    if (const auto phase = m_ReadNumber(m_Tokens[m_Index])) {
        values.Levels.AcPhaseDegrees = *phase;
        ++m_Index;
    }
    return {};
}

/**
 * @brief   Reads a waveform such as "PULSE(0 1 1n)", with the numbers inside its parentheses.
 * @details A later waveform replaces an earlier one, with its numbers.
 */
SourceLineReader::Result SourceLineReader::ReadWaveform(const SourceWaveform waveform, SourceValues &values) {
    const std::string &token = m_Tokens[m_Index];
    ++m_Index;
    values.Waveform = waveform;
    values.WaveformValues.clear();
    const std::size_t open = token.find('(');
    const std::size_t close = token.rfind(')');
    if (close == std::string::npos || close < open) {
        if (IsSkippingUnknown()) {
            return {};
        }
        return std::unexpected(std::format("the waveform '{}' has no closing parenthesis", token));
    }
    for (const std::string &text : SplitTokens(std::string_view(token).substr(open + 1, close - open - 1))) {
        const auto value = m_ReadNumber(text);
        if (value) {
            values.WaveformValues.push_back(*value);
        } else if (!IsSkippingUnknown()) {
            return std::unexpected(value.error());
        }
    }
    return {};
}

/**
 * @brief   Reads a word that is no DC, AC or waveform word: the DC value written alone, or a word the line does not
 *          know.
 */
SourceLineReader::Result SourceLineReader::ReadBareValue(SourceValues &values) {
    const std::string &token = m_Tokens[m_Index];
    if (IsSkippingUnknown()) {
        ++m_Index;
        if (const auto value = m_ReadNumber(token)) {
            values.Levels.Dc = *value;
        }
        return {};
    }
    if (m_Index == m_First && token.find('(') == std::string::npos) {
        ++m_Index;
        const auto value = m_ReadNumber(token);
        if (!value) {
            return std::unexpected(value.error());
        }
        values.Levels.Dc = *value;
        return {};
    }
    const std::string word = token.substr(0, token.find('('));
    return std::unexpected(std::format("the source word or option '{}' is not simulated inside a "
                                       "subcircuit kept as text; only DC and AC values and the waveforms are",
                                       word));
}

/**
 * @brief   Reads the number of a DC or AC word, which takes the default of the word when it does not read and the
 *          line skips what it cannot read.
 */
std::expected<double, std::string> SourceLineReader::ReadNumberOr(const std::string_view text,
                                                                  const double fallback) const {
    auto value = m_ReadNumber(text);
    if (!value && IsSkippingUnknown()) {
        return fallback;
    }
    return value;
}

/**
 * @brief   Tells whether a word follows the one the reader reads next, which a DC or AC word needs for its number.
 */
bool SourceLineReader::HasNext() const {
    return m_Index + 1 < m_Tokens.size();
}

bool SourceLineReader::IsSkippingUnknown() const {
    return m_Policy == SourceWordPolicy::SKIP_UNKNOWN;
}

/**
 * @brief   Reads every word of a source line after its nodes.
 * @param[in] tokens       Tokens of the line.
 * @param[in] first        Index of the first word after the nodes.
 * @param[in] read_number  Reads one number of the line.
 * @param[in] policy       What the line does with what it cannot read.
 * @return  What the line gives, or the first word that cannot be read when the policy refuses what it does not know.
 */
std::expected<SourceValues, std::string> ReadSourceLine(const std::vector<std::string> &tokens,
                                                        const std::size_t first,
                                                        const SourceNumberReader &read_number,
                                                        const SourceWordPolicy policy) {
    SourceLineReader reader(tokens, first, read_number, policy);
    SourceValues values;
    while (!reader.IsAtEnd()) {
        if (const auto read = reader.ReadWord(values); !read) {
            return std::unexpected(read.error());
        }
    }
    return values;
}

} // namespace Nodalys
