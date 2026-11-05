/**
 * @file    pole_zero.h
 * @brief   Turns the poles and zeros a solver reports into the rows a pole-zero analysis shows, and tells in words what
 *          went wrong when it finds none.
 * @details Independent of the solver: it takes the roots as complex numbers in rad/s and the lines the solver printed,
 *          so any backend that finds them, and the tests with made-up numbers, share the rules for conjugate pairs,
 *          cancellation and quality factors.
 */

#ifndef IMCSIM_POLE_ZERO_H
#define IMCSIM_POLE_ZERO_H

#include "model/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  PoleZeroInput
 * @brief   The port of an input source: the nodes of its terminals, and whether it is a current source.
 */
struct PoleZeroInput {
    int PositiveNode = 0;
    int NegativeNode = 0;
    bool IsCurrent = false;
};

/// Roots as complex numbers in rad/s, in the order the solver reports them.
using ComplexRoots = std::vector<std::complex<double>>;

// Input
std::optional<PoleZeroInput> FindPoleZeroInput(const Circuit &circuit, std::string_view source_name);

// Results
PoleZero BuildPoleZero(const Circuit &circuit, const PoleZeroSettings &settings, const ComplexRoots &poles,
                       const ComplexRoots &zeros);

// Solver messages
std::optional<std::string> DescribePoleZeroFailure(const std::vector<SimulatorMessage> &messages);
std::optional<std::string> FindPoleZeroWarning(const std::vector<SimulatorMessage> &messages);

/// What to tell the user when the solver leaves no pole and no zero without saying why.
inline constexpr std::string_view NO_POLE_ZERO_RESULT =
    "ngspice found no poles or zeros. A circuit with no capacitor or inductor between the input source and the output "
    "has none; ngspice's solver also fails without saying why on some circuits, such as one with an ideal op-amp at "
    "some gain-bandwidth products, so try another gain-bandwidth product or a real op-amp model";

} // namespace Core

#endif // IMCSIM_POLE_ZERO_H
