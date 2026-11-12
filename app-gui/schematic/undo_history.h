/**
 * @file    undo_history.h
 * @brief   The undo and redo steps of a document, kept as whole snapshots saved to text.
 */

#ifndef IMCSIM_UNDO_HISTORY_H
#define IMCSIM_UNDO_HISTORY_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   UndoHistory
 * @brief   Remembers the states a document went through, so they can be gone back to and forth.
 * @details Each state is a whole snapshot, which is simple and always consistent, at the cost of some memory. The
 *          history never looks at a snapshot: the owner takes one when a change is pending and hands it over. Changes
 *          pile up as one pending change until Commit(), so a whole drag or a typed value is undone in one step. The
 *          snapshot at the last save is kept too, to tell whether the document is back at what the file holds.
 */
class UndoHistory {
public:
    /// Older steps are dropped beyond this, so a long session does not keep growing.
    static constexpr std::size_t MAX_UNDO_STEPS = 100;

    // Starting over
    void Reset(std::string snapshot);

    // Recording
    void NoteChange();
    bool HasPendingChange() const;
    void Commit(std::string snapshot);
    void MarkSaved();

    // Going back and forth
    bool CanUndo() const;
    bool CanRedo() const;
    std::optional<std::string> Undo();
    std::optional<std::string> Redo();
    bool IsAtSavedState() const;

private:
    std::string m_CurrentSnapshot;
    std::string m_SavedSnapshot;
    std::vector<std::string> m_UndoSnapshots;
    std::vector<std::string> m_RedoSnapshots;
    /// Set by every change and cleared by Commit(), so unchanged frames skip taking a snapshot.
    bool m_HasPendingChange = false;
};

} // namespace GUI

#endif // IMCSIM_UNDO_HISTORY_H
