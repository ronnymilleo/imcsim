/**
 * @file    auto_rerun.h
 * @brief   Decides when to run the selected analysis again by itself, after an edit of the circuit.
 */

#ifndef IMCSIM_AUTO_RERUN_H
#define IMCSIM_AUTO_RERUN_H

#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @struct  AutoRerunFrame
 * @brief   What the program is doing this frame, as far as running again by itself is concerned.
 */
struct AutoRerunFrame {
    // The user's preference
    bool IsEnabled = false;
    // Seconds on any clock that only goes forward
    double Time = 0.0;
    // Change with each edit that drops the results, and with each schematic opened or created
    std::size_t CircuitVersion = 0;
    std::size_t DocumentVersion = 0;
    // The selected analysis has a result or an error to follow, or is running
    bool HasRunToFollow = false;
    // A field is being typed in or something is being dragged, so the edit is not over yet
    bool IsEditing = false;
    // A run is in progress or ending, or the next run would be a live one, which follows edits by itself
    bool IsRunBlocked = false;
};

/**
 * @class   AutoRerun
 * @brief   Runs the selected analysis again once an edit of the circuit settles, so results follow edits.
 * @details Only an analysis the user already ran is followed: an edit while it had a result, an error or a run in
 *          progress makes it due. It runs once the edit is over (no field active, nothing dragged) and nothing changed
 *          for SETTLE_SECONDS, so typing or a burst of edits runs it once; a run in progress, which the edit cancels,
 *          ends first. Opening or creating a schematic forgets what was followed.
 */
class AutoRerun {
public:
    // Quiet time after the last edit before running again
    static constexpr double SETTLE_SECONDS = 0.5;

    bool Update(const AutoRerunFrame &frame);
    bool IsDue() const;

private:
    std::optional<std::size_t> m_SeenCircuitVersion;
    std::optional<std::size_t> m_SeenDocumentVersion;
    bool m_IsFollowing = false;
    // Time of the last edit while a run is due
    std::optional<double> m_DueSince;
};

} // namespace GUI

#endif // IMCSIM_AUTO_RERUN_H
