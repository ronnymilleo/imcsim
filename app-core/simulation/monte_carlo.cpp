/**
 * @file    monte_carlo.cpp
 * @brief   Draws the values of the parts within their tolerances and builds the circuit of one Monte Carlo run.
 */

#include "simulation/monte_carlo.h"

#include "circuit/components/source.h"
#include "circuit/components/subcircuit.h"
#include "circuit/subcircuit.h"
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>

namespace Core {

namespace {

/// Spreads the bits of a counter, so that the seeds of neighboring runs start unrelated streams (SplitMix64).
constexpr std::uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t MIX_FIRST_MULTIPLIER = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t MIX_SECOND_MULTIPLIER = 0x94D049BB133111EBULL;
constexpr int MIX_FIRST_SHIFT = 30;
constexpr int MIX_SECOND_SHIFT = 27;
constexpr int MIX_FINAL_SHIFT = 31;

/// A double has 53 bits of mantissa; the engine gives 64 bits, so the top 53 make the fraction.
constexpr int MANTISSA_DISCARDED_BITS = 11;
constexpr double MANTISSA_SCALE = 1.0 / 9007199254740992.0;

constexpr double PERCENT = 100.0;

std::uint64_t MixSeed(const unsigned int seed, const int run) {
    std::uint64_t state = (static_cast<std::uint64_t>(seed) << 32) + static_cast<std::uint64_t>(run) * GOLDEN_GAMMA;
    state = (state ^ (state >> MIX_FIRST_SHIFT)) * MIX_FIRST_MULTIPLIER;
    state = (state ^ (state >> MIX_SECOND_SHIFT)) * MIX_SECOND_MULTIPLIER;
    return state ^ (state >> MIX_FINAL_SHIFT);
}

/**
 * @brief   Adds the toleranced parts of the body of a block and of the blocks it places, which Monte Carlo does not
 *          vary yet.
 */
void CollectBlockTolerances(const Circuit &circuit, const Circuit &body, std::vector<std::string> &names,
                            const int depth) {
    // A block that contains itself is refused elsewhere; the depth keeps a damaged file from looping here
    constexpr int MAX_NESTING = 32;
    if (depth > MAX_NESTING) {
        return;
    }
    for (const CircuitEntry &entry : body.GetEntries()) {
        if (entry.Part->HasTolerance() && entry.Part->SupportsTolerance()) {
            names.push_back(entry.Part->GetName());
        }
        if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
            continue;
        }
        const auto &instance = static_cast<const SubcircuitInstance &>(*entry.Part);
        if (const auto definition = circuit.FindSubcircuit(instance.GetDefinitionName())) {
            CollectBlockTolerances(circuit, definition->Body, names, depth + 1);
        }
    }
}

} // namespace

/**
 * @brief   Starts the random numbers of one run.
 * @param[in] seed  Seed of the analysis.
 * @param[in] run   Number of the run, 1 or more.
 * @return  An engine that depends on both and on nothing else, so any run can be made again alone.
 */
std::mt19937_64 MakeRunEngine(const unsigned int seed, const int run) {
    return std::mt19937_64(MixSeed(seed, run));
}

/**
 * @brief   Draws a number between 0 and 1.
 * @param[in,out] engine  Source of the bits.
 * @return  A value from 0 up to but not including 1, made of the top 53 bits of the engine, so every platform agrees.
 */
double DrawUniform(std::mt19937_64 &engine) {
    return static_cast<double>(engine() >> MANTISSA_DISCARDED_BITS) * MANTISSA_SCALE;
}

/**
 * @brief   Draws a number of a normal distribution with a mean of 0 and a standard deviation of 1, within 3 deviations.
 * @param[in,out] engine  Source of the bits.
 * @return  The number, by the Box-Muller transform; one beyond TOLERANCE_SIGMAS is drawn again, so a part never lies
 *          outside the tolerance it was given. The transform uses the math library, so the last digits may differ
 *          between platforms.
 */
double DrawStandardNormal(std::mt19937_64 &engine) {
    while (true) {
        // One minus the uniform number lies in (0, 1], which the logarithm accepts
        const double radius = std::sqrt(-2.0 * std::log(1.0 - DrawUniform(engine)));
        const double angle = 2.0 * std::numbers::pi * DrawUniform(engine);
        const double normal = radius * std::cos(angle);
        if (std::abs(normal) <= TOLERANCE_SIGMAS) {
            return normal;
        }
    }
}

/**
 * @brief   Draws the factor a value is multiplied by.
 * @param[in,out] engine            Source of the bits.
 * @param[in] distribution          How the values spread.
 * @param[in] tolerance_percent     Tolerance in percent.
 * @return  A factor from 1 - tolerance to 1 + tolerance.
 */
double DrawToleranceFactor(std::mt19937_64 &engine, const ToleranceDistribution distribution,
                           const double tolerance_percent) {
    const double fraction = tolerance_percent / PERCENT;
    if (distribution == ToleranceDistribution::UNIFORM) {
        return 1.0 + fraction * (2.0 * DrawUniform(engine) - 1.0);
    }
    return 1.0 + fraction * DrawStandardNormal(engine) / TOLERANCE_SIGMAS;
}

/**
 * @brief   Tells whether a part takes another value in each Monte Carlo run.
 * @param[in] part  Part to look at.
 * @return  True for a part with a tolerance; a voltage source only counts while it is a DC one, since the other
 *          waveforms do not use its value.
 */
bool IsVaried(const Component &part) {
    if (!part.HasTolerance() || !part.SupportsTolerance()) {
        return false;
    }
    if (part.GetType() == ComponentType::VOLTAGE_SOURCE) {
        return static_cast<const Source &>(part).GetSourceType() == Source::SourceType::DC;
    }
    return true;
}

/**
 * @brief   Lists the parts the runs vary.
 * @param[in] circuit  Circuit to look in.
 * @return  The parts of the circuit itself with a tolerance, in circuit order.
 */
std::vector<const Component *> FindVariedParts(const Circuit &circuit) {
    std::vector<const Component *> parts;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (IsVaried(*entry.Part)) {
            parts.push_back(entry.Part);
        }
    }
    return parts;
}

/**
 * @brief   Looks for parts with a tolerance inside the blocks of the circuit.
 * @param[in] circuit  Circuit to look in.
 * @return  The reason a run is refused, naming the block and the parts, or no value when no block holds one.
 */
std::optional<std::string> FindBlockTolerance(const Circuit &circuit) {
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (entry.Part->GetType() != ComponentType::SUBCIRCUIT) {
            continue;
        }
        const auto &instance = static_cast<const SubcircuitInstance &>(*entry.Part);
        const auto definition = circuit.FindSubcircuit(instance.GetDefinitionName());
        if (!definition) {
            continue;
        }
        std::vector<std::string> names;
        CollectBlockTolerances(circuit, definition->Body, names, 0);
        if (names.empty()) {
            continue;
        }
        std::string list = names.front();
        for (std::size_t index = 1; index < names.size(); ++index) {
            list += ", " + names[index];
        }
        return std::format("The block {} holds parts with a tolerance ({}), which Monte Carlo does not vary yet; clear "
                           "their tolerance or take the parts out of the block",
                           entry.Part->GetName(), list);
    }
    return std::nullopt;
}

/**
 * @brief   Gives every part of one kind the same tolerance, or takes it from them all.
 * @param[in] parts    Parts to look through.
 * @param[in] type     Kind of part to change.
 * @param[in] percent  The tolerance, or no value to clear it.
 * @return  How many parts were set, which are those of that kind.
 */
std::size_t SetTolerances(const std::span<Component *const> parts, const ComponentType type,
                          const std::optional<double> percent) {
    std::size_t changed = 0;
    for (Component *part : parts) {
        if (part->GetType() != type || !part->SupportsTolerance()) {
            continue;
        }
        if (percent) {
            part->SetTolerance(*percent);
        } else {
            part->ClearTolerance();
        }
        ++changed;
    }
    return changed;
}

/**
 * @brief   Checks what a Monte Carlo run needs before it starts.
 * @param[in] circuit   Circuit to run.
 * @param[in] settings  Number of runs and the distribution.
 * @return  The reason it cannot run, or no value: a number of runs out of range, a block with toleranced parts, or no
 *          part with a tolerance at all.
 */
std::optional<std::string> CheckMonteCarlo(const Circuit &circuit, const MonteCarloSettings &settings) {
    if (settings.Runs < MIN_MONTE_CARLO_RUNS || settings.Runs > MAX_MONTE_CARLO_RUNS) {
        return std::format("Monte Carlo makes {} to {} runs", MIN_MONTE_CARLO_RUNS, MAX_MONTE_CARLO_RUNS);
    }
    if (const std::optional<std::string> block = FindBlockTolerance(circuit)) {
        return block;
    }
    if (FindVariedParts(circuit).empty()) {
        return "No part has a tolerance; set one in the properties of a resistor, capacitor, inductor or DC voltage "
               "source, or with Set tolerance of all in the Monte Carlo settings";
    }
    return std::nullopt;
}

/**
 * @brief   Builds the circuit of one run.
 * @param[in] circuit   Circuit to vary.
 * @param[in] settings  The seed and the distribution.
 * @param[in] run       Number of the run: 0 is the nominal one, which keeps every value, and 1 or more draw them.
 * @return  The circuit with a copy of each toleranced part at its drawn value in place of the original, so every
 *          reading that uses the value of a part sees the value the run had, and those values in the order of
 *          FindVariedParts().
 */
MonteCarloCircuit BuildMonteCarloCircuit(const Circuit &circuit, const MonteCarloSettings &settings, const int run) {
    MonteCarloCircuit built;
    std::mt19937_64 engine = MakeRunEngine(settings.Seed, run);
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (!IsVaried(*entry.Part)) {
            built.Simulated.Add(*entry.Part, entry.Nodes);
            continue;
        }
        std::unique_ptr<Component> copy = entry.Part->Clone();
        if (run > 0) {
            const double factor =
                DrawToleranceFactor(engine, settings.Distribution, entry.Part->GetTolerance().value_or(0.0));
            copy->SetValue(entry.Part->GetValue() * factor);
        }
        built.Values.push_back(copy->GetValue());
        built.Simulated.Add(*copy, entry.Nodes);
        built.Parts.push_back(std::move(copy));
    }
    built.Simulated.AddDefinitionsOf(circuit);
    return built;
}

/**
 * @brief   Names a distribution as the settings panel and the file show it.
 * @param[in] distribution  Distribution to name.
 * @return  "Gaussian" or "Uniform".
 */
const char *GetDistributionName(const ToleranceDistribution distribution) {
    switch (distribution) {
    case ToleranceDistribution::GAUSSIAN:
        return "Gaussian";
    case ToleranceDistribution::UNIFORM:
        return "Uniform";
    default:
        return "";
    }
}

} // namespace Core
