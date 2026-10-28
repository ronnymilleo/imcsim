/**
 * @file    auto_rerun.cpp
 * @brief   Decides when to run the selected analysis again by itself, after an edit of the circuit.
 */

#include "auto_rerun.h"

namespace GUI {

/**
 * @brief   Takes in one frame and tells whether to run the selected analysis now.
 * @param[in] frame  State of the program this frame; call it once per frame.
 * @return  True once per settled edit, when the run should start; false otherwise.
 * @note    An edit is seen through the circuit version, so the frame that drops the result is the one that counts, and
 *          what was followed is what the frame before it had.
 */
bool AutoRerun::Update(const AutoRerunFrame &frame) {
    if (m_SeenDocumentVersion != frame.DocumentVersion) {
        // Another schematic, or the first frame: the edits of the old one are not followed
        m_SeenDocumentVersion = frame.DocumentVersion;
        m_SeenCircuitVersion = frame.CircuitVersion;
        m_IsFollowing = frame.HasRunToFollow;
        m_DueSince.reset();
        return false;
    }
    if (m_SeenCircuitVersion != frame.CircuitVersion) {
        m_SeenCircuitVersion = frame.CircuitVersion;
        if (m_IsFollowing && frame.IsEnabled) {
            m_DueSince = frame.Time;
        }
    }
    m_IsFollowing = frame.HasRunToFollow || m_DueSince.has_value();
    if (!frame.IsEnabled) {
        m_DueSince.reset();
        return false;
    }
    if (!m_DueSince || frame.IsEditing || frame.IsRunBlocked || frame.Time - *m_DueSince < SETTLE_SECONDS) {
        return false;
    }
    m_DueSince.reset();
    return true;
}

/**
 * @brief   Tells whether an edit is waiting to be followed by a run.
 * @return  True from an edit of a followed analysis until Update() returns true, the preference is turned off or
 *          another schematic is opened.
 */
bool AutoRerun::IsDue() const {
    return m_DueSince.has_value();
}

} // namespace GUI
