/**
 * @file    file_wires.cpp
 * @brief   Writes the wires of a schematic to JSON and reads them back, skipping those that cannot be drawn.
 */

#include "schematic/file_wires.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <format>
#include <optional>
#include <string>

namespace GUI {

namespace {

/**
 * @enum    WireProblem
 * @brief   Why a wire in a file cannot be drawn.
 */
enum class WireProblem {
    NONE,
    MISSING_ENDS,
    ZERO_LENGTH,
    DIAGONAL,
};

/**
 * @brief   Tells whether a wire with both ends can be drawn: wires are horizontal or vertical, and have a length.
 */
WireProblem ClassifyWire(const GridPoint &start, const GridPoint &end) {
    if (start == end) {
        return WireProblem::ZERO_LENGTH;
    }
    if (start.X != end.X && start.Y != end.Y) {
        return WireProblem::DIAGONAL;
    }
    return WireProblem::NONE;
}

/**
 * @brief   The warning for a wire that was skipped.
 * @param[in] problem  What is wrong with the wire; not NONE.
 * @param[in] wording  How much the warning says.
 * @param[in] number   Position of the wire in the list, counting from one.
 * @param[in] start    Start of the wire; only a diagonal wire shows it.
 * @param[in] end      End of the wire; only a diagonal wire shows it.
 */
std::string DescribeWireProblem(const WireProblem problem,
                                const WireWording wording,
                                const std::size_t number,
                                const GridPoint &start,
                                const GridPoint &end) {
    if (wording == WireWording::GENERIC) {
        return std::format("Wire {} is invalid and was skipped", number);
    }
    switch (problem) {
    case WireProblem::MISSING_ENDS:
        return std::format("Wire {} has no integer start and end and was skipped", number);
    case WireProblem::ZERO_LENGTH:
        return std::format("Wire {} has zero length and was skipped", number);
    case WireProblem::DIAGONAL:
        return std::format("Wire {} from ({}, {}) to ({}, {}) is diagonal and was skipped; redraw it",
                           number,
                           start.X,
                           start.Y,
                           end.X,
                           end.Y);
    case WireProblem::NONE:
    default:
        return std::format("Wire {} is invalid and was skipped", number);
    }
}

} // namespace

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
void ReadWires(const nlohmann::json &object,
               const WireWording wording,
               std::vector<UIWire> &wires,
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
            warnings.push_back(DescribeWireProblem(WireProblem::MISSING_ENDS, wording, index + 1, {}, {}));
            continue;
        }
        const WireProblem problem = ClassifyWire(*start, *end);
        if (problem == WireProblem::NONE) {
            wires.emplace_back(*start, *end);
        } else {
            warnings.push_back(DescribeWireProblem(problem, wording, index + 1, *start, *end));
        }
    }
}

} // namespace GUI
