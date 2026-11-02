/**
 * @file    transient_window.cpp
 * @brief   Which window of points of the transient the plot and the schematic show, and whether it follows a run.
 */

#include "transient_window.h"

#include <algorithm>
#include <vector>

namespace GUI {

namespace {

// How many points from first fit in a span of time, at least one and at most the largest window
std::size_t CountSpanPoints(const std::vector<double> &times, const std::size_t first, const double span) {
    const auto end =
        std::upper_bound(times.begin() + static_cast<std::ptrdiff_t>(first), times.end(), times[first] + span);
    const auto count = static_cast<std::size_t>(end - times.begin()) - first;
    return std::clamp<std::size_t>(count, 1, MAX_WINDOW_POINTS);
}

// The first of the points that end a run within a span of time, keeping at most the largest window
std::size_t FindSpanStart(const std::vector<double> &times, const double span) {
    const auto start = std::lower_bound(times.begin(), times.end(), times.back() - span);
    const auto first = static_cast<std::size_t>(start - times.begin());
    return std::max(first, times.size() - std::min(times.size(), static_cast<std::size_t>(MAX_WINDOW_POINTS)));
}

} // namespace

/**
 * @brief   Chooses the window to show this frame.
 * @param[in] transient      The transient on display, or null when there is none.
 * @param[in] result_version Changes whenever a new result replaces the transient.
 * @param[in] is_running     Whether a run is in progress.
 * @return  The window of points. A new result starts at its beginning, unless it is the whole run that replaces the
 *          chunks the window was following, which stays where it was. A window that follows a run in progress is on its
 *          newest points. With a span, the window holds the points of that stretch of time, up to MAX_WINDOW_POINTS.
 */
PointWindow TransientWindow::Update(const Core::Transient *transient, const std::size_t result_version,
                                    const bool is_running) {
    if (transient == nullptr) {
        m_Shown = {};
        m_TotalPoints = 0;
        m_IsLive = false;
        return m_Shown;
    }
    m_TotalPoints = transient->Times.size();
    m_Points = std::clamp(m_Points, MIN_WINDOW_POINTS, MAX_WINDOW_POINTS);
    auto points = static_cast<std::size_t>(m_Points);
    if (result_version != m_ResultVersion) {
        m_ResultVersion = result_version;
        if (!m_WasIncomplete) {
            m_First = 0;
            m_Follows = true;
        }
    }
    m_WasIncomplete = !transient->IsComplete;
    m_IsLive = m_Follows && is_running && !transient->IsComplete;
    if (m_Span && m_TotalPoints > 0) {
        if (m_IsLive) {
            m_First = FindSpanStart(transient->Times, *m_Span);
        }
        m_First = std::min(m_First, m_TotalPoints - 1);
        points = CountSpanPoints(transient->Times, m_First, *m_Span);
    } else if (m_IsLive) {
        m_First = GetEndWindow(m_TotalPoints, points).First;
    }
    m_Shown = ClampWindow(m_TotalPoints, m_First, points);
    m_First = m_Shown.First;
    return m_Shown;
}

/**
 * @brief   Returns the window the last update chose.
 * @return  The points to plot and to play; none before the first update, or without a transient.
 */
PointWindow TransientWindow::GetWindow() const {
    return m_Shown;
}

/**
 * @brief   Returns how many points the transient has.
 * @return  The count the last update saw.
 */
std::size_t TransientWindow::GetTotalPoints() const {
    return m_TotalPoints;
}

/**
 * @brief   Tells whether the window is on the newest points of a run in progress.
 * @return  True while the window follows the run and the run goes on.
 */
bool TransientWindow::IsLive() const {
    return m_IsLive;
}

/**
 * @brief   Returns how many points the window holds at most.
 * @return  The size the user chose, from MIN_WINDOW_POINTS to MAX_WINDOW_POINTS.
 */
int TransientWindow::GetPoints() const {
    return m_Points;
}

/**
 * @brief   Sets how many points the window holds at most.
 * @param[in] points  The size; one outside the limits is brought inside them by the next update.
 */
void TransientWindow::SetPoints(const int points) {
    m_Points = points;
}

/**
 * @brief   Moves the window to start at a point, as the user does with the scroll bar.
 * @param[in] first  Index of the first point; the next update keeps the window inside the transient.
 * @note    The window stops following the run.
 */
void TransientWindow::SetFirst(const std::size_t first) {
    m_First = first;
    m_Follows = false;
}

/**
 * @brief   Tells whether the window follows the newest points of a run.
 * @return  The choice of the user, which only counts while a run goes on.
 */
bool TransientWindow::DoesFollow() const {
    return m_Follows;
}

/**
 * @brief   Chooses whether the window follows the newest points of a run.
 * @param[in] follows  True to follow.
 */
void TransientWindow::SetFollow(const bool follows) {
    m_Follows = follows;
}

/**
 * @brief   Returns the stretch of time the window holds in place of a number of points.
 * @return  The span in seconds, or no value when the window holds the number of points the user picked.
 */
std::optional<double> TransientWindow::GetSpan() const {
    return m_Span;
}

/**
 * @brief   Makes the window hold a stretch of time instead of a number of points, as a live run is seen.
 * @param[in] span  The span in seconds, or no value to go back to the number of points.
 */
void TransientWindow::SetSpan(const std::optional<double> span) {
    m_Span = span;
}

} // namespace GUI
