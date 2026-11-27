/**
 * @file    live_input_handler.h
 * @brief   What the mouse does to the parts the user sets by hand during a live run: flip, hold, turn.
 */

#ifndef IMCSIM_LIVE_INPUT_HANDLER_H
#define IMCSIM_LIVE_INPUT_HANDLER_H

#include "base/helpers.h"
#include "schematic/live_input.h"
#include "schematic/schematic.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @struct  LivePart
 * @brief   A part under the cursor that the user sets by hand during a live run, and what the mouse does to it.
 */
struct LivePart {
    std::size_t Index = 0; ///< Index of the part among the elements of the schematic on show.
    LiveAction Action = LiveAction::NONE;
};

/**
 * @class   LiveInputHandler
 * @brief   Reads the mouse over the parts of a live run and keeps the push button the mouse holds down.
 */
class LiveInputHandler {
public:
    // Picking
    std::optional<LivePart>
    FindLivePart(const Schematic &schematic, const ViewTransform &view, bool is_live_part_pickable) const;

    // Input
    bool HandleInput(Schematic &schematic, std::optional<LivePart> live_part);

    // The push button the mouse holds down
    std::optional<std::size_t> GetHeldButton() const;

private:
    std::optional<std::size_t> m_HeldButton; ///< The push button the mouse holds down.

    void ReleaseHeldButton(Schematic &schematic);
};

} // namespace GUI

#endif // IMCSIM_LIVE_INPUT_HANDLER_H
