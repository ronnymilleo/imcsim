/**
 * @file    ngspice_readers_small_signal.cpp
 * @brief   Reads the results of the transfer function, the poles and zeros, the S-parameters and the sensitivity from
 * the plots of ngspice.
 * @details Defines part of what ngspice_readers.h declares; the readers are split by kind of analysis over
 *          ngspice_vectors.h.
 */

#include "backends/ngspice/ngspice_readers.h"

#include "backends/ngspice/ngspice_library.h"
#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/components/source.h"
#include "simulation/circuit_checks.h"
#include "simulation/pole_zero.h"
#include "simulation/ratings.h"
#include "simulation/sensitivity.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <format>
#include <ngspice/sharedspice.h>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

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

/**
 * @brief   The plot a sensitivity analysis leaves; its name starts with "sens".
 */
std::optional<std::string> FindSensitivityPlot() {
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with("sens")) {
            return name;
        }
    }
    return std::nullopt;
}

/**
 * @brief   The first value of every vector of the plot that has a finite one, by name.
 * @details ngspice also saves the parameters of each model under "device:parameter", which are left out, and reports
 *          some as nan when the model does not use them.
 */
SensitivityVectors ReadSensitivityVectors(const std::string &plot) {
    SensitivityVectors values;
    for (const std::string &name : ListVectors(plot)) {
        if (name.contains(':')) {
            continue;
        }
        const pvector_info vector = FindVector(name);
        if (vector == nullptr) {
            continue;
        }
        const double value = RealPart(*vector).front();
        if (std::isfinite(value)) {
            values[name] = value;
        }
    }
    return values;
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

/**
 * @brief   Reads the scattering matrix of the ports at every frequency of an S-parameter analysis.
 * @param[in] circuit  Circuit that was simulated; its ports, numbered 1 to N, set the size of the matrix.
 * @return  The matrix at every frequency, or the error when a vector is missing or holds something that is not a
 *          number.
 */
std::expected<SParameters, std::string> ReadSParameters(const Circuit &circuit) {
    const pvector_info frequency = FindVector("frequency");
    if (frequency == nullptr) {
        return std::unexpected("ngspice returned no frequency points");
    }
    SParameters result;
    result.Frequencies = RealPart(*frequency);
    const std::size_t point_count = result.Frequencies.size();
    std::vector<CircuitPort> ports = circuit.GetPorts();
    std::ranges::sort(ports, {}, &CircuitPort::Number);
    for (const CircuitPort &port : ports) {
        result.ReferenceOhms.push_back(port.ReferenceOhms);
    }
    const int port_count = result.GetPortCount();
    std::vector<std::vector<std::complex<double>>> entries;
    for (int row = 1; row <= port_count; ++row) {
        for (int column = 1; column <= port_count; ++column) {
            const pvector_info vector = FindVector(std::format("s_{}_{}", row, column));
            if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != point_count) {
                return std::unexpected(std::format("ngspice returned no S({}, {})", row, column));
            }
            entries.push_back(ComplexValues(*vector));
        }
    }
    for (std::size_t point = 0; point < point_count; ++point) {
        for (const std::vector<std::complex<double>> &entry : entries) {
            if (!std::isfinite(entry[point].real()) || !std::isfinite(entry[point].imag())) {
                return std::unexpected("ngspice returned S-parameters that are not numbers");
            }
            result.Values.push_back(entry[point]);
        }
    }
    return result;
}

/**
 * @brief   Reads the sensitivity of every part to the voltage of the output node.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] settings  Output node the run used.
 * @return  The sensitivity of every part with a value, or the error when ngspice left no sensitivity plot.
 */
std::expected<Sensitivity, std::string> ReadSensitivity(const Circuit &circuit, const SensitivitySettings &settings) {
    const std::optional<std::string> plot = FindSensitivityPlot();
    if (!plot) {
        return std::unexpected("ngspice returned no sensitivity plot");
    }
    SendCommand(std::format("setplot {}", *plot));
    return BuildSensitivity(circuit, settings.OutputNode, ReadSensitivityVectors(*plot));
}

} // namespace Core
