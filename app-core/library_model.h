/**
 * @file    library_model.h
 * @brief   A .model line from a vendor's library, kept as text, which diodes and transistors can use by name.
 */

#ifndef IMCSIM_LIBRARY_MODEL_H
#define IMCSIM_LIBRARY_MODEL_H

#include "components/component.h"
#include <optional>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  LibraryModel
 * @brief   A device model as a library writes it, such as ".model 1N4148 D(IS=2.52n RS=0.568 ...)".
 * @details Kind is the family of parts it fits: ComponentType::DIODE for every diode, LED and Zener, or the type of
 *          transistor (NPN, PNP, NMOS, PMOS). Text is the whole statement, continuation lines included, as it came; the
 *          netlist writes it under GetLibraryModelSpiceName(), so it cannot clash with a ready model.
 */
struct LibraryModel {
    std::string Name;
    ComponentType Kind = ComponentType::DIODE;
    std::string Text;

    bool operator==(const LibraryModel &) const = default;
};

// Reading
std::optional<LibraryModel> ReadLibraryModel(std::string_view text);

// Parts
bool CanUseLibraryModel(ComponentType part, ComponentType kind);
const char *GetModelKindName(ComponentType kind);

// Netlist
std::string GetLibraryModelSpiceName(std::string_view name);
std::string FormatLibraryModel(const LibraryModel &model);
std::string RenameLibraryModel(const LibraryModel &model, std::string_view name);

} // namespace Core

#endif // IMCSIM_LIBRARY_MODEL_H
