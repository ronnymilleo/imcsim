/**
 * @file    transient_window.h
 * @brief   Which window of points of the transient the plot and the schematic show, and whether it follows a run.
 */

#ifndef IMCSIM_TRANSIENT_WINDOW_H
#define IMCSIM_TRANSIENT_WINDOW_H

#include "point_window.h"
#include "simulation_results.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   TransientWindow
 * @brief   The window of points that the Output window plots and the schematic plays, which the user moves and sizes.
 * @details The Application updates it every frame, so it follows a run in progress whichever window is shown; the plot
 *          and the schematic read the window the last update chose. The window holds a number of points the user
 *          picks, or, with a span set, the points of that stretch of time, as a live run is seen. It is view state: it
 * is not saved with the schematic.
 */
class TransientWindow {
public:
    // Every frame
    PointWindow Update(const Core::Transient *transient, std::size_t result_version, bool is_running);
    PointWindow GetWindow() const;
    std::size_t GetTotalPoints() const;
    bool IsLive() const;

    // The user's choices
    int GetPoints() const;
    void SetPoints(int points);
    void SetFirst(std::size_t first);
    bool DoesFollow() const;
    void SetFollow(bool follows);

    // A stretch of time in place of a number of points
    std::optional<double> GetSpan() const;
    void SetSpan(std::optional<double> span);

private:
    // What the user asked for, which the next update applies
    std::size_t m_First = 0;
    int m_Points = DEFAULT_WINDOW_POINTS;
    bool m_Follows = true;
    std::optional<double> m_Span;

    // What the last update chose, and what it saw
    PointWindow m_Shown;
    std::size_t m_TotalPoints = 0;
    bool m_IsLive = false;
    std::size_t m_ResultVersion = 0;
    bool m_WasIncomplete = false;
};

} // namespace GUI

#endif // IMCSIM_TRANSIENT_WINDOW_H
