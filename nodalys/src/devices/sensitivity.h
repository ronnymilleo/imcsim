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
    const std::vector<std::complex<double>> &Adjoint; ///< Adjoint of the output in the small-signal system.
    /// Adjoint of the bias weights in the DC system; all zero for a circuit without nonlinear devices.
    const std::vector<std::complex<double>> &BiasAdjoint;

    /**
     * @brief   Reads the DC solution entry for an unknown index, returning zero for ground.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  DC solution value at `unknown`, or `0.0` when `unknown < 0`.
     */
    [[nodiscard]] double DcAt(int unknown) const {
        return unknown < 0 ? 0.0 : DcSolution[static_cast<std::size_t>(unknown)];
    }

    /**
     * @brief   Computes the DC potential difference between two unknowns.
     * @param[in] first   Positive-side MNA unknown index (negative for ground).
     * @param[in] second  Negative-side MNA unknown index (negative for ground).
     * @return  Difference `DcAt(first) - DcAt(second)`.
     */
    [[nodiscard]] double DcAcross(int first, int second) const { return DcAt(first) - DcAt(second); }

    /**
     * @brief   Reads the AC phasor entry for an unknown index, returning zero for ground.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  Complex AC phasor at `unknown`, or `0` when `unknown < 0`.
     */
    [[nodiscard]] std::complex<double> AcAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : AcSolution[static_cast<std::size_t>(unknown)];
    }

    /**
     * @brief   Computes the AC phasor difference between two unknowns.
     * @param[in] first   Positive-side MNA unknown index (negative for ground).
     * @param[in] second  Negative-side MNA unknown index (negative for ground).
     * @return  Complex difference `AcAt(first) - AcAt(second)`.
     */
    [[nodiscard]] std::complex<double> AcAcross(int first, int second) const { return AcAt(first) - AcAt(second); }

    /**
     * @brief   Reads the small-signal output adjoint entry for an unknown index, returning zero for ground.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  Complex adjoint entry at `unknown`, or `0` when `unknown < 0`.
     */
    [[nodiscard]] std::complex<double> AdjointAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : Adjoint[static_cast<std::size_t>(unknown)];
    }

    /**
     * @brief   Computes the small-signal output adjoint difference between two unknowns.
     * @param[in] first   Positive-side MNA unknown index (negative for ground).
     * @param[in] second  Negative-side MNA unknown index (negative for ground).
     * @return  Complex difference `AdjointAt(first) - AdjointAt(second)`.
     */
    [[nodiscard]] std::complex<double> AdjointAcross(int first, int second) const {
        return AdjointAt(first) - AdjointAt(second);
    }

    /**
     * @brief   Reads the DC bias-shift adjoint entry for an unknown index, returning zero for ground.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  Complex bias adjoint entry at `unknown`, or `0` when `unknown < 0`.
     */
    [[nodiscard]] std::complex<double> BiasAdjointAt(int unknown) const {
        return unknown < 0 ? std::complex<double>{} : BiasAdjoint[static_cast<std::size_t>(unknown)];
    }

    /**
     * @brief   Computes the DC bias-shift adjoint difference between two unknowns.
     * @param[in] first   Positive-side MNA unknown index (negative for ground).
     * @param[in] second  Negative-side MNA unknown index (negative for ground).
     * @return  Complex difference `BiasAdjointAt(first) - BiasAdjointAt(second)`.
     */
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

    /**
     * @brief   Reads the DC solution value for an unknown index, returning zero for ground or out-of-range indices.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  DC solution entry at `unknown`, or `0.0` when out of bounds.
     */
    [[nodiscard]] double VoltageAt(int unknown) const {
        return (unknown >= 0 && static_cast<std::size_t>(unknown) < DcSolution.size())
                 ? DcSolution[static_cast<std::size_t>(unknown)]
                 : 0.0;
    }

    /**
     * @brief   Reads the DC output adjoint value for an unknown index, returning zero for ground or out-of-range
     *          indices.
     * @param[in] unknown  MNA unknown index, or negative for ground.
     * @return  DC adjoint entry at `unknown`, or `0.0` when out of bounds.
     */
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
    /**
     * @brief   Describes the varied parameter and current nominal value of this device.
     * @return  Parameter metadata, or empty when sensitivity does not apply.
     */
    [[nodiscard]] virtual std::optional<SensitivityDescriptor> DescribeSensitivity() const = 0;

    /**
     * @brief   Computes the DC sensitivity \f$\partial V_{\text{out}} / \partial p\f$ around the operating point.
     * @param[in] point  DC operating point and output adjoint vectors.
     * @return  Derivative of the output with respect to this device's parameter.
     */
    [[nodiscard]] virtual double DcSensitivityDerivative(const DcAdjointView &point) const = 0;

    /**
     * @brief   Computes the AC sensitivity \f$\partial V_{\text{out}} / \partial p\f$ at angular frequency
     *          `point.Omega`.
     * @param[in] point  Small-signal solution, DC operating point, direct adjoint, and bias adjoint vectors.
     * @return  Complex derivative of the AC output phasor with respect to this device's parameter.
     */
    [[nodiscard]] virtual std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const = 0;

protected:
    /**
     * @brief   Constructs the sensitivity capability base.
     */
    SensitiveDevice() = default;

    /**
     * @brief   Copies the sensitivity capability base.
     */
    SensitiveDevice(const SensitiveDevice &) = default;

    /**
     * @brief   Assigns the sensitivity capability base.
     * @return  Reference to this instance.
     */
    SensitiveDevice &operator=(const SensitiveDevice &) = default;

    /**
     * @brief   Destroys the sensitivity capability base.
     */
    ~SensitiveDevice() = default;
};

} // namespace Nodalys

#endif // NODALYS_DEVICES_SENSITIVITY_H
