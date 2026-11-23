/**
 * @file    port_matrices.h
 * @brief   Impedance and admittance matrices of the ports, worked out from the scattering matrix.
 */

#ifndef NODALYS_PORT_MATRICES_H
#define NODALYS_PORT_MATRICES_H

#include "nodalys/results.h"
#include <optional>
#include <vector>

namespace Nodalys {

// Conversions
[[nodiscard]] std::optional<PortMatrix> ComputeImpedanceMatrix(const PortMatrix &scattering,
                                                               const std::vector<double> &reference_ohms);
[[nodiscard]] std::optional<PortMatrix> ComputeAdmittanceMatrix(const PortMatrix &scattering,
                                                                const std::vector<double> &reference_ohms);

} // namespace Nodalys

#endif // NODALYS_PORT_MATRICES_H
