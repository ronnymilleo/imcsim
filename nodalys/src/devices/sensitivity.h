/**
 * @file    sensitivity.h
 * @brief   Segregated polymorphic capability interface for devices that expose analytical DC and AC sensitivities.
 */

#ifndef NODALYS_DEVICES_SENSITIVITY_H
#define NODALYS_DEVICES_SENSITIVITY_H

#include "nodalys/results.h"
#include <complex>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @struct  AcAdjointView
 * @brief   Small-signal solution, DC operating point, and adjoints at one frequency for AC sensitivity derivatives.
 */
struct AcAdjointView {
    double Omega = 0.0;
    const std::vector<double> &DcSolution;
    const std::vector<std::complex<double>> &AcSolution;
    /// Adjoint of the output in the small-signal system.
    const std::vector<std::complex<double>> &Adjoint;
    /// Adjoint of the bias weights in the DC system; all zero for a circuit without nonlinear devices.
    const std::vector<std::complex<double>> &BiasAdjoint;

    [[nodiscard]] double DcAt(int unknown) const {
        return unknown < 0 ? 0.0 : DcSolution[static_cast<std::size_t>(unknown)];
    }

    [[nodiscard]] double DcAcross(int first, int second) const { return DcAt(first) - DcAt(second); }

    [[nodiscard]] std::complex<double> AcAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : AcSolution[static_cast<std::size_t>(unknown)];
    }

    [[nodiscard]] std::complex<double> AcAcross(int first, int second) const { return AcAt(first) - AcAt(second); }

    [[nodiscard]] std::complex<double> AdjointAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : Adjoint[static_cast<std::size_t>(unknown)];
    }

    [[nodiscard]] std::complex<double> AdjointAcross(int first, int second) const {
        return AdjointAt(first) - AdjointAt(second);
    }

    [[nodiscard]] std::complex<double> BiasAdjointAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : BiasAdjoint[static_cast<std::size_t>(unknown)];
    }

    [[nodiscard]] std::complex<double> BiasAdjointAcross(int first, int second) const {
        return BiasAdjointAt(first) - BiasAdjointAt(second);
    }
};

/**
 * @struct  DcAdjointView
 * @brief   Operating point and DC output adjoint from which each sensitive device computes its DC sensitivity.
 */
struct DcAdjointView {
    const std::vector<double> &DcSolution;
    const std::vector<double> &Adjoint;

    [[nodiscard]] double VoltageAt(int unknown) const {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < DcSolution.size())
                 ? DcSolution[static_cast<std::size_t>(unknown)]
                 : 0.0;
    }

    [[nodiscard]] double AdjointAt(int unknown) const {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < Adjoint.size())
                 ? Adjoint[static_cast<std::size_t>(unknown)]
                 : 0.0;
    }
};

/**
 * @struct  SensitivityDescriptor
 * @brief   Metadata describing a device's varied parameter for DC and AC sensitivity analyses.
 */
struct SensitivityDescriptor {
    SensitivityParameter Parameter = SensitivityParameter::RESISTANCE;
    double Value = 0.0;
    /// Prefix prepended to subcircuit element names in DC sensitivity vectors (e.g. "r.", "v."), empty for gains.
    std::string_view SubcircuitPrefix;
    /// True for controlled sources whose DC sensitivity vector key uses the "_gain" suffix.
    bool IsGainParameter = false;
};

/**
 * @class   SensitiveDevice
 * @brief   Optional capability interface for devices that report analytical first-order DC and AC sensitivities.
 */
class SensitiveDevice {
public:
    /// Describes the varied parameter and current nominal value, or empty if sensitivity does not apply.
    [[nodiscard]] virtual std::optional<SensitivityDescriptor> DescribeSensitivity() const = 0;

    /// Returns dV_out/dp around the DC operating point using the adjoint method.
    [[nodiscard]] virtual double DcSensitivityDerivative(const DcAdjointView &point) const = 0;

    /// Returns dV_out/dp at angular frequency point.Omega using direct and bias adjoints.
    [[nodiscard]] virtual std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const = 0;

protected:
    SensitiveDevice() = default;
    SensitiveDevice(const SensitiveDevice &) = default;
    SensitiveDevice &operator=(const SensitiveDevice &) = default;
    ~SensitiveDevice() = default;
};

} // namespace Nodalys

#endif // NODALYS_DEVICES_SENSITIVITY_H
