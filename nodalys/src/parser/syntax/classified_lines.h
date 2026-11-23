/**
 * @file    classified_lines.h
 * @brief   Sorts the lines of a subcircuit kept as SPICE text by what they do: parameters, models, nested subcircuits
 *          and elements.
 */

#ifndef NODALYS_PARSER_CLASSIFIED_LINES_H
#define NODALYS_PARSER_CLASSIFIED_LINES_H

#include "parser/subcircuit/subcircuit_scope.h"

#include <expected>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  TextElement
 * @brief   An element line of the text, with its tokens split once.
 */
struct TextElement {
    std::string Line;
    std::vector<std::string> Tokens;
};

/**
 * @struct  ClassifiedLines
 * @brief   The lines of a text sorted by what they do, since the parameters and models come before the elements that
 *          use them whatever the order they are written in.
 */
struct ClassifiedLines {
    /// The tokens of each .param line, in order.
    std::vector<std::vector<std::string>> Parameters;
    /// The tokens of each .model card, in order.
    std::vector<std::vector<std::string>> Models;
    /// The .subckt blocks written in the text, with their lines; the scope they sit in is left for the caller to set.
    std::vector<TextSubcircuit> Subcircuits;
    std::vector<TextElement> Elements;
    /// The names of the voltage sources among the elements, as written, which a controlled source or an expression
    /// may name to read a current, even from a line written before the source.
    std::vector<std::string> VoltageSources;
};

/**
 * @struct  RefusedLine
 * @brief   A line of the text that cannot be read, with the reason, for the caller to name the subcircuit around it.
 */
struct RefusedLine {
    std::string Line;
    std::string Reason;
};

// Sorting
std::expected<ClassifiedLines, RefusedLine> ClassifyLines(const std::vector<std::string> &lines);

} // namespace Nodalys

#endif // NODALYS_PARSER_CLASSIFIED_LINES_H
