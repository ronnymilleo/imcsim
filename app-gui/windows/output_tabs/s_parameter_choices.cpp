/**
 * @file    s_parameter_choices.cpp
 * @brief   What the S-parameters tab shows: the view, the entries checked for each view and quantity, and what the body
 *          draws once those are chosen.
 */

#include "windows/output_tabs/s_parameter_choices.h"

namespace GUI {

namespace {

constexpr std::size_t S_MAGNITUDE_PHASE_SLOT = 0;
constexpr std::size_t S_SMITH_SLOT = 1;
constexpr std::size_t S_POLAR_SLOT = 2;
constexpr std::size_t Z_SLOT = 3;
constexpr std::size_t Y_SLOT = 4;

} // namespace

/**
 * @brief   Starts every selection again for a result with another number of ports.
 * @param[in] port_count  Ports of the new result.
 */
void SParameterSelections::Reset(const int port_count) {
    for (std::size_t slot = 0; slot < SELECTION_COUNT; ++slot) {
        if (slot == S_SMITH_SLOT) {
            m_Selections[slot].ResetToReflections(port_count);
        } else {
            m_Selections[slot].Reset(port_count);
        }
    }
}

/**
 * @brief   The number of ports the selections were set for.
 * @return  The ports of the last Reset(), or 0 before the first.
 */
int SParameterSelections::GetPortCount() const {
    return m_Selections[S_MAGNITUDE_PHASE_SLOT].GetPortCount();
}

/**
 * @brief   The entries checked for a quantity and a view.
 * @param[in] quantity  S, Z or Y.
 * @param[in] view      The view drawn; only S tells the Smith chart apart.
 * @return  The selection, which the set keeps.
 */
SParameterSelection &SParameterSelections::Get(const SParameterQuantity quantity, const SParameterView view) {
    return m_Selections[GetSlot(quantity, view)];
}

/**
 * @brief   The entries checked for a quantity and a view.
 * @param[in] quantity  S, Z or Y.
 * @param[in] view      The view drawn; only S tells the Smith chart apart.
 * @return  Read-only selection for that quantity and view.
 */
const SParameterSelection &SParameterSelections::Get(const SParameterQuantity quantity,
                                                     const SParameterView view) const {
    return m_Selections[GetSlot(quantity, view)];
}

std::size_t SParameterSelections::GetSlot(const SParameterQuantity quantity, const SParameterView view) {
    switch (quantity) {
    case SParameterQuantity::Z:
        return Z_SLOT;
    case SParameterQuantity::Y:
        return Y_SLOT;
    case SParameterQuantity::S:
    default:
        if (view == SParameterView::SMITH) {
            return S_SMITH_SLOT;
        }
        return view == SParameterView::POLAR ? S_POLAR_SLOT : S_MAGNITUDE_PHASE_SLOT;
    }
}

/**
 * @brief   Decides what the body of the tab draws, for the drawing and the export to agree.
 * @param[in] result       The S-parameters shown.
 * @param[in] quantity     S, Z or Y.
 * @param[in] view         The view drawn.
 * @param[in] has_checked  Whether the selection of the view has an entry checked.
 * @return  Why there is nothing to draw, Z or Y at no frequency or no entry checked, or the view to draw; the stability
 *          needs no entry.
 */
SParameterBody ChooseBody(const Core::SParameters &result,
                          const SParameterQuantity quantity,
                          const SParameterView view,
                          const bool has_checked) {
    if (AreAllFrequenciesAbsent(result, quantity)) {
        return SParameterBody::NO_FREQUENCY;
    }
    if (view == SParameterView::STABILITY) {
        return SParameterBody::STABILITY;
    }
    if (!has_checked) {
        return SParameterBody::NOTHING_CHECKED;
    }
    if (view == SParameterView::SMITH) {
        return SParameterBody::SMITH;
    }
    return view == SParameterView::POLAR ? SParameterBody::POLAR : SParameterBody::MAGNITUDE_PHASE;
}

} // namespace GUI
