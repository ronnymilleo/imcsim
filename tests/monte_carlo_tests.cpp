/**
 * @file    monte_carlo_tests.cpp
 * @brief   Tests for the draws of a Monte Carlo analysis and the circuit of each of its runs.
 */

#include "simulation/monte_carlo.h"

#include "circuit/components/capacitor.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/voltage_source.h"
#include "circuit/subcircuit.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

/// A 1k resistor within 5 % and a 100n capacitor within 10 % in series, to ground, fed by a 5 V source.
struct Filter {
    Core::VoltageSource Supply;
    Core::Resistor Series;
    Core::Capacitor Shunt;
    Core::Ground Reference;
    Core::Circuit Circuit;

    Filter() {
        Supply.SetName("V1");
        Supply.SetValue(5.0);
        Series.SetName("R1");
        Series.SetValue(1e3);
        Series.SetTolerance(5.0);
        Shunt.SetName("C1");
        Shunt.SetValue(100e-9);
        Shunt.SetTolerance(10.0);
        Circuit.Add(Supply, {1, 0});
        Circuit.Add(Series, {1, 2});
        Circuit.Add(Shunt, {2, 0});
        Circuit.Add(Reference, {0});
    }
};

constexpr Core::MonteCarloSettings SEEDED{.Enabled = true, .Runs = 10, .Seed = 1234};

} // namespace

TEST_CASE("A part takes a tolerance only when its kind does and the percent is sane", "[monte_carlo]") {
    Core::Resistor resistor;
    CHECK(resistor.SupportsTolerance());
    CHECK_FALSE(resistor.HasTolerance());
    resistor.SetTolerance(5.0);
    CHECK(resistor.GetTolerance() == 5.0);
    resistor.SetTolerance(0.0);
    resistor.SetTolerance(-1.0);
    resistor.SetTolerance(Core::MAX_TOLERANCE_PERCENT + 1.0);
    CHECK(resistor.GetTolerance() == 5.0);
    resistor.ClearTolerance();
    CHECK_FALSE(resistor.HasTolerance());

    Core::Ground ground;
    ground.SetTolerance(5.0);
    CHECK_FALSE(ground.SupportsTolerance());
    CHECK_FALSE(ground.HasTolerance());
}

TEST_CASE("A clone keeps the tolerance", "[monte_carlo]") {
    Core::Capacitor capacitor;
    capacitor.SetTolerance(20.0);
    CHECK(capacitor.Clone()->GetTolerance() == 20.0);
}

TEST_CASE("The uniform draw lies from 0 up to but not including 1 and repeats from a seed", "[monte_carlo]") {
    std::mt19937_64 first = Core::MakeRunEngine(1234, 1);
    std::mt19937_64 second = Core::MakeRunEngine(1234, 1);
    double sum = 0.0;
    for (int index = 0; index < 1000; ++index) {
        const double value = Core::DrawUniform(first);
        CHECK(value == Core::DrawUniform(second));
        CHECK(value >= 0.0);
        CHECK(value < 1.0);
        sum += value;
    }
    CHECK_THAT(sum / 1000.0, Catch::Matchers::WithinAbs(0.5, 0.05));
}

TEST_CASE("The draws are fixed numbers for a seed, so a seed repeats on every platform", "[monte_carlo]") {
    // The generator and the top 53 bits are exact everywhere, so the uniform numbers are checked closely; the logarithm
    // and cosine of the normal draw may differ in the last digits between math libraries
    std::mt19937_64 uniform = Core::MakeRunEngine(1234, 1);
    CHECK_THAT(Core::DrawUniform(uniform), Catch::Matchers::WithinAbs(0.96205700150859397, 1e-15));
    CHECK_THAT(Core::DrawUniform(uniform), Catch::Matchers::WithinAbs(0.18867097431878432, 1e-15));
    CHECK_THAT(Core::DrawUniform(uniform), Catch::Matchers::WithinAbs(0.016176485083659875, 1e-15));
    std::mt19937_64 normal = Core::MakeRunEngine(1234, 1);
    CHECK_THAT(Core::DrawStandardNormal(normal), Catch::Matchers::WithinAbs(0.96148842104438315, 1e-9));
    CHECK_THAT(Core::DrawStandardNormal(normal), Catch::Matchers::WithinAbs(-0.0014073479804356183, 1e-9));
    CHECK_THAT(Core::DrawStandardNormal(normal), Catch::Matchers::WithinAbs(-0.38530753389264782, 1e-9));
    // Run 2 of the same seed: a Gaussian 5 % factor, then a uniform 10 % one
    std::mt19937_64 second_run = Core::MakeRunEngine(1234, 2);
    CHECK_THAT(Core::DrawToleranceFactor(second_run, Core::ToleranceDistribution::GAUSSIAN, 5.0),
               Catch::Matchers::WithinAbs(1.00086762846740207, 1e-9));
    CHECK_THAT(Core::DrawToleranceFactor(second_run, Core::ToleranceDistribution::UNIFORM, 10.0),
               Catch::Matchers::WithinAbs(0.95313398749983347, 1e-9));
}

TEST_CASE("Different runs and different seeds start different streams", "[monte_carlo]") {
    std::mt19937_64 base = Core::MakeRunEngine(1234, 1);
    std::mt19937_64 other_run = Core::MakeRunEngine(1234, 2);
    std::mt19937_64 other_seed = Core::MakeRunEngine(1235, 1);
    const std::mt19937_64::result_type first_bits = base();
    CHECK(other_run() != first_bits);
    CHECK(other_seed() != first_bits);
}

TEST_CASE("The normal draw has a mean of 0, a deviation of 1 and never passes 3 deviations", "[monte_carlo]") {
    std::mt19937_64 engine = Core::MakeRunEngine(7, 1);
    constexpr int COUNT = 20000;
    double sum = 0.0;
    double squares = 0.0;
    double largest = 0.0;
    for (int index = 0; index < COUNT; ++index) {
        const double value = Core::DrawStandardNormal(engine);
        sum += value;
        squares += value * value;
        largest = std::max(largest, std::abs(value));
    }
    CHECK_THAT(sum / COUNT, Catch::Matchers::WithinAbs(0.0, 0.03));
    // Truncating at 3 deviations leaves 0.986 of the deviation
    CHECK_THAT(std::sqrt(squares / COUNT), Catch::Matchers::WithinAbs(0.986, 0.03));
    CHECK(largest <= Core::TOLERANCE_SIGMAS);
}

TEST_CASE("A factor stays within the tolerance for both distributions", "[monte_carlo]") {
    std::mt19937_64 engine = Core::MakeRunEngine(99, 1);
    double lowest = 10.0;
    double highest = 0.0;
    for (const Core::ToleranceDistribution distribution :
         {Core::ToleranceDistribution::GAUSSIAN, Core::ToleranceDistribution::UNIFORM}) {
        for (int index = 0; index < 5000; ++index) {
            const double factor = Core::DrawToleranceFactor(engine, distribution, 10.0);
            CHECK(factor >= 0.9);
            CHECK(factor <= 1.1);
            if (distribution == Core::ToleranceDistribution::UNIFORM) {
                lowest = std::min(lowest, factor);
                highest = std::max(highest, factor);
            }
        }
    }
    // The uniform draw reaches the ends of the tolerance, the Gaussian one thins out towards them
    CHECK(lowest < 0.902);
    CHECK(highest > 1.098);
}

TEST_CASE("The Gaussian draw spreads less than the uniform one for the same tolerance", "[monte_carlo]") {
    std::mt19937_64 engine = Core::MakeRunEngine(5, 1);
    const auto spread = [&engine](const Core::ToleranceDistribution distribution) {
        double squares = 0.0;
        constexpr int COUNT = 10000;
        for (int index = 0; index < COUNT; ++index) {
            const double deviation = Core::DrawToleranceFactor(engine, distribution, 10.0) - 1.0;
            squares += deviation * deviation;
        }
        return std::sqrt(squares / COUNT);
    };
    // A third of the tolerance against its share of 1 over the root of 3
    CHECK_THAT(spread(Core::ToleranceDistribution::GAUSSIAN), Catch::Matchers::WithinAbs(0.033, 0.004));
    CHECK_THAT(spread(Core::ToleranceDistribution::UNIFORM), Catch::Matchers::WithinAbs(0.0577, 0.004));
}

TEST_CASE("The nominal run keeps every value and the others draw them", "[monte_carlo]") {
    Filter filter;
    const Core::MonteCarloCircuit nominal = Core::BuildMonteCarloCircuit(filter.Circuit, SEEDED, 0);
    CHECK(nominal.Values == std::vector<double>{1e3, 100e-9});
    const Core::MonteCarloCircuit drawn = Core::BuildMonteCarloCircuit(filter.Circuit, SEEDED, 1);
    REQUIRE(drawn.Values.size() == 2);
    CHECK(drawn.Values[0] != 1e3);
    CHECK(drawn.Values[0] >= 950.0);
    CHECK(drawn.Values[0] <= 1050.0);
    CHECK(drawn.Values[1] >= 90e-9);
    CHECK(drawn.Values[1] <= 110e-9);
}

TEST_CASE("A run builds the same circuit again, and any run can be made alone", "[monte_carlo]") {
    Filter filter;
    std::vector<std::vector<double>> in_order;
    for (int run = 1; run <= 5; ++run) {
        in_order.push_back(Core::BuildMonteCarloCircuit(filter.Circuit, SEEDED, run).Values);
    }
    CHECK(Core::BuildMonteCarloCircuit(filter.Circuit, SEEDED, 4).Values == in_order[3]);
    CHECK(in_order[0] != in_order[1]);
    Core::MonteCarloSettings other = SEEDED;
    other.Seed = 4321;
    CHECK(Core::BuildMonteCarloCircuit(filter.Circuit, other, 1).Values != in_order[0]);
}

TEST_CASE("A run swaps in copies of the varied parts and leaves the rest and the original alone", "[monte_carlo]") {
    Filter filter;
    const Core::MonteCarloCircuit built = Core::BuildMonteCarloCircuit(filter.Circuit, SEEDED, 3);
    const auto &entries = built.Simulated.GetEntries();
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].Part == &filter.Supply);
    CHECK(entries[1].Part != &filter.Series);
    CHECK(entries[1].Part->GetName() == "R1");
    CHECK(entries[1].Part->GetValue() == built.Values[0]);
    CHECK(entries[2].Part->GetValue() == built.Values[1]);
    CHECK(entries[3].Part == &filter.Reference);
    CHECK(entries[1].Nodes == std::vector<int>{1, 2});
    CHECK(filter.Series.GetValue() == 1e3);
    CHECK(filter.Shunt.GetValue() == 100e-9);
}

TEST_CASE("A DC voltage source with a tolerance varies, and a source of another waveform does not", "[monte_carlo]") {
    Filter filter;
    filter.Supply.SetTolerance(2.0);
    CHECK(Core::FindVariedParts(filter.Circuit).size() == 3);
    filter.Supply.SetSourceType(Core::Source::SourceType::AC);
    CHECK(Core::FindVariedParts(filter.Circuit).size() == 2);
}

TEST_CASE("Tolerances are given to every part of a kind and taken away again", "[monte_carlo]") {
    Core::Resistor first;
    Core::Resistor second;
    Core::Capacitor capacitor;
    const std::vector<Core::Component *> parts = {&first, &second, &capacitor};
    CHECK(Core::SetTolerances(parts, Core::ComponentType::RESISTOR, 1.0) == 2);
    CHECK(first.GetTolerance() == 1.0);
    CHECK(second.GetTolerance() == 1.0);
    CHECK_FALSE(capacitor.HasTolerance());
    CHECK(Core::SetTolerances(parts, Core::ComponentType::RESISTOR, std::nullopt) == 2);
    CHECK_FALSE(first.HasTolerance());
}

TEST_CASE("A Monte Carlo run is refused without a tolerance, with runs out of range or with toleranced parts in a "
          "block",
          "[monte_carlo]") {
    Filter filter;
    CHECK_FALSE(Core::CheckMonteCarlo(filter.Circuit, SEEDED));

    Core::MonteCarloSettings few = SEEDED;
    few.Runs = 1;
    CHECK(Core::CheckMonteCarlo(filter.Circuit, few));
    few.Runs = Core::MAX_MONTE_CARLO_RUNS + 1;
    CHECK(Core::CheckMonteCarlo(filter.Circuit, few));

    filter.Series.ClearTolerance();
    filter.Shunt.ClearTolerance();
    const std::optional<std::string> none = Core::CheckMonteCarlo(filter.Circuit, SEEDED);
    REQUIRE(none);
    CHECK(none->contains("No part has a tolerance"));
}

TEST_CASE("A block that holds a toleranced part refuses the run and names the block and the part", "[monte_carlo]") {
    Core::Resistor inside;
    inside.SetName("R9");
    inside.SetValue(1e3);
    inside.SetTolerance(5.0);
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = "divider";
    definition->Pins = {{"IN", 1}, {"OUT", 2}};
    definition->Body.Add(inside, {1, 2});
    Core::SubcircuitInstance instance("divider", {"IN", "OUT"});
    instance.SetName("X1");

    Core::Resistor outside;
    outside.SetName("R1");
    outside.SetValue(1e3);
    outside.SetTolerance(5.0);
    Core::Circuit circuit;
    circuit.Add(outside, {1, 0});
    circuit.Add(instance, {1, 2});
    circuit.AddSubcircuit(definition);

    const std::optional<std::string> problem = Core::CheckMonteCarlo(circuit, SEEDED);
    REQUIRE(problem);
    CHECK(problem->contains("X1"));
    CHECK(problem->contains("R9"));

    // A block without tolerances is fine
    inside.ClearTolerance();
    CHECK_FALSE(Core::CheckMonteCarlo(circuit, SEEDED));
}

TEST_CASE("Distributions have names", "[monte_carlo]") {
    CHECK(std::string(Core::GetDistributionName(Core::ToleranceDistribution::GAUSSIAN)) == "Gaussian");
    CHECK(std::string(Core::GetDistributionName(Core::ToleranceDistribution::UNIFORM)) == "Uniform");
}
