/**
 * @file    spice_library.cpp
 * @brief   Reads the subcircuits of a SPICE library file, such as a vendor's op-amp models, as text to keep.
 */

#include "spice_library.h"

#include "components/subcircuit.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace Core {

namespace {

// A statement of the file: its physical lines, continuations included, and the same text as one line without comments
struct Statement {
    std::size_t FirstLine = 0;
    std::size_t LastLine = 0;
    std::string Logical;
};

// A definition the file makes outside any subcircuit, which a subcircuit may use by name
struct Definition {
    std::size_t Order = 0;
    std::string Text;
};

struct Scale {
    std::string_view Suffix;
    double Factor;
};

// SPICE scale suffixes, matched without regard to case; "meg" and "mil" come before "m", which is milli
constexpr auto SCALES = std::to_array<Scale>({{"meg", 1e6},
                                              {"mil", 25.4e-6},
                                              {"t", 1e12},
                                              {"g", 1e9},
                                              {"k", 1e3},
                                              {"m", 1e-3},
                                              {"u", 1e-6},
                                              {"n", 1e-9},
                                              {"p", 1e-12},
                                              {"f", 1e-15}});

// What vendors write around encrypted models, which no other simulator can read
constexpr auto ENCRYPTION_MARKERS = std::to_array<std::string_view>({"$cdnencstart", "pspice_encrypted"});

std::string ToLower(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string> SplitLines(const std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.emplace_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

// ngspice ends a line at ";", and at "$" when it follows a space or a tab
std::string_view StripComment(std::string_view line) {
    if (const std::size_t semicolon = line.find(';'); semicolon != std::string_view::npos) {
        line = line.substr(0, semicolon);
    }
    for (std::size_t position = 1; position < line.size(); ++position) {
        if (line[position] == '$' && (line[position - 1] == ' ' || line[position - 1] == '\t')) {
            return line.substr(0, position);
        }
    }
    return line;
}

// Joins each line with the "+" lines that continue it; comment lines and blank lines belong to no statement
std::vector<Statement> ReadStatements(const std::vector<std::string> &lines) {
    std::vector<Statement> statements;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string_view line = Trim(lines[index]);
        if (line.empty() || line.front() == '*') {
            continue;
        }
        const std::string_view content = Trim(StripComment(line));
        if (line.front() == '+' && !statements.empty()) {
            statements.back().Logical += ' ';
            statements.back().Logical += Trim(content.substr(1));
            statements.back().LastLine = index;
            continue;
        }
        statements.push_back({.FirstLine = index, .LastLine = index, .Logical = std::string(content)});
    }
    return statements;
}

// Splits on spaces, with "name = value" read as one "name=value" token
std::vector<std::string> Tokenize(const std::string_view logical) {
    std::string text;
    for (const char character : logical) {
        if (character == '=') {
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
        }
        if ((character == ' ' || character == '\t') && !text.empty() && text.back() == '=') {
            continue;
        }
        text += character == '\t' ? ' ' : character;
    }
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find(' ', start);
        if (end != start) {
            tokens.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return tokens;
}

std::string JoinLines(const std::vector<std::string> &lines, const std::size_t first, const std::size_t last) {
    std::string text;
    for (std::size_t index = first; index <= last && index < lines.size(); ++index) {
        text += lines[index];
        text += '\n';
    }
    return text;
}

bool IsNameCharacter(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-';
}

// The names a text could refer to, lowercase, since SPICE ignores case
std::set<std::string> CollectNames(const std::string_view text) {
    std::set<std::string> names;
    for (std::size_t position = 0; position < text.size();) {
        if (!IsNameCharacter(text[position])) {
            ++position;
            continue;
        }
        const std::size_t start = position;
        while (position < text.size() && IsNameCharacter(text[position])) {
            ++position;
        }
        names.insert(ToLower(text.substr(start, position - start)));
    }
    return names;
}

// The header of a .subckt: its ports, then its parameters after "params:" or from the first "name=value"
void ReadHeader(const std::vector<std::string> &tokens, LibrarySubcircuit &subcircuit, std::string &expressions) {
    bool are_parameters = false;
    for (std::size_t index = 2; index < tokens.size(); ++index) {
        const std::string &token = tokens[index];
        if (ToLower(token) == "params:") {
            are_parameters = true;
            continue;
        }
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos && !are_parameters) {
            subcircuit.Ports.push_back(token);
            continue;
        }
        are_parameters = true;
        if (equals == std::string::npos || equals == 0) {
            subcircuit.Warnings.push_back(std::format("The parameter \"{}\" has no value and was left out", token));
            continue;
        }
        const std::string name = token.substr(0, equals);
        const std::string value = token.substr(equals + 1);
        if (const std::optional<double> number = ParseSpiceNumber(value);
            number && SubcircuitInstance::IsValidIdentifier(name)) {
            subcircuit.Parameters.push_back({name, *number});
        } else {
            // An expression stays a parameter of the text, which the instances cannot set
            expressions += std::format(".param {}={}\n", name, value);
        }
    }
}

// Appends the definitions of the file that the text uses, and those they use in turn, in file order
std::string AddDependencies(const std::string &text, const std::string &own_name,
                            const std::map<std::string, Definition> &definitions) {
    std::set<std::string> used;
    std::vector<std::string> pending = {text};
    while (!pending.empty()) {
        const std::string current = std::move(pending.back());
        pending.pop_back();
        for (const std::string &name : CollectNames(current)) {
            const auto definition = definitions.find(name);
            if (definition == definitions.end() || name == own_name || !used.insert(name).second) {
                continue;
            }
            pending.push_back(definition->second.Text);
        }
    }
    std::vector<const Definition *> ordered;
    std::set<std::string> written;
    for (const std::string &name : used) {
        const Definition &definition = definitions.at(name);
        // One .param line can define several names; it is written once
        if (written.insert(definition.Text).second) {
            ordered.push_back(&definition);
        }
    }
    std::ranges::sort(ordered, {}, &Definition::Order);
    std::string dependencies;
    for (const Definition *definition : ordered) {
        dependencies += definition->Text;
    }
    return dependencies;
}

bool IsEncrypted(const std::string_view text) {
    const std::string lower = ToLower(text);
    return std::ranges::any_of(ENCRYPTION_MARKERS,
                               [&lower](const std::string_view marker) { return lower.contains(marker); });
}

// The comment lines right above a line, without their "*", which vendors use to say what each port of a .subckt is
std::string ReadDescription(const std::vector<std::string> &lines, const std::size_t line) {
    std::size_t first = line;
    while (first > 0 && Trim(lines[first - 1]).starts_with('*')) {
        --first;
    }
    std::string description;
    for (std::size_t index = first; index < line; ++index) {
        std::string_view comment = Trim(lines[index]);
        comment.remove_prefix(1);
        description += comment;
        description += '\n';
    }
    return description;
}

bool IsInclusion(const std::string_view directive) {
    return directive == ".include" || directive == ".inc" || directive == ".lib";
}

// What the first pass over a library finds: where each subcircuit outside any other starts and ends, as indices of its
// statements, and the definitions outside the subcircuits that one may use
struct LibraryScan {
    std::vector<std::string> Lines;
    std::vector<Statement> Statements;
    std::vector<std::pair<std::size_t, std::size_t>> Found;
    std::map<std::string, Definition> Definitions;
    std::size_t Order = 0;
};

// A .model, .param or inclusion outside the subcircuits
void ReadTopLevelStatement(LibraryScan &scan, const Statement &statement, const std::vector<std::string> &tokens,
                           SpiceLibrary &library) {
    const std::string directive = ToLower(tokens.front());
    const std::string text = JoinLines(scan.Lines, statement.FirstLine, statement.LastLine);
    if (directive == ".model" && tokens.size() >= 2) {
        const bool is_new =
            scan.Definitions.try_emplace(ToLower(tokens[1]), Definition{.Order = scan.Order++, .Text = text}).second;
        // A model a part of imcsim can use is offered on its own too; the others only serve the subcircuits
        if (std::optional<LibraryModel> model = ReadLibraryModel(text); model && is_new) {
            library.Models.push_back(std::move(*model));
        }
    } else if (directive == ".param") {
        const std::size_t order = scan.Order++;
        for (std::size_t token = 1; token < tokens.size(); ++token) {
            if (const std::size_t equals = tokens[token].find('='); equals != std::string::npos && equals > 0) {
                scan.Definitions.try_emplace(ToLower(tokens[token].substr(0, equals)),
                                             Definition{.Order = order, .Text = text});
            }
        }
    } else if (IsInclusion(directive)) {
        library.Warnings.push_back(std::format("Line {}: {} is not followed; import the file it names as well",
                                               statement.FirstLine + 1, tokens.front()));
    }
}

// Records a subcircuit that ends, which another one may use by its name
void AddFoundSubcircuit(LibraryScan &scan, const std::size_t header, const std::size_t end) {
    scan.Found.emplace_back(header, end);
    if (const std::vector<std::string> tokens = Tokenize(scan.Statements[header].Logical); tokens.size() >= 2) {
        scan.Definitions.try_emplace(ToLower(tokens[1]),
                                     Definition{.Order = scan.Order++,
                                                .Text = JoinLines(scan.Lines, scan.Statements[header].FirstLine,
                                                                  scan.Statements[end].LastLine)});
    }
}

// The first pass finds the subcircuits and the definitions outside them
LibraryScan ScanLibrary(const std::string_view text, SpiceLibrary &library) {
    LibraryScan scan;
    scan.Lines = SplitLines(text);
    scan.Statements = ReadStatements(scan.Lines);
    int depth = 0;
    std::size_t header = 0;
    for (std::size_t index = 0; index < scan.Statements.size(); ++index) {
        const Statement &statement = scan.Statements[index];
        const std::vector<std::string> tokens = Tokenize(statement.Logical);
        if (tokens.empty()) {
            continue;
        }
        const std::string directive = ToLower(tokens.front());
        if (directive == ".subckt") {
            header = depth++ == 0 ? index : header;
        } else if (directive == ".ends" && depth == 0) {
            library.Warnings.push_back(
                std::format("Line {} closes a subcircuit that was never opened", statement.FirstLine + 1));
        } else if (directive == ".ends") {
            if (--depth == 0) {
                AddFoundSubcircuit(scan, header, index);
            }
        } else if (depth == 0) {
            ReadTopLevelStatement(scan, statement, tokens, library);
        }
    }
    if (depth > 0) {
        library.Warnings.emplace_back("The file ends inside a subcircuit, which was left out");
    }
    return scan;
}

// An .include or .lib inside a subcircuit brings in what the file does not have
void WarnInclusions(const LibraryScan &scan, const std::size_t header, const std::size_t end,
                    LibrarySubcircuit &subcircuit) {
    for (std::size_t index = header + 1; index < end; ++index) {
        const std::vector<std::string> inner = Tokenize(scan.Statements[index].Logical);
        if (!inner.empty() && IsInclusion(ToLower(inner.front()))) {
            subcircuit.Warnings.push_back(std::format("Line {}: {} is not followed, so what it brings in is missing",
                                                      scan.Statements[index].FirstLine + 1, inner.front()));
        }
    }
}

// The second pass turns a subcircuit into a text that needs nothing else from the file; no value for one without a
// name, defined twice or encrypted, with the warning that says so
std::optional<LibrarySubcircuit> BuildSubcircuit(const LibraryScan &scan, const std::size_t header,
                                                 const std::size_t end, std::set<std::string> &names,
                                                 std::vector<std::string> &warnings) {
    const Statement &first = scan.Statements[header];
    const Statement &last = scan.Statements[end];
    const std::vector<std::string> tokens = Tokenize(first.Logical);
    if (tokens.size() < 2) {
        warnings.push_back(std::format("The subcircuit on line {} has no name and was left out", first.FirstLine + 1));
        return std::nullopt;
    }
    LibrarySubcircuit subcircuit;
    subcircuit.Name = tokens[1];
    subcircuit.Description = ReadDescription(scan.Lines, first.FirstLine);
    if (!names.insert(ToLower(subcircuit.Name)).second) {
        warnings.push_back(std::format("{} is defined twice; only the first one was read", subcircuit.Name));
        return std::nullopt;
    }
    std::string expressions;
    ReadHeader(tokens, subcircuit, expressions);
    const std::string body = last.FirstLine > first.LastLine + 1
                                 ? JoinLines(scan.Lines, first.LastLine + 1, last.FirstLine - 1)
                                 : std::string();
    if (IsEncrypted(body)) {
        warnings.push_back(std::format("{} is encrypted, so only its vendor's simulator can read it; it was left out",
                                       subcircuit.Name));
        return std::nullopt;
    }
    WarnInclusions(scan, header, end, subcircuit);
    // What it uses from the file comes first, since a .param must be set before an expression reads it
    const std::string own = expressions + body;
    subcircuit.Text = AddDependencies(own, ToLower(subcircuit.Name), scan.Definitions) + own;
    return subcircuit;
}

} // namespace

/**
 * @brief   Reads the subcircuits of a SPICE library file.
 * @param[in] text  Contents of a .lib, .mod, .sub or .cir file.
 * @return  Every .subckt of the file outside any other, each with the .model, .subckt and .param definitions of the
 *          file that it uses, so it simulates on its own; every diode and transistor .model outside a subcircuit, which
 *          the parts can use by name; and the warnings that concern the whole file, such as an
 *          .include it does not follow or a subcircuit left unclosed. An encrypted subcircuit is left out with a
 *          warning, since only its vendor's simulator can read it.
 */
SpiceLibrary ReadSpiceLibrary(const std::string_view text) {
    SpiceLibrary library;
    const LibraryScan scan = ScanLibrary(text, library);
    std::set<std::string> names;
    for (const auto &[header, end] : scan.Found) {
        if (std::optional<LibrarySubcircuit> subcircuit = BuildSubcircuit(scan, header, end, names, library.Warnings)) {
            library.Subcircuits.push_back(std::move(*subcircuit));
        }
    }
    if (library.Subcircuits.empty() && library.Models.empty() && library.Warnings.empty()) {
        library.Warnings.emplace_back("The file has no .subckt, nor a diode or transistor .model, to import");
    }
    return library;
}

/**
 * @brief   Reads a number as SPICE writes it, with a scale suffix and maybe a unit.
 * @param[in] text  Text such as "4.7k", "10Meg", "100nF" or "1e-3".
 * @return  The value; no value for an expression, such as "{2*R}", or text that does not start with a number.
 * @note    Case is ignored, as SPICE does: "M" and "m" are milli, and mega is "meg". Letters after the suffix are a
 * unit and are ignored.
 */
std::optional<double> ParseSpiceNumber(const std::string_view text) {
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || !std::isfinite(value)) {
        return std::nullopt;
    }
    const std::string rest = ToLower(text.substr(static_cast<std::size_t>(end - text.data())));
    if (!std::ranges::all_of(rest, [](const unsigned char character) { return std::isalpha(character) != 0; })) {
        return std::nullopt;
    }
    for (const Scale &scale : SCALES) {
        if (rest.starts_with(scale.Suffix)) {
            return value * scale.Factor;
        }
    }
    return value;
}

} // namespace Core
