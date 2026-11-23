/**
 * @file    ac_sens_bias.h
 * @brief   How the small-signal matrix of an AC sensitivity analysis moves with the DC operating point.
 * @details A nonlinear device linearizes at the operating point, so its conductances and capacitances depend on the DC
 *          voltages, which move when any element value changes. The change of the AC response through that path, the
 *          bias term, needs the derivative of the stamps with respect to every DC unknown the device reads.
 */

#ifndef NODALYS_AC_SENS_BIAS_H
#define NODALYS_AC_SENS_BIAS_H

#include "compiler/compiled_circuit.h"
#include <complex>
#include <vector>

namespace Nodalys {

/**
 * @struct  JacobianEntry
 * @brief   Derivative of one entry of the small-signal matrix with respect to one DC unknown.
 */
struct JacobianEntry {
    int Row = 0;
    int Column = 0;
    /// Derivative of the conductance part, in siemens per volt or per ampere.
    double Conductance = 0.0;
    /// Derivative of the coefficient that multiplies \f$j\omega\f$, in farads per volt or per ampere.
    double Reactive = 0.0;
};

/**
 * @struct  JacobianDerivative
 * @brief   The derivative of the stamps of the nonlinear devices with respect to one DC unknown.
 */
struct JacobianDerivative {
    int Unknown = 0;
    std::vector<JacobianEntry> Entries;
};

// Derivatives of the stamps
std::vector<JacobianDerivative> FindJacobianDerivatives(const CompiledCircuit &compiled,
                                                        const std::vector<double> &x_dc);

// Bias weights
std::vector<std::complex<double>> FindBiasWeights(const std::vector<JacobianDerivative> &derivatives,
                                                  std::size_t size,
                                                  const std::vector<std::complex<double>> &adjoint,
                                                  const std::vector<std::complex<double>> &x_ac,
                                                  double omega);

} // namespace Nodalys

#endif // NODALYS_AC_SENS_BIAS_H
