/**
 * @file    stability_summary_tests.cpp
 * @brief   Tests of the sentences about the stability of a two-port: where it can oscillate, the lowest K and mu, the
 *          odd cases of a single point, edges, unilateral and silent two-ports, and what the circles say.
 */

#include "results/stability_summary.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

/**
 * @struct  Point
 * @brief   Rollett factor K and geometric stability factor mu of one frequency point.
 */
struct Point {
    double K = 2.0;
    double Mu = 2.0;
};

/**
 * @brief   A sweep whose points have the given K and mu, stepped by 100 MHz from 100 MHz, stable when mu is above 1.
 */
GUI::TwoPortSweep MakeSweep(const std::vector<Point> &points) {
    GUI::TwoPortSweep sweep;
    double frequency = 100e6;
    for (const Point &point : points) {
        GUI::TwoPortMetrics metrics;
        metrics.K = point.K;
        metrics.Mu = point.Mu;
        metrics.MuPrime = point.Mu;
        metrics.IsUnconditionallyStable = point.Mu > 1.0;
        metrics.HasForwardTransmission = true;
        sweep.Frequencies.push_back(frequency);
        sweep.Metrics.push_back(metrics);
        frequency += 100e6;
    }
    return sweep;
}

} // namespace

TEST_CASE("Runs of unstable points are found in order, with an invalid point ending a run", "[stability_summary]") {
    GUI::TwoPortSweep sweep = MakeSweep({{2, 2}, {0.5, 0.6}, {0.4, 0.5}, {2, 2}, {0.3, 0.4}, {2, 2}});
    CHECK(GUI::FindUnstableRegions(sweep) == std::vector<GUI::UnstableRegion>{{1, 2}, {4, 4}});
    sweep.Metrics[2].IsValid = false;
    CHECK(GUI::FindUnstableRegions(sweep) == std::vector<GUI::UnstableRegion>{{1, 1}, {4, 4}});
    CHECK(GUI::FindUnstableRegions(MakeSweep({{2, 2}, {3, 3}})).empty());
}

TEST_CASE("The default frequency of the circles is where mu is lowest, the first one when tied",
          "[stability_summary]") {
    CHECK(GUI::FindLowestMu(MakeSweep({{2, 2}, {1, 0.7}, {1, 0.7}, {2, 2}})) == 1);
    CHECK_FALSE(GUI::FindLowestMu(GUI::TwoPortSweep{}));
    // With mu infinite everywhere, the first point
    const double infinite = std::numeric_limits<double>::infinity();
    CHECK(GUI::FindLowestMu(MakeSweep({{infinite, infinite}, {infinite, infinite}})) == 0);
    GUI::TwoPortSweep invalid = MakeSweep({{2, 2}, {2, 1.5}});
    invalid.Metrics[1].IsValid = false;
    CHECK(GUI::FindLowestMu(invalid) == 0);
}

TEST_CASE("A sweep that is stable everywhere says so with its smallest margin", "[stability_summary]") {
    CHECK(GUI::DescribeStability(MakeSweep({{3, 3}, {1.4, 1.07}, {2, 2}}))
          == "Unconditionally stable over the whole sweep; the smallest margin is mu = 1.07 at 200MHz.");
}

TEST_CASE("A sweep that is unstable in one band names its ends and its lowest K", "[stability_summary]") {
    const std::string text = GUI::DescribeStability(MakeSweep({{3, 3}, {0.8, 0.9}, {0.42, 0.5}, {0.9, 0.95}, {3, 3}}));
    CHECK(text
          == "Potentially unstable from 200MHz to 400MHz (K < 1); lowest K is 0.42 at 300MHz. Unconditionally stable "
             "elsewhere in the sweep.");
}

TEST_CASE("A sweep that is unstable everywhere says so", "[stability_summary]") {
    CHECK(GUI::DescribeStability(MakeSweep({{0.8, 0.9}, {0.42, 0.5}, {0.9, 0.95}}))
          == "Potentially unstable over the whole sweep (K < 1); lowest K is 0.42 at 200MHz.");
}

TEST_CASE("A sweep unstable at its edges only tells the two bands", "[stability_summary]") {
    const std::string text = GUI::DescribeStability(MakeSweep({{0.5, 0.6}, {0.7, 0.8}, {3, 3}, {3, 3}, {0.9, 0.95}}));
    CHECK_THAT(text, ContainsSubstring("Potentially unstable up to 200MHz and at 500MHz (K < 1)"));
    CHECK_THAT(text, ContainsSubstring("lowest K is 0.5 at 100MHz"));
    CHECK_THAT(text, ContainsSubstring("Unconditionally stable elsewhere in the sweep."));
    CHECK_THAT(GUI::DescribeStability(MakeSweep({{3, 3}, {3, 3}, {0.5, 0.6}, {0.7, 0.8}})),
               ContainsSubstring("Potentially unstable from 300MHz upward (K < 1)"));
}

TEST_CASE("Many separate bands are counted instead of listed", "[stability_summary]") {
    const std::string text = GUI::DescribeStability(MakeSweep({{0.5, 0.6}, {3, 3}, {0.5, 0.6}, {3, 3}, {0.5, 0.6}}));
    CHECK_THAT(text, ContainsSubstring("in 3 separate bands between 100MHz and 500MHz (K < 1)"));
}

TEST_CASE("A single point is told as one, stable or not", "[stability_summary]") {
    CHECK(GUI::DescribeStability(MakeSweep({{1.8, 1.4}})) == "Unconditionally stable at 100MHz (K = 1.8, mu = 1.4).");
    CHECK(GUI::DescribeStability(MakeSweep({{0.42, 0.5}})) == "Potentially unstable at 100MHz (K = 0.42).");
}

TEST_CASE("A point that is unstable with K above 1 is told by mu", "[stability_summary]") {
    // |Delta| above 1: K says nothing
    const std::string text = GUI::DescribeStability(MakeSweep({{3, 3}, {4.2, -0.8}, {3, 3}}));
    CHECK_THAT(text, ContainsSubstring("Potentially unstable at 200MHz (mu < 1); lowest mu is -0.8 at 200MHz."));
}

TEST_CASE("A sweep with no finite matrix cannot be told", "[stability_summary]") {
    GUI::TwoPortSweep sweep = MakeSweep({{2, 2}, {2, 2}});
    for (GUI::TwoPortMetrics &metrics : sweep.Metrics) {
        metrics.IsValid = false;
    }
    CHECK(GUI::DescribeStability(sweep) == "The S matrix has no finite values, so its stability cannot be told.");
    CHECK(GUI::DescribeStability(GUI::TwoPortSweep{}).empty());
}

TEST_CASE("A unilateral sweep says that K is infinite and the gain is the unilateral one", "[stability_summary]") {
    const double infinite = std::numeric_limits<double>::infinity();
    GUI::TwoPortSweep sweep = MakeSweep({{infinite, 2.5}, {infinite, 3.0}});
    for (GUI::TwoPortMetrics &metrics : sweep.Metrics) {
        metrics.IsUnilateral = true;
    }
    const std::string text = GUI::DescribeStability(sweep);
    CHECK_THAT(text, ContainsSubstring("Unconditionally stable over the whole sweep; the smallest margin is mu = 2.5"));
    CHECK_THAT(text, EndsWith("K is infinite and the gain shown is the unilateral power gain."));
    sweep.Metrics[1].IsUnilateral = false;
    CHECK_THAT(GUI::DescribeStability(sweep), ContainsSubstring("S12 is 0 at 1 of 2 points"));
}

TEST_CASE("A sweep that transmits nothing says there is no gain to bound", "[stability_summary]") {
    GUI::TwoPortSweep sweep = MakeSweep({{2, 2}, {2, 2}});
    for (GUI::TwoPortMetrics &metrics : sweep.Metrics) {
        metrics.HasForwardTransmission = false;
        metrics.IsUnilateral = true;
    }
    CHECK_THAT(GUI::DescribeStability(sweep),
               EndsWith("S21 is 0, so nothing is transmitted and there is no gain to bound."));
    sweep.Metrics[0].HasForwardTransmission = true;
    CHECK_THAT(GUI::DescribeStability(sweep), ContainsSubstring("S21 is 0 at 1 of 2 points"));
}

TEST_CASE("The circles note says nothing when both cross the chart and names each one that does not",
          "[stability_summary]") {
    GUI::StabilityCircles circles;
    circles.Source.Coverage = GUI::ChartCoverage::PARTLY;
    circles.Load.Coverage = GUI::ChartCoverage::PARTLY;
    CHECK(GUI::DescribeCircles(circles, 180e6).empty());
    circles.Load.Coverage = GUI::ChartCoverage::ENTIRELY_STABLE;
    CHECK(GUI::DescribeCircles(circles, 180e6)
          == "Load circle: the whole chart is stable at 180MHz, so no circle shows.");
    circles.Source.Coverage = GUI::ChartCoverage::ENTIRELY_UNSTABLE;
    CHECK(
        GUI::DescribeCircles(circles, 180e6)
        == "Source circle: the whole chart is unstable at 180MHz, so no circle shows. Load circle: the whole chart is "
           "stable at 180MHz, so no circle shows.");
}

TEST_CASE("The metrics of a point read as lines for a tooltip", "[stability_summary]") {
    GUI::TwoPortMetrics metrics;
    metrics.K = 0.917;
    metrics.DeltaMagnitude = 0.342;
    metrics.Mu = 0.942;
    metrics.MuPrime = 0.975;
    metrics.Gain = GUI::GainKind::MSG;
    metrics.GainDecibels = 34.54;
    const std::vector<std::string> lines = GUI::DescribeMetrics(metrics, 180e6);
    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "Stability at 180MHz");
    CHECK(lines[1] == "K = 0.917, |Delta| = 0.342");
    CHECK(lines[2] == "mu = 0.942, mu' = 0.975");
    CHECK(lines[3] == "Potentially unstable");
    CHECK(lines[4] == "MSG 34.5 dB");
    metrics.IsValid = false;
    CHECK(GUI::DescribeMetrics(metrics, 180e6).size() == 2);
}

TEST_CASE("The Smith chart labels its circles with their frequency and the mu found there", "[stability_summary]") {
    GUI::TwoPortMetrics metrics;
    metrics.Mu = 0.942;
    CHECK(GUI::DescribeCircleFrequency(metrics, 180e6) == "Circles at 180MHz, mu = 0.942");
    metrics.IsValid = false;
    CHECK(GUI::DescribeCircleFrequency(metrics, 180e6) == "Circles at 180MHz, no finite S matrix");
}
