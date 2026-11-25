/**
 * @file    classified_lines.cpp
 * @brief   Sorts the lines of a subcircuit kept as SPICE text by what they do: parameters, models, nested subcircuits
 *          and elements.
 */

#include "parser/syntax/classified_lines.h"

#include "parser/subcircuit/subcircuit_lines.h"
#include "parser/syntax/spice_text.h"
#include "text/text_case.h"

#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace Nodalys {

namespace {

/**
 * @brief   Reads the .subckt line of a nested definition: its name, its ports and its parameters with their defaults.
 */
std::expected<TextSubcircuit, std::string> ReadSubcircuitHeader(const std::vector<std::string> &tokens) {
    if (tokens.size() < 2) {
        return std::unexpected("a .subckt line needs a name");
    }
    TextSubcircuit subcircuit;
    subcircuit.Name = tokens[1];
    std::size_t index = 2;
    while (index < tokens.size() && !IsParameterStart(tokens[index])) {
        subcircuit.Ports.push_back(tokens[index]);
        ++index;
    }
    for (auto &[name, text] : SplitParameterTokens(tokens, index)) {
        if (text.empty()) {
            return std::unexpected(std::format("the parameter {} has no default value", name));
        }
        subcircuit.Parameters.emplace_back(std::move(name), std::move(text));
    }
    return subcircuit;
}

/**
 * @brief   Tells how many lines a nested .subckt takes, from the line after its header to its .ends.
 * @return  The number of lines of its body, or none when it never closes.
 */
std::optional<std::size_t> FindBodyLength(const std::vector<std::string> &lines, const std::size_t first) {
    int depth = 1;
    for (std::size_t index = first; index < lines.size(); ++index) {
        const std::vector<std::string> tokens = SplitTokens(lines[index]);
        const std::string directive = tokens.empty() ? std::string() : ToUpper(tokens[0]);
        if (directive == ".SUBCKT") {
            ++depth;
        } else if (directive == ".ENDS" && --depth == 0) {
            return index - first;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Reads the nested .subckt that starts at a line, with the lines of its body up to its .ends.
 */
std::expected<TextSubcircuit, RefusedLine> ReadNestedSubcircuit(const std::vector<std::string> &lines,
                                                                const std::vector<std::string> &tokens,
                                                                const std::size_t header) {
    auto subcircuit = ReadSubcircuitHeader(tokens);
    if (!subcircuit) {
        return std::unexpected(
            RefusedLine{.Line = lines[header], .Reason = "is not understood: " + subcircuit.error()});
    }
    const std::optional<std::size_t> length = FindBodyLength(lines, header + 1);
    if (!length) {
        return std::unexpected(RefusedLine{.Line = lines[header], .Reason = "starts a subcircuit that has no .ends"});
    }
    subcircuit->Lines.assign(lines.begin() + static_cast<std::ptrdiff_t>(header + 1),
                             lines.begin() + static_cast<std::ptrdiff_t>(header + 1 + *length));
    return std::move(*subcircuit);
}

/**
 * @brief   Keeps an element line, and the name of a voltage source among them.
 */
void AddElement(ClassifiedLines &classified, const std::string &line, std::vector<std::string> tokens) {
    if (std::toupper(static_cast<unsigned char>(tokens[0].front())) == 'V') {
        classified.VoltageSources.push_back(tokens[0]);
    }
    classified.Elements.push_back({.Line = line, .Tokens = std::move(tokens)});
}

} // namespace

/**
 * @brief   Sorts the lines of a text, each split into its tokens once.
 * @details A nested .subckt takes the lines up to its .ends, which are kept with it and not sorted here: they are
 *          sorted when an X line expands it. A .end line is skipped.
 * @param[in] lines  The logical lines of the text.
 * @return  The sorted lines, or the line that cannot be read: a directive the engine does not read or a .subckt that is
 *          not understood or never closes.
 */
std::expected<ClassifiedLines, RefusedLine> ClassifyLines(const std::vector<std::string> &lines) {
    ClassifiedLines classified;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::vector<std::string> tokens = SplitTokens(lines[index]);
        const std::string directive = ToUpper(tokens[0]);
        if (directive == ".SUBCKT") {
            auto subcircuit = ReadNestedSubcircuit(lines, tokens, index);
            if (!subcircuit) {
                return std::unexpected(subcircuit.error());
            }
            index += subcircuit->Lines.size() + 1;
            classified.Subcircuits.push_back(std::move(*subcircuit));
        } else if (directive == ".PARAM") {
            classified.Parameters.push_back(std::move(tokens));
        } else if (directive == ".MODEL") {
            classified.Models.push_back(std::move(tokens));
        } else if (directive == ".END") {
            continue;
        } else if (directive.starts_with('.')) {
            return std::unexpected(
                RefusedLine{.Line = lines[index], .Reason = "uses a directive that Nodalys does not read"});
        } else {
            AddElement(classified, lines[index], std::move(tokens));
        }
    }
    return classified;
}

} // namespace Nodalys
