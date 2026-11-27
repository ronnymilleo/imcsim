/**
 * @file    turn_keys.h
 * @brief   The keys that turn what the editor holds: R a quarter turn, M a mirror left to right, Shift+M top to bottom.
 */

#ifndef IMCSIM_TURN_KEYS_H
#define IMCSIM_TURN_KEYS_H

#include "schematic/wire_editing.h"
#include <optional>

namespace GUI {

/**
 * @struct  TurnKeys
 * @brief   The state of the keys that turn, in one frame.
 */
struct TurnKeys {
    bool IsRotatePressed = false; ///< Whether R was pressed this frame.
    bool IsMirrorPressed = false; ///< Whether M was pressed this frame.
    bool IsShiftHeld = false;
};

// Turning keys
[[nodiscard]] std::optional<PointTransform::Kind> GetTurnKind(TurnKeys keys);
[[nodiscard]] std::optional<PointTransform::Kind> ReadTurnKey();

} // namespace GUI

#endif // IMCSIM_TURN_KEYS_H
