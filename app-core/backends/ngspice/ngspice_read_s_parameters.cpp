/**
 * @file    ngspice_read_s_parameters.cpp
 * @brief   Reads the scattering matrix of an S-parameter analysis from the current plot of ngspice.
 */

#include "backends/ngspice/ngspice_read_s_parameters.h"

#include "backends/ngspice/ngspice_vectors.h"
#include "circuit/circuit.h"
#include "simulation/simulation_results.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Core {

/**
 * @brief   Reads the scattering matrix of the ports at every frequency of an S-parameter analysis.
 * @param[in] circuit  Circuit that was simulated; its ports, numbered 1 to N, set the size of the matrix.
 * @return  The matrix at every frequency, or the error when a vector is missing or holds something that is not a
 *          number.
 */
std::expected<SParameters, std::string> ReadSParameters(const Circuit &circuit) {
    const std::optional<std::vector<double>> frequencies = ReadRealVector("frequency");
    if (!frequencies) {
        return std::unexpected("ngspice returned no frequency points");
    }
    SParameters result;
    result.Frequencies = *frequencies;
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
            auto values = ReadComplexVectorOfLength(std::format("s_{}_{}", row, column),
                                                    point_count,
                                                    std::format("S({}, {})", row, column));
            if (!values) {
                return std::unexpected(values.error());
            }
            entries.push_back(std::move(*values));
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

} // namespace Core
