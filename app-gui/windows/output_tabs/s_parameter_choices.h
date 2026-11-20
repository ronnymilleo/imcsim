/**
 * @file    s_parameter_choices.h
 * @brief   What the S-parameters tab shows: the view, the entries checked for each view and quantity, and what the body
 *          draws once those are chosen.
 */

#ifndef IMCSIM_S_PARAMETER_CHOICES_H
#define IMCSIM_S_PARAMETER_CHOICES_H

#include "plots/port_matrix_view.h"
#include "plots/s_parameter_view.h"
#include "simulation/simulation_results.h"
#include <array>
#include <cstddef>

namespace GUI {

/**
 * @enum    SParameterView
 * @brief   What the tab draws: the Smith chart, the magnitude and phase plots or the stability of a two-port.
 */
enum class SParameterView {
    SMITH,
    MAGNITUDE_PHASE,
    STABILITY
};

/**
 * @enum    SParameterBody
 * @brief   What the body of the tab draws under its controls.
 */
enum class SParameterBody {
    /// Z or Y exist at no frequency of the sweep, so the body says why instead.
    NO_FREQUENCY,
    /// No entry of the matrix is checked, so there is nothing to plot.
    NOTHING_CHECKED,
    STABILITY,
    SMITH,
    MAGNITUDE_PHASE
};

/**
 * @class   SParameterSelections
 * @brief   The entries checked for each view of S, and for Z and Y, each kept apart so switching back finds them.
 * @details The Smith chart starts from the reflections, the others from S11 and S21.
 */
class SParameterSelections {
public:
    void Reset(int port_count);
    int GetPortCount() const;
    SParameterSelection &Get(SParameterQuantity quantity, SParameterView view);
    const SParameterSelection &Get(SParameterQuantity quantity, SParameterView view) const;

private:
    /// The magnitude and phase of S, the Smith chart of S, Z and Y, in that order.
    static constexpr std::size_t SELECTION_COUNT = 4;

    std::array<SParameterSelection, SELECTION_COUNT> m_Selections;

    static std::size_t GetSlot(SParameterQuantity quantity, SParameterView view);
};

// Body
SParameterBody
ChooseBody(const Core::SParameters &result, SParameterQuantity quantity, SParameterView view, bool has_checked);

} // namespace GUI

#endif // IMCSIM_S_PARAMETER_CHOICES_H
