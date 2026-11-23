/**
 * @file    ac_sens_elements.h
 * @brief   The elements an AC sensitivity analysis varies, and the derivative of the output with respect to each.
 * @details With the small-signal solution \f$x_{ac}\f$, the adjoint \f$\lambda\f$ of the output and the bias adjoint
 * \f$\mu\f$, the change of the output with a value \f$p\f$ is
 *          \f$\partial V / \partial p = -\lambda^T (\partial Y / \partial p) x_{ac} - \mu^T g_p\f$, where \f$Y\f$ is
 * the small-signal matrix and \f$g_p\f$ the derivative of the DC equations with respect to \f$p\f$.
 */

#ifndef NODALYS_AC_SENS_ELEMENTS_H
#define NODALYS_AC_SENS_ELEMENTS_H

#include "compiler/compiled_circuit.h"
#include "devices/sensitivity.h"
#include "nodalys/results.h"
#include <complex>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  AcSensitivityElement
 * @brief   One element whose value the analysis varies.
 */
struct AcSensitivityElement {
    std::string Name;
    SensitivityParameter Parameter = SensitivityParameter::RESISTANCE;
    const SensitiveDevice *Instance = nullptr;
    double Value = 0.0;
};

/**
 * @struct  AcSensitivityPoint
 * @brief   What the derivatives of one frequency are worked out from.
 */
struct AcSensitivityPoint {
    double Omega = 0.0;
    const std::vector<double> &DcSolution;
    const std::vector<std::complex<double>> &AcSolution;
    /// Adjoint of the output in the small-signal system.
    const std::vector<std::complex<double>> &Adjoint;
    /// Adjoint of the bias weights in the DC system; all zero for a circuit without nonlinear devices.
    const std::vector<std::complex<double>> &BiasAdjoint;
};

// Elements
std::vector<AcSensitivityElement> FindSensitivityElements(const CompiledCircuit &compiled);

// Derivatives
std::complex<double> FindOutputDerivative(const AcSensitivityElement &element, const AcSensitivityPoint &point);

} // namespace Nodalys

#endif // NODALYS_AC_SENS_ELEMENTS_H
