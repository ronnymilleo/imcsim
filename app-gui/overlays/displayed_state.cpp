/**
 * @file    displayed_state.cpp
 * @brief   Resolves what the schematic shows each frame: the last operating point or the transient at the instant of
 * the playback, the heat scale its colors use and what the fuses and logic displays read.
 */

#include "overlays/displayed_state.h"

#include "imgui.h"
#include "results/logic_outputs.h"

namespace GUI {

namespace {

/// How often the heat scale of a transient that is still being made is worked out again, in seconds.
constexpr double SCALE_REFRESH_SECONDS = 0.5;

} // namespace

/**
 * @brief   Which result the schematic shows: the last operating point, unless a transient was run after it.
 * @param[in] has_operating_point   Whether there is an operating point.
 * @param[in] has_transient_points  Whether there is a transient with at least one point.
 * @param[in] last_run              The analysis run last, or none before any run.
 * @return  The transient when it has points and no operating point was run after it, otherwise the operating point
 *          when there is one, otherwise none.
 */
ShownResult ChooseShownResult(const bool has_operating_point,
                              const bool has_transient_points,
                              const std::optional<Analysis> last_run) {
    if (has_transient_points && (!has_operating_point || last_run != Analysis::OPERATING_POINT)) {
        return ShownResult::TRANSIENT;
    }
    return has_operating_point ? ShownResult::OPERATING_POINT : ShownResult::NONE;
}

/**
 * @brief   Whether the heat scale of a transient is to be worked out again this frame.
 * @param[in] cache        The scale kept from before.
 * @param[in] wanted       The data and the window the scale is wanted for.
 * @param[in] is_complete  Whether the run of the transient is over.
 * @param[in] now          The screen clock, in seconds.
 * @return  True when the scale kept is for other data or another window, and either the run is over, there is no scale
 *          yet or SCALE_REFRESH_SECONDS passed since the last one; while chunks keep arriving it is refreshed a few
 *          times a second only.
 */
bool IsScaleDue(const TransientScaleCache &cache, const ScaleKey &wanted, const bool is_complete, const double now) {
    const bool is_stale = cache.Key != wanted;
    const bool may_refresh = is_complete || !cache.Key.has_value() || now - cache.LastTime >= SCALE_REFRESH_SECONDS;
    return is_stale && may_refresh;
}

/**
 * @brief   Creates a resolver that shows nothing until the first Resolve().
 * @param[in] playback  Instant of the transient to show, and the window of points that plays; it must outlive the
 *                      resolver.
 */
DisplayedStateResolver::DisplayedStateResolver(const TransientPlayback &playback) : m_Playback(playback) {
}

/**
 * @brief   Resolves what the schematic shows this frame, and what each fuse shows.
 * @param[in] schematic  The schematic being edited, whose parts and nodes the results are shown on.
 * @param[in] results    The results to show.
 * @param[in] last_run   The analysis run last, which tells whether a transient came after the operating point.
 * @note    Call it once per frame, before drawing.
 */
void DisplayedStateResolver::Resolve(Schematic &schematic,
                                     const ResultStore &results,
                                     const std::optional<Analysis> last_run) {
    const auto &operating_point = results.GetOperatingPoint();
    const auto &transient = results.GetTransient();
    const ShownResult shown =
        ChooseShownResult(operating_point.has_value(), transient && !transient->Times.empty(), last_run);
    std::optional<ShownTransient> shown_transient;
    if (shown == ShownResult::TRANSIENT && transient) {
        shown_transient.emplace(ShownTransient{.Data = *transient,
                                               .DataVersion = results.GetTransientDataVersion(),
                                               .Instant = m_Playback.Time});
    }
    const double now = ImGui::GetTime();
    m_FuseDisplays = m_Fuses.Update(schematic.GetElements(),
                                    schematic.GetConnectivity(),
                                    shown_transient ? &*shown_transient : nullptr,
                                    now);
    // Without a transient to show, the schematic shows the operating point, if there is one
    m_Displayed = BuildDisplayedState(schematic, shown_transient ? &*shown_transient : nullptr, operating_point, now);
}

/**
 * @brief   Shows nothing until the next Resolve(), as when the editor switches to another schematic.
 */
void DisplayedStateResolver::Reset() {
    m_Displayed.reset();
}

/**
 * @brief   What the schematic shows this frame.
 * @return  The values on display, or no value before any operating point or transient.
 */
const std::optional<DisplayedState> &DisplayedStateResolver::GetDisplayed() const {
    return m_Displayed;
}

/**
 * @brief   What each fuse shows this frame.
 * @return  One entry per element as of the last Resolve(): whether a fuse is open and how strong its flash is, and
 *          none for the other parts.
 */
const std::vector<std::optional<FuseDisplay>> &DisplayedStateResolver::GetFuseDisplays() const {
    return m_FuseDisplays;
}

/**
 * @brief   Reads the result shown into the values on display, with the powers of the parts and what the logic
 *          displays read.
 * @param[in] schematic        The schematic being edited.
 * @param[in] transient        The transient to show, or null to show the operating point.
 * @param[in] operating_point  The operating point, shown when there is no transient to show.
 * @param[in] now              The screen clock, in seconds, which paces the heat scale of a transient still running.
 * @return  The values on display, or none when there is no result to show.
 */
std::optional<DisplayedState>
DisplayedStateResolver::BuildDisplayedState(Schematic &schematic,
                                            const ShownTransient *transient,
                                            const std::optional<Core::OperatingPoint> &operating_point,
                                            const double now) {
    const auto &elements = schematic.GetElements();
    const Connectivity &connectivity = schematic.GetConnectivity();
    DisplayedState state;
    if (transient != nullptr) {
        state.Point = SampleTransient(transient->Data, transient->Instant);
        state.Scale = RefreshTransientScale(*transient, schematic, now);
        state.Time = transient->Instant;
    } else if (operating_point) {
        state.Point = *operating_point;
        state.Scale = ScaleOf(state.Point, elements, connectivity);
    } else {
        return std::nullopt;
    }
    state.Powers = ComputePowers(elements, connectivity, state.Point);
    state.FuseBlowTimes = m_Fuses.GetBlowTimes();
    state.LogicReadings.resize(elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
        state.LogicReadings[index] = ReadLogicDisplay(*elements[index], connectivity, state.Point);
    }
    return state;
}

/**
 * @brief   The heat scale of a transient over the window that plays, worked out again only when IsScaleDue() says so,
 *          so a window of any size costs the same.
 * @return  The scale kept, which may be from an earlier frame while chunks keep arriving.
 */
const HeatScale &
DisplayedStateResolver::RefreshTransientScale(const ShownTransient &transient, Schematic &schematic, const double now) {
    const ScaleKey wanted = {.DataVersion = transient.DataVersion, .Window = m_Playback.Window.GetWindow()};
    if (IsScaleDue(m_TransientScale, wanted, transient.Data.IsComplete, now)) {
        m_TransientScale.Scale = ScaleOf(SliceTransient(transient.Data, wanted.Window),
                                         schematic.GetElements(),
                                         schematic.GetConnectivity());
        m_TransientScale.Key = wanted;
        m_TransientScale.LastTime = now;
    }
    return m_TransientScale.Scale;
}

} // namespace GUI
