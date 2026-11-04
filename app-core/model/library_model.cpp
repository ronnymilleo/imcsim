/**
 * @file    library_model.cpp
 * @brief   A .model line from a vendor's library, kept as text, which diodes and transistors can use by name.
 */

#include "model/library_model.h"

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <utility>

namespace Core {

namespace {

struct KindName {
    std::string_view SpiceType;
    ComponentType Kind;
};

/// The SPICE model types a part of imcsim can use; the others, such as VDMOS or switches, have no part to go on.
constexpr auto KINDS = std::to_array<KindName>({{"d", ComponentType::DIODE},
                                                {"npn", ComponentType::NPN},
                                                {"pnp", ComponentType::PNP},
                                                {"nmos", ComponentType::NMOS},
                                                {"pmos", ComponentType::PMOS}});

/// Prefix of the netlist name of a library model, which no ready model starts with.
constexpr std::string_view SPICE_NAME_PREFIX = "LIB_";

bool IsSpace(const char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

std::string ToLower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

/**
 * @brief   Where the name of a .model statement starts and how long it is, or no value when the text is not one.
 */
std::optional<std::pair<std::size_t, std::size_t>> FindName(const std::string_view text) {
    std::size_t position = 0;
    while (position < text.size() && IsSpace(text[position])) {
        ++position;
    }
    const std::string_view keyword = ".model";
    if (ToLower(text.substr(position, keyword.size())) != keyword) {
        return std::nullopt;
    }
    position += keyword.size();
    if (position >= text.size() || !IsSpace(text[position])) {
        return std::nullopt;
    }
    while (position < text.size() && IsSpace(text[position])) {
        ++position;
    }
    const std::size_t start = position;
    while (position < text.size() && !IsSpace(text[position]) && text[position] != '(') {
        ++position;
    }
    if (position == start) {
        return std::nullopt;
    }
    return std::pair{start, position - start};
}

} // namespace

/**
 * @brief   Reads a .model statement that a diode or a transistor can use.
 * @param[in] text  The statement, continuation lines included, such as ".model 2N3904 NPN(IS=6.734f BF=416.4 ...)".
 * @return  The model with its name, the family of parts it fits and the text as given; no value for any other text,
 *          and for a model type no part of imcsim takes, such as VDMOS.
 */
std::optional<LibraryModel> ReadLibraryModel(const std::string_view text) {
    const auto name = FindName(text);
    if (!name) {
        return std::nullopt;
    }
    std::size_t position = name->first + name->second;
    while (position < text.size() && (IsSpace(text[position]) || text[position] == '+')) {
        ++position;
    }
    const std::size_t type_start = position;
    while (position < text.size() && std::isalpha(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
    const std::string type = ToLower(text.substr(type_start, position - type_start));
    const auto kind = std::ranges::find(KINDS, std::string_view(type), &KindName::SpiceType);
    if (kind == KINDS.end()) {
        return std::nullopt;
    }
    std::string model_text(text);
    if (!model_text.ends_with('\n')) {
        model_text += '\n';
    }
    return LibraryModel{
        .Name = std::string(text.substr(name->first, name->second)), .Kind = kind->Kind, .Text = std::move(model_text)};
}

/**
 * @brief   Tells whether a part can use a model of a family.
 * @param[in] part  Type of the part.
 * @param[in] kind  Family of the model, its LibraryModel::Kind.
 * @return  True for a diode model on any diode, LED or Zener, and for a transistor model on a transistor of its type.
 */
bool CanUseLibraryModel(const ComponentType part, const ComponentType kind) {
    if (kind == ComponentType::DIODE) {
        return IsDiode(part);
    }
    return (IsBJT(part) || IsMOSFET(part)) && part == kind;
}

/**
 * @brief   Returns a readable name for the family of a model.
 * @param[in] kind  Family of the model, its LibraryModel::Kind.
 * @return  "diode", "NPN", "PNP", "NMOS" or "PMOS".
 */
const char *GetModelKindName(const ComponentType kind) {
    switch (kind) {
    case ComponentType::NPN:
        return "NPN";
    case ComponentType::PNP:
        return "PNP";
    case ComponentType::NMOS:
        return "NMOS";
    case ComponentType::PMOS:
        return "PMOS";
    case ComponentType::DIODE:
    default:
        return "diode";
    }
}

/**
 * @brief   Returns the name a library model has in the netlist.
 * @param[in] name  Name of the model, as its library writes it.
 * @return  The name after a prefix that no ready model uses, such as "LIB_1N4148".
 */
std::string GetLibraryModelSpiceName(const std::string_view name) {
    return std::format("{}{}", SPICE_NAME_PREFIX, name);
}

/**
 * @brief   Writes a library model for the netlist.
 * @param[in] model  Model to write.
 * @return  Its text with the name it has in the netlist, GetLibraryModelSpiceName(), ending in a new line.
 */
std::string FormatLibraryModel(const LibraryModel &model) {
    return RenameLibraryModel(model, GetLibraryModelSpiceName(model.Name));
}

/**
 * @brief   Returns the text of a model under another name, as when a second model of the same name joins a schematic.
 * @param[in] model  Model to rename.
 * @param[in] name   New name.
 * @return  Its text with the name of its .model statement replaced; the text as it was when it has no name to replace.
 */
std::string RenameLibraryModel(const LibraryModel &model, const std::string_view name) {
    std::string text = model.Text;
    if (const auto found = FindName(text)) {
        text.replace(found->first, found->second, name);
    }
    return text;
}

} // namespace Core
