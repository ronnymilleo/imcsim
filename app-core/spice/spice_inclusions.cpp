/**
 * @file    spice_inclusions.cpp
 * @brief   Puts the files a SPICE library includes in place of its .include and .lib lines, as a simulator reads them.
 */

#include "spice/spice_inclusions.h"

#include "spice/spice_text.h"
#include <algorithm>
#include <format>
#include <optional>

namespace Core {

namespace {

/// How deep inclusions may nest, so a loop the paths do not reveal, such as through a link, still ends.
constexpr std::size_t MAX_INCLUSION_DEPTH = 16;

/**
 * @struct  Expansion
 * @brief   The lines put together so far, and the files and sections being read, to catch one that includes itself.
 */
struct Expansion {
    const LibraryFileReader &ReadFile;
    LibraryLines Result;
    std::vector<std::string> Open;
};

/**
 * @struct  Source
 * @brief   A file being read and how its lines are named in the warnings.
 */
struct Source {
    std::filesystem::path Path;
    /// Whether it is the file the user picked, whose lines need no file name.
    bool IsMain = false;
};

std::string DescribeLine(const Source &source, const std::size_t line) {
    return source.IsMain ? std::format("Line {}", line + 1)
                         : std::format("Line {} of {}", line + 1, source.Path.filename().string());
}

/**
 * @brief   A file name as a line writes it, without the quotes some vendors put around it.
 */
std::string Unquote(const std::string_view name) {
    if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
        return std::string(name.substr(1, name.size() - 2));
    }
    return std::string(name);
}

/**
 * @brief   The words of a line before any comment, which ngspice starts with ";", or "$" after a space.
 */
std::vector<std::string> ReadWords(const std::string_view line) {
    std::vector<std::string> words = SplitWords(line);
    const auto comment = std::ranges::find_if(
        words, [](const std::string &word) { return word.starts_with(';') || word.starts_with('$'); });
    words.erase(comment, words.end());
    return words;
}

/**
 * @brief   Whether the one name after a .lib is a file, as LTspice writes it to include a whole file, rather than the
 *          start of a section of this one.
 */
bool IsFileName(const std::string_view name) {
    return name.contains('.') || name.contains('/') || name.contains('\\');
}

/**
 * @brief   Where the lines of a section are: after its ".lib name" line and up to its .endl.
 * @return  The first line and the one past the last; none when the file has no such section.
 */
std::optional<std::pair<std::size_t, std::size_t>> FindSection(const std::vector<std::string> &lines,
                                                               const std::string &section) {
    for (std::size_t start = 0; start < lines.size(); ++start) {
        const std::vector<std::string> words = ReadWords(lines[start]);
        if (words.size() != 2 || ToLower(words[0]) != ".lib" || ToLower(words[1]) != ToLower(section)) {
            continue;
        }
        for (std::size_t end = start + 1; end < lines.size(); ++end) {
            const std::vector<std::string> inner = ReadWords(lines[end]);
            if (!inner.empty() && ToLower(inner[0]) == ".endl") {
                return std::pair{start + 1, end};
            }
        }
        return std::pair{start + 1, lines.size()};
    }
    return std::nullopt;
}

void ExpandLines(Expansion &expansion, const std::vector<std::string> &lines, std::size_t first, std::size_t last,
                 const Source &source);

/**
 * @brief   Reads the file a line names, or one section of it, in place of that line.
 * @param[in,out] expansion  Where its lines go, and a warning when it cannot be read.
 * @param[in]     from       The file whose line names it, which a relative path starts from.
 * @param[in]     origin     Where that line is, for the warnings.
 * @param[in]     name       The file as the line writes it.
 * @param[in]     section    The section to read, or empty for the whole file.
 */
void Include(Expansion &expansion, const Source &from, const std::string &origin, const std::string_view name,
             const std::string &section) {
    const std::filesystem::path written = Unquote(name);
    const Source source = {.Path =
                               (written.is_absolute() ? written : from.Path.parent_path() / written).lexically_normal(),
                           .IsMain = false};
    const std::string key = std::format("{}|{}", source.Path.generic_string(), ToLower(section));
    if (expansion.Open.size() >= MAX_INCLUSION_DEPTH || std::ranges::contains(expansion.Open, key)) {
        expansion.Result.Warnings.push_back(
            std::format("{}: {} includes itself, so it is not read again", origin, written.string()));
        return;
    }
    const std::expected<std::string, std::string> text = expansion.ReadFile(source.Path);
    if (!text) {
        expansion.Result.Warnings.push_back(
            std::format("{}: could not read {}: {}", origin, written.string(), text.error()));
        return;
    }
    const std::vector<std::string> lines = SplitLines(*text);
    std::pair<std::size_t, std::size_t> range = {0, lines.size()};
    if (!section.empty()) {
        const auto found = FindSection(lines, section);
        if (!found) {
            expansion.Result.Warnings.push_back(
                std::format("{}: {} has no section {}", origin, written.string(), section));
            return;
        }
        range = *found;
    }
    expansion.Open.push_back(key);
    ExpandLines(expansion, lines, range.first, range.second, source);
    expansion.Open.pop_back();
}

/**
 * @brief   Copies some lines of a file, with the files their .include and .lib lines name in place of those lines.
 * @details A line that includes, or that starts or ends a section, leaves a blank line, so a comment above the next
 *          line still describes only that line.
 */
void ExpandLines(Expansion &expansion, const std::vector<std::string> &lines, const std::size_t first,
                 const std::size_t last, const Source &source) {
    for (std::size_t index = first; index < last; ++index) {
        const std::vector<std::string> words = ReadWords(lines[index]);
        const std::string directive = words.empty() ? std::string() : ToLower(words[0]);
        const std::string origin = DescribeLine(source, index);
        const bool is_inclusion =
            ((directive == ".include" || directive == ".inc") && words.size() >= 2) ||
            (directive == ".lib" && (words.size() >= 3 || (words.size() == 2 && IsFileName(Unquote(words[1])))));
        if (!is_inclusion && directive != ".lib" && directive != ".endl") {
            expansion.Result.Lines.push_back(lines[index]);
            expansion.Result.Origins.push_back(origin);
            continue;
        }
        expansion.Result.Lines.emplace_back();
        expansion.Result.Origins.push_back(origin);
        if (is_inclusion) {
            Include(expansion, source, origin, words[1], directive == ".lib" && words.size() >= 3 ? words[2] : "");
        }
    }
}

} // namespace

/**
 * @brief   Lists the lines of a library as they are, for a text with no file to include others from.
 * @param[in] text  Contents of the library.
 * @return  Its lines, each with its number; no warnings.
 */
LibraryLines ListLibraryLines(const std::string_view text) {
    LibraryLines library;
    library.Lines = SplitLines(text);
    for (std::size_t index = 0; index < library.Lines.size(); ++index) {
        library.Origins.push_back(std::format("Line {}", index + 1));
    }
    return library;
}

/**
 * @brief   Reads a library file with what it includes put in place, as ngspice would read it.
 * @param[in] path       The library file.
 * @param[in] read_file  Reads it and the files it names.
 * @return  Its lines, in which each .include and each ".lib file section" gives way to the lines of that file or
 *          section, read the same way, and each section marker or .endl to a blank line. A path is taken from the
 *          folder of the file that writes it. A file that cannot be read, that includes itself or that lacks the
 *          section asked for leaves a warning instead.
 * @note    A ".lib name" with no section is a whole file when the name has a dot or a slash, as LTspice writes it, and
 *          the start of a section of this file otherwise. Every section of the file picked is read, the first
 *          definition of a name winning, as it says nothing of which one to use.
 */
LibraryLines ExpandInclusions(const std::filesystem::path &path, const LibraryFileReader &read_file) {
    Expansion expansion{.ReadFile = read_file, .Result = {}, .Open = {}};
    const std::expected<std::string, std::string> text = read_file(path);
    if (!text) {
        expansion.Result.Warnings.push_back(
            std::format("Could not read {}: {}", path.filename().string(), text.error()));
        return std::move(expansion.Result);
    }
    const std::vector<std::string> lines = SplitLines(*text);
    expansion.Open.push_back(std::format("{}|", path.lexically_normal().generic_string()));
    ExpandLines(expansion, lines, 0, lines.size(), {.Path = path, .IsMain = true});
    return std::move(expansion.Result);
}

} // namespace Core
