/**
 * @file    s_parameter_choices_tests.cpp
 * @brief   Tests of what the S-parameters tab shows: the entries kept for each view and quantity, and what the body
 *          draws.
 */

#include "windows/output_tabs/s_parameter_choices.h"

#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <optional>
#include <vector>

namespace {

/**
 * @brief   A two-port at two frequencies, with Z and Y at both or at neither.
 */
Core::SParameters MakeTwoPort(const bool has_matrices) {
    Core::SParameters result;
    result.Frequencies = {1e6, 1e7};
    result.ReferenceOhms = {50.0, 50.0};
    for (std::size_t frequency = 0; frequency < 2; ++frequency) {
        result.Values.insert(result.Values.end(), 4, std::complex<double>(0.1, 0.0));
        result.ImpedanceMatrices.emplace_back(
            has_matrices ? std::optional(std::vector<std::complex<double>>(4, {50.0, 0.0})) : std::nullopt);
        result.AdmittanceMatrices.emplace_back(
            has_matrices ? std::optional(std::vector<std::complex<double>>(4, {0.02, 0.0})) : std::nullopt);
    }
    return result;
}

} // namespace

TEST_CASE("Each view of S, and Z and Y, keep their own checked entries", "[s_parameter_choices]") {
    GUI::SParameterSelections selections;
    selections.Reset(2);
    CHECK(selections.GetPortCount() == 2);
    // The Smith chart starts from the reflections, the magnitude and phase from S11 and S21
    CHECK(selections.Get(GUI::SParameterQuantity::S, GUI::SParameterView::SMITH).IsShown(2, 2));
    CHECK_FALSE(selections.Get(GUI::SParameterQuantity::S, GUI::SParameterView::MAGNITUDE_PHASE).IsShown(2, 2));

    selections.Get(GUI::SParameterQuantity::Z, GUI::SParameterView::MAGNITUDE_PHASE).SetShown(1, 2, true);
    CHECK(selections.Get(GUI::SParameterQuantity::Z, GUI::SParameterView::SMITH).IsShown(1, 2));
    CHECK_FALSE(selections.Get(GUI::SParameterQuantity::Y, GUI::SParameterView::MAGNITUDE_PHASE).IsShown(1, 2));
    CHECK_FALSE(selections.Get(GUI::SParameterQuantity::S, GUI::SParameterView::MAGNITUDE_PHASE).IsShown(1, 2));
}

TEST_CASE("The body says why it draws nothing, or draws the view", "[s_parameter_choices]") {
    const Core::SParameters with = MakeTwoPort(true);
    const Core::SParameters without = MakeTwoPort(false);
    using Body = GUI::SParameterBody;
    CHECK(GUI::ChooseBody(without, GUI::SParameterQuantity::Z, GUI::SParameterView::MAGNITUDE_PHASE, true)
          == Body::NO_FREQUENCY);
    CHECK(GUI::ChooseBody(with, GUI::SParameterQuantity::S, GUI::SParameterView::SMITH, false)
          == Body::NOTHING_CHECKED);
    // The stability needs no entry checked
    CHECK(GUI::ChooseBody(with, GUI::SParameterQuantity::S, GUI::SParameterView::STABILITY, false) == Body::STABILITY);
    CHECK(GUI::ChooseBody(with, GUI::SParameterQuantity::S, GUI::SParameterView::SMITH, true) == Body::SMITH);
    CHECK(GUI::ChooseBody(with, GUI::SParameterQuantity::Y, GUI::SParameterView::MAGNITUDE_PHASE, true)
          == Body::MAGNITUDE_PHASE);
}
