/**
 * @file    monte_carlo.h
 * @brief   Draws the values of the parts within their tolerances and builds the circuit of one Monte Carlo run.
 * @details Independent of the simulation backend and of the platform: the draws are written here instead of taken from
 *          the standard distributions, whose results differ between standard libraries, so a seed gives the same values
 *          everywhere and run k can be reproduced without making the runs before it.
 */

#ifndef IMCSIM_MONTE_CARLO_H
#define IMCSIM_MONTE_CARLO_H

#include "circuit/circuit.h"
#include "circuit/components/component.h"
#include <expected>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace Core {

/**
 * @enum    ToleranceDistribution
 * @brief   How a value is spread within its tolerance.
 * @details Gaussian takes the tolerance as three standard deviations and draws no value beyond it; uniform draws any
 *          value within the tolerance with the same chance.
 */
enum class ToleranceDistribution {
    GAUSSIAN,
    UNIFORM,
};

/// Fewest runs a Monte Carlo analysis may make.
constexpr int MIN_MONTE_CARLO_RUNS = 2;
/// Most runs a Monte Carlo analysis may make: a few minutes for the slowest examples.
constexpr int MAX_MONTE_CARLO_RUNS = 1000;
/// Runs at first.
constexpr int DEFAULT_MONTE_CARLO_RUNS = 100;
/// Standard deviations a toleranced value spans in both directions with the Gaussian distribution.
constexpr double TOLERANCE_SIGMAS = 3.0;

/**
 * @struct  MonteCarloSettings
 * @brief   How the runs of a Monte Carlo analysis are drawn: how many, how the values spread and the seed.
 * @details Seed zero means none was chosen yet; the settings panel picks a random one when Monte Carlo is first
 *          turned on. Run 0 always keeps every value as it is, the nominal run, and runs 1 to Runs draw the values.
 */
struct MonteCarloSettings {
    bool Enabled = false;
    int Runs = DEFAULT_MONTE_CARLO_RUNS;
    ToleranceDistribution Distribution = ToleranceDistribution::GAUSSIAN;
    unsigned int Seed = 0;

    bool operator==(const MonteCarloSettings &) const = default;
};

/**
 * @struct  MonteCarloCircuit
 * @brief   The circuit of one Monte Carlo run and the value each toleranced part took.
 * @details Simulated refers to Parts, the copies of the varied parts, so both live together.
 */
struct MonteCarloCircuit {
    std::vector<std::unique_ptr<Component>> Parts;
    Circuit Simulated;
    /// Value of each varied part, in the order FindVariedParts() lists them.
    std::vector<double> Values;
};

// Random numbers
std::mt19937_64 MakeRunEngine(unsigned int seed, int run);
double DrawUniform(std::mt19937_64 &engine);
double DrawStandardNormal(std::mt19937_64 &engine);
double DrawToleranceFactor(std::mt19937_64 &engine, ToleranceDistribution distribution, double tolerance_percent);

// Parts
bool IsVaried(const Component &part);
std::vector<const Component *> FindVariedParts(const Circuit &circuit);
std::optional<std::string> FindBlockTolerance(const Circuit &circuit);
std::size_t SetTolerances(std::span<Component *const> parts, ComponentType type, std::optional<double> percent);

// Runs
std::optional<std::string> CheckMonteCarlo(const Circuit &circuit, const MonteCarloSettings &settings);
MonteCarloCircuit BuildMonteCarloCircuit(const Circuit &circuit, const MonteCarloSettings &settings, int run);

// Names
const char *GetDistributionName(ToleranceDistribution distribution);

} // namespace Core

#endif // IMCSIM_MONTE_CARLO_H
