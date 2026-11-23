/**
 * @file    source_line.h
 * @brief   Reads the words of an independent source line after its nodes, "[DC] <value> [AC <magnitude> [<phase>]]"
 *          and a waveform, the same way for a netlist and for a subcircuit kept as SPICE text.
 */

#ifndef NODALYS_PARSER_SOURCE_LINE_H
#define NODALYS_PARSER_SOURCE_LINE_H

#include "parser/sources/source_waveforms.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/// Reads one number of a source line: a literal in a netlist, or an expression of parameters in a subcircuit text.
using SourceNumberReader = std::function<std::expected<double, std::string>(std::string_view text)>;

/**
 * @enum    SourceWordPolicy
 * @brief   What a source line does with what it cannot read.
 * @details SKIP_UNKNOWN is the netlist, which reads as much as it can: a word it does not know and a number of a
 *          waveform that does not read are skipped, a DC or AC word whose number does not read takes the default of
 *          the word, and a bare number anywhere is the DC value. REFUSE_UNKNOWN is the subcircuit text, which never
 *          drops a word without a message: any of those is an error, and a bare value is only read right after the
 *          nodes.
 */
enum class SourceWordPolicy {
    SKIP_UNKNOWN,
    REFUSE_UNKNOWN
};

/**
 * @struct  SourceValues
 * @brief   What an independent source line gives: its DC value, its small-signal magnitude and phase, and its
 *          waveform with the numbers in the parentheses.
 */
struct SourceValues {
    SourceLevels Levels;
    SourceWaveform Waveform = SourceWaveform::NONE;
    std::vector<double> WaveformValues;
};

/**
 * @class   SourceLineReader
 * @brief   Walks the words of a source line after its nodes, one word with the values that belong to it at a time.
 * @details The caller may read words of its own between them, such as the port options of a netlist, by looking at
 *          GetIndex() and moving past them with Skip().
 */
class SourceLineReader {
public:
    SourceLineReader(const std::vector<std::string> &tokens,
                     std::size_t first,
                     SourceNumberReader read_number,
                     SourceWordPolicy policy);

    // Position
    bool IsAtEnd() const;
    std::size_t GetIndex() const;
    void Skip(std::size_t count);

    // Words
    [[nodiscard]] std::expected<void, std::string> ReadWord(SourceValues &values);

private:
    using Result = std::expected<void, std::string>;

    const std::vector<std::string> &m_Tokens;
    /// Index of the first word after the nodes, the only place a bare value may sit when unknown words are refused.
    std::size_t m_First;
    std::size_t m_Index;
    SourceNumberReader m_ReadNumber;
    SourceWordPolicy m_Policy;

    // Words
    Result ReadDc(SourceValues &values);
    Result ReadDcAssignment(SourceValues &values);
    Result ReadAc(SourceValues &values);
    Result ReadWaveform(SourceWaveform waveform, SourceValues &values);
    Result ReadBareValue(SourceValues &values);

    // Numbers
    std::expected<double, std::string> ReadNumberOr(std::string_view text, double fallback) const;
    bool HasNext() const;
    bool IsSkippingUnknown() const;
};

// Lines
[[nodiscard]] std::expected<SourceValues, std::string> ReadSourceLine(const std::vector<std::string> &tokens,
                                                                      std::size_t first,
                                                                      const SourceNumberReader &read_number,
                                                                      SourceWordPolicy policy);

} // namespace Nodalys

#endif // NODALYS_PARSER_SOURCE_LINE_H
