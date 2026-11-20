/**
 * @file    step_widgets_tests.cpp
 * @brief   Tests for the traces the step plots of the Output window read from a stepped run.
 */

#include "windows/output_tabs/plot_widgets.h"
#include "windows/output_tabs/step_widgets.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("Each step of a stepped run gives the step plots its own traces, in order", "[step_widgets]") {
    std::vector<Core::Transient> steps(2);
    steps[0].Times = {0.0, 1.0};
    steps[1].Times = {0.0, 2.0};
    const std::vector<GUI::StepTraces> traces =
        GUI::MakeStepTraces(steps, &Core::Transient::Times, &Core::Transient::NodeVoltages, &Core::Transient::Currents);
    REQUIRE(traces.size() == 2);
    CHECK(traces[1].Xs == &steps[1].Times);
    CHECK(traces[0].Nodes == &steps[0].NodeVoltages);
    CHECK(traces[0].Currents == &steps[0].Currents);
}

TEST_CASE("A math channel reads in its own unit unless the plot gives one", "[step_widgets]") {
    const GUI::MathTrace channel{.Label = "M1", .Values = {1.0, 2.0}, .Unit = "W", .Axis = ImAxis_Y3, .Color = 7};
    const GUI::PlottedTrace own = GUI::ToPlottedTrace(channel);
    CHECK(own.Label == "M1");
    CHECK(own.Values == &channel.Values);
    CHECK(std::string(own.Unit) == "W");
    CHECK(own.Color == 7);
    CHECK(std::string(GUI::ToPlottedTrace(channel, " dB").Unit) == " dB");
}
