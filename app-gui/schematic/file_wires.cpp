/**
 * @file    file_wires.cpp
 * @brief   Writes the wires of a schematic to JSON and reads them back, skipping those that cannot be drawn.
 */

#include "schematic/file_wires.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <format>
#include <optional>

namespace GUI {

/**
 * @brief   Writes wires as a JSON list.
 * @param[in] wires  Wire segments to write.
 * @return  The list, with a start and an end point for each wire.
 */
nlohmann::json WriteWires(const std::vector<UIWire> &wires) {
    nlohmann::json list = nlohmann::json::array();
    for (const UIWire &wire : wires) {
        list.push_back({
            {"start", {wire.GetStart().X, wire.GetStart().Y}},
            {"end", {wire.GetEnd().X, wire.GetEnd().Y}},
        });
    }
    return list;
}

/**
 * @brief   Reads the "wires" list of a document or of a block definition.
 * @details Wires are horizontal or vertical; one with no integer ends, no length or a diagonal is skipped with a
 *          warning.
 * @param[in]     object   Document or definition object that holds the list.
 * @param[in]     wording  How much the warnings say.
 * @param[out]    wires    Receives the wires that were read.
 * @param[in,out] warnings Receives one message for each wire skipped.
 */
void ReadWires(const nlohmann::json &object, const WireWording wording, std::vector<UIWire> &wires,
               std::vector<std::string> &warnings) {
    const auto list = object.find("wires");
    if (list == object.end() || !list->is_array()) {
        return;
    }
    for (std::size_t index = 0; index < list->size(); ++index) {
        const nlohmann::json &wire = (*list)[index];
        const std::optional<GridPoint> start = wire.is_object() ? ReadPoint(wire, "start") : std::nullopt;
        const std::optional<GridPoint> end = wire.is_object() ? ReadPoint(wire, "end") : std::nullopt;
        if (!start || !end) {
            warnings.push_back(wording == WireWording::SPECIFIC
                                   ? std::format("Wire {} has no integer start and end and was skipped", index + 1)
                                   : std::format("Wire {} is invalid and was skipped", index + 1));
        } else if (*start == *end) {
            warnings.push_back(wording == WireWording::SPECIFIC
                                   ? std::format("Wire {} has zero length and was skipped", index + 1)
                                   : std::format("Wire {} is invalid and was skipped", index + 1));
        } else if (start->X != end->X && start->Y != end->Y) {
            warnings.push_back(wording == WireWording::SPECIFIC
                                   ? std::format("Wire {} from ({}, {}) to ({}, {}) is diagonal and was skipped; "
                                                 "redraw it",
                                                 index + 1, start->X, start->Y, end->X, end->Y)
                                   : std::format("Wire {} is invalid and was skipped", index + 1));
        } else {
            wires.emplace_back(*start, *end);
        }
    }
}

} // namespace GUI
