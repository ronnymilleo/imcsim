/**
 * @file    undo_history.cpp
 * @brief   The undo and redo steps of a document, kept as whole snapshots saved to text.
 */

#include "schematic/undo_history.h"

#include <utility>

namespace GUI {

/**
 * @brief   Makes a snapshot the only state, and also the saved one.
 * @param[in] snapshot  The document as it is now.
 */
void UndoHistory::Reset(std::string snapshot) {
    m_CurrentSnapshot = std::move(snapshot);
    m_SavedSnapshot = m_CurrentSnapshot;
    m_UndoSnapshots.clear();
    m_RedoSnapshots.clear();
    m_HasPendingChange = false;
}

/**
 * @brief   Records that the document changed since the last commit.
 */
void UndoHistory::NoteChange() {
    m_HasPendingChange = true;
}

/**
 * @brief   Tells whether a change waits for Commit().
 * @return  True after NoteChange() until the next Commit(), Undo() or Redo().
 */
bool UndoHistory::HasPendingChange() const {
    return m_HasPendingChange;
}

/**
 * @brief   Turns the changes made since the last commit into one undo step.
 * @param[in] snapshot  The document as it is now.
 * @note    A snapshot equal to the current one adds no step. A new step clears what could be redone.
 */
void UndoHistory::Commit(std::string snapshot) {
    m_HasPendingChange = false;
    if (snapshot == m_CurrentSnapshot) {
        return;
    }
    m_UndoSnapshots.push_back(std::move(m_CurrentSnapshot));
    if (m_UndoSnapshots.size() > MAX_UNDO_STEPS) {
        m_UndoSnapshots.erase(m_UndoSnapshots.begin());
    }
    m_CurrentSnapshot = std::move(snapshot);
    m_RedoSnapshots.clear();
}

/**
 * @brief   Makes the current state the saved one.
 * @note    Undoing or redoing back to it makes IsAtSavedState() true again.
 */
void UndoHistory::MarkSaved() {
    m_SavedSnapshot = m_CurrentSnapshot;
}

/**
 * @brief   Tells whether there is a step to undo.
 * @return  True when there is a committed step or a pending change.
 */
bool UndoHistory::CanUndo() const {
    return !m_UndoSnapshots.empty() || m_HasPendingChange;
}

/**
 * @brief   Tells whether there is an undone step to redo.
 * @return  True when Redo() would change something.
 */
bool UndoHistory::CanRedo() const {
    return !m_RedoSnapshots.empty();
}

/**
 * @brief   Goes back to the state before the last step.
 * @return  The snapshot to restore, or nothing when there is no committed step.
 * @note    Commit a pending change first, so it is what gets undone.
 */
std::optional<std::string> UndoHistory::Undo() {
    if (m_UndoSnapshots.empty()) {
        return std::nullopt;
    }
    m_RedoSnapshots.push_back(std::move(m_CurrentSnapshot));
    m_CurrentSnapshot = std::move(m_UndoSnapshots.back());
    m_UndoSnapshots.pop_back();
    m_HasPendingChange = false;
    return m_CurrentSnapshot;
}

/**
 * @brief   Applies again the last step that was undone.
 * @return  The snapshot to restore, or nothing when there is no step to redo.
 * @note    A pending change is dropped, as the restored state replaces it.
 */
std::optional<std::string> UndoHistory::Redo() {
    if (m_RedoSnapshots.empty()) {
        return std::nullopt;
    }
    m_UndoSnapshots.push_back(std::move(m_CurrentSnapshot));
    m_CurrentSnapshot = std::move(m_RedoSnapshots.back());
    m_RedoSnapshots.pop_back();
    m_HasPendingChange = false;
    return m_CurrentSnapshot;
}

/**
 * @brief   Tells whether the current state is the one at the last save.
 * @return  True when they hold the same snapshot.
 */
bool UndoHistory::IsAtSavedState() const {
    return m_CurrentSnapshot == m_SavedSnapshot;
}

} // namespace GUI
