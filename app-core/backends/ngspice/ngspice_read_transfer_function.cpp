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
#include "spice/spice_text.h"
#include <cmath>
#include <expected>
#include <format>
#include <optional>
#include <string>

namespace Core {

/**
 * @brief   Reads the gain and the two impedances of a transfer function analysis.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node and input source the run used.
 * @return  The transfer function or the error when a vector is missing or is not a number.
 */
std::expected<TransferFunction, std::string> ReadTransferFunction(const Circuit &circuit,
                                                                  const TransferFunctionSettings &settings) {
    const std::optional<std::string> plot = FindPlotStartingWith("tf");
    if (!plot) {
        return std::unexpected("ngspice returned no transfer function plot");
    }
    SendCommand(std::format("setplot {}", *plot));
    // The vectors are named after the input source and the output expression, in lower case
    const std::string input_impedance_name = std::format("{}#input_impedance", ToLower(settings.InputSource));
    const std::string output_impedance_name = std::format("output_impedance_at_v({})", settings.OutputNode);
    const auto gain = ReadScalar("transfer_function", "transfer_function");
    const auto input_impedance = ReadScalar(input_impedance_name, input_impedance_name);
    const auto output_impedance = ReadScalar(output_impedance_name, output_impedance_name);
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
    if (!std::isfinite(result.Gain) || !std::isfinite(result.InputImpedance)
        || !std::isfinite(result.OutputImpedance)) {
        return std::unexpected("ngspice returned a transfer function that is not a number");
    }
    return result;
}

} // namespace Core
