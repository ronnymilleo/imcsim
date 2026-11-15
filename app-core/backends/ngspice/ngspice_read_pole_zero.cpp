/**
 * @file    ngspice_read_pole_zero.cpp
 * @brief   Reads the poles and zeros from the plot ngspice leaves.
 */

#include "backends/ngspice/ngspice_read_pole_zero.h"

#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/pole_zero.h"
#include "simulation/simulation_results.h"
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

namespace {

/**
 * @brief   The plot a pole-zero analysis leaves; its name starts with "pz".
 */
std::optional<std::string> FindPoleZeroPlot() {
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with("pz")) {
            return name;
        }
    }
    return std::nullopt;
}

/**
 * @brief   The roots of one kind in the plot, from its vectors "pole(1)", "pole(2)"... or "zero(1)"...
 */
ComplexRoots ReadRoots(const std::string &plot, const std::string_view kind) {
    ComplexRoots roots;
    const std::string prefix = std::format("{}(", kind);
    for (const std::string &name : ListVectors(plot)) {
        if (!name.starts_with(prefix)) {
            continue;
        }
        const pvector_info vector = FindVector(std::format("{}.{}", plot, name));
        if (vector != nullptr) {
            roots.push_back(ComplexValues(*vector).front());
        }
    }
    return roots;
}

} // namespace

/**
 * @brief   Reads the poles and zeros of the transfer function the run found.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node and input source the run used.
 * @return  The poles and zeros, or the error when ngspice left none, which it does without a message when its solver
 *          fails.
 */
std::expected<PoleZero, std::string> ReadPoleZero(const Circuit &circuit, const PoleZeroSettings &settings) {
    const std::optional<std::string> plot = FindPoleZeroPlot();
    if (!plot) {
        return std::unexpected("ngspice returned no pole-zero plot");
    }
    const ComplexRoots poles = ReadRoots(*plot, "pole");
    const ComplexRoots zeros = ReadRoots(*plot, "zero");
    if (poles.empty() && zeros.empty()) {
        return std::unexpected(std::string(NO_POLE_ZERO_RESULT));
    }
    return BuildPoleZero(circuit, settings, poles, zeros);
}

} // namespace Core
