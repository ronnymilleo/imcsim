/**
 * @file    raw_plot_tests.cpp
 * @brief   Tests of the plot that the rawfile exporter builds for every analysis, and of the files it writes with it.
 */

#include <catch2/catch_test_macros.hpp>

#include "export/raw_plot.h"

#include <nodalys/nodalys.h>

#include <string>

namespace {

/// The first five lines of every plot: the date is fixed, so the same simulation always writes the same bytes.
const std::string HEADER = "Title: t\nDate: Mon Nov  9 22:00:00 2026\n";

} // namespace

TEST_CASE("A raw plot writes its header, its variables and the point number before the first value of each point",
          "[raw_plot]") {
    Nodalys::RawPlot plot("Transient Analysis", false);
    plot.AddVariable("time", "time");
    plot.AddVariable("v(1)", "voltage");
    plot.BeginPoint();
    plot.AddReal(0.0);
    plot.AddReal(1.0);
    plot.BeginPoint();
    plot.AddReal(1e-3);
    plot.AddReal(2.0);

    const std::string expected = HEADER
                               + "Plotname: Transient Analysis\n"
                                 "Flags: real\n"
                                 "No. Variables: 2\n"
                                 "No. Points: 2\n"
                                 "Variables:\n"
                                 "\t0\ttime\ttime\n"
                                 "\t1\tv(1)\tvoltage\n"
                                 "Values:\n"
                                 "0\t0.000000000000000e+00\n"
                                 "\t1.000000000000000e+00\n"
                                 "1\t1.000000000000000e-03\n"
                                 "\t2.000000000000000e+00\n";
    CHECK(plot.ToText("t") == expected);
}

TEST_CASE("A raw plot counts a point that has no values, as an operating point of a circuit with no variables",
          "[raw_plot]") {
    Nodalys::RawPlot plot("Operating Point", false);
    plot.BeginPoint();

    const std::string expected = HEADER
                               + "Plotname: Operating Point\n"
                                 "Flags: real\n"
                                 "No. Variables: 0\n"
                                 "No. Points: 1\n"
                                 "Variables:\n"
                                 "Values:\n";
    CHECK(plot.ToText("t") == expected);
}

TEST_CASE("A complex raw plot writes the real and the imaginary part of a value separated by a comma", "[raw_plot]") {
    Nodalys::RawPlot plot("AC Analysis", true);
    plot.AddVariable("frequency", "frequency");
    plot.AddVariable("pole", "complex");
    plot.BeginPoint();
    plot.AddComplex({10.0, 0.0});
    plot.AddText("0.0,0.0");

    const std::string text = plot.ToText("t");
    CHECK(text.find("Flags: complex\n") != std::string::npos);
    // A cell written as text goes in as it is, the way the pole-zero plot pads its shorter list
    CHECK(text.ends_with("Values:\n0\t1.000000000000000e+01,0.000000000000000e+00\n\t0.0,0.0\n"));
}

TEST_CASE("The raw file of a pole-zero result pads the shorter list with zeros", "[raw_plot][export]") {
    Nodalys::PoleZeroResult result;
    result.Poles = {{-1.0, 2.0}, {-3.0, 0.0}};
    result.Zeros = {{-5.0, 0.0}};

    const std::string text = Nodalys::ExportRaw(result, "t");
    CHECK(text.find("No. Variables: 2\nNo. Points: 2\n") != std::string::npos);
    CHECK(text.ends_with("Values:\n"
                         "0\t-1.000000000000000e+00,2.000000000000000e+00\n"
                         "\t-5.000000000000000e+00,0.000000000000000e+00\n"
                         "1\t-3.000000000000000e+00,0.000000000000000e+00\n"
                         "\t0.0,0.0\n"));
}

TEST_CASE("The raw file of a DC sweep with no curves has no variables and no points", "[raw_plot][export]") {
    const std::string expected = HEADER
                               + "Plotname: DC transfer characteristic\n"
                                 "Flags: real\n"
                                 "No. Variables: 0\n"
                                 "No. Points: 0\n"
                                 "Variables:\n"
                                 "Values:\n";
    CHECK(Nodalys::ExportRaw(Nodalys::DcSweepResult{}, "t") == expected);
}

TEST_CASE("The raw file of a transient reads zero where a series stops short of the time points",
          "[raw_plot][export]") {
    Nodalys::TransientResult result;
    result.Times = {0.0, 1.0};
    // Node 0 is ground; node 1 has only the first of the two points
    result.NodeVoltages = {{0.0, 0.0}, {5.0}};

    const std::string text = Nodalys::ExportRaw(result, "t");
    CHECK(text.ends_with("1\t1.000000000000000e+00\n\t0.000000000000000e+00\n"));
}

TEST_CASE("The raw file of a harmonic balance writes the waveform plot only when there are time points",
          "[raw_plot][export]") {
    Nodalys::HarmonicBalanceResult result;
    result.Frequencies = {0.0, 1e3};
    result.NodePhasors = {{}, {{1.0, 0.0}, {0.5, 0.5}}};

    const auto count_plots = [](const std::string &text) {
        std::size_t count = 0;
        for (std::size_t at = text.find("Title: "); at != std::string::npos; at = text.find("Title: ", at + 1)) {
            ++count;
        }
        return count;
    };
    CHECK(count_plots(Nodalys::ExportRaw(result, "t")) == 1);

    result.Times = {0.0};
    result.NodeWaveforms = {{}, {1.0}};
    CHECK(count_plots(Nodalys::ExportRaw(result, "t")) == 2);
}
