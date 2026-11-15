/**
 * @file    ngspice_read_transfer_function.cpp
 * @brief   Reads the result of a transfer function analysis from the plot ngspice leaves.
 */

#include "backends/ngspice/ngspice_read_transfer_function.h"

#include "backends/ngspice/ngspice_library.h"
#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include "simulation/simulation_results.h"
#include <cmath>
#include <expected>
#include <format>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>

namespace Core {

namespace {

/**
 * @brief   The plot a transfer function analysis leaves; its name starts with "tf" and it holds one row.
 */
std::optional<std::string> FindTransferFunctionPlot() {
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with("tf")) {
            return name;
        }
    }
    return std::nullopt;
}

/**
 * @brief   One of the three single-value vectors of a transfer function; ngspice names them after the source and the
 *          output.
 */
std::expected<double, std::string> ReadTransferFunctionValue(const std::string &name) {
    const pvector_info vector = FindVector(name);
    if (vector == nullptr || vector->v_realdata == nullptr) {
        return std::unexpected(std::format("ngspice returned no {}", name));
    }
    return vector->v_realdata[0];
}

} // namespace

/**
 * @brief   Reads the gain and the two impedances of a transfer function analysis.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node and input source the run used.
 * @return  The transfer function or the error when a vector is missing or is not a number.
 */
std::expected<TransferFunction, std::string> ReadTransferFunction(const Circuit &circuit,
                                                                  const TransferFunctionSettings &settings) {
    const std::optional<std::string> plot = FindTransferFunctionPlot();
    if (!plot) {
        return std::unexpected("ngspice returned no transfer function plot");
    }
    SendCommand(std::format("setplot {}", *plot));
    // The vectors are named after the input source and the output expression, in lower case
    const auto gain = ReadTransferFunctionValue("transfer_function");
    const auto input_impedance =
        ReadTransferFunctionValue(std::format("{}#input_impedance", ToLower(settings.InputSource)));
    const auto output_impedance =
        ReadTransferFunctionValue(std::format("output_impedance_at_v({})", settings.OutputNode));
    if (!gain) {
        return std::unexpected(gain.error());
    }
    if (!input_impedance) {
        return std::unexpected(input_impedance.error());
    }
    if (!output_impedance) {
        return std::unexpected(output_impedance.error());
    }
    TransferFunction result;
    result.OutputNode = settings.OutputNode;
    result.InputSource = settings.InputSource;
    result.InputUnit = SourceUnit(*FindTransferFunctionInput(circuit, settings.InputSource));
    result.Gain = *gain;
    result.InputImpedance = *input_impedance;
    result.OutputImpedance = *output_impedance;
    // An infinite value would format as nan or inf, which reads as a broken number; an open path is instead reported
    // around 1e20 ohm, which stays finite and the views turn into an open circuit
    if (!std::isfinite(result.Gain) || !std::isfinite(result.InputImpedance) ||
        !std::isfinite(result.OutputImpedance)) {
        return std::unexpected("ngspice returned a transfer function that is not a number");
    }
    return result;
}

} // namespace Core
