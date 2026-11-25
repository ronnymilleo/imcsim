/**
 * @file    transmission_line.cpp
 * @brief   A uniform two-port TEM transmission line in the simulation model, lossless or with skin and dielectric loss.
 */

#include "circuit/components/transmission_line.h"

#include <cmath>
#include <memory>

namespace Core {

/**
 * @brief   Tells whether any conductor or dielectric loss figure is set above zero.
 * @return  True when the line has DC resistance, skin-effect resistance, DC conductance, or dielectric loss tangent.
 */
bool TransmissionLineParameters::IsLossy() const {
    return DcResistance > 0.0 || SkinResistance > 0.0 || DcConductance > 0.0 || LossTangent > 0.0;
}

/**
 * @brief   Creates a lossless 50 Ohm transmission line with a propagation delay of 1 ns.
 */
TransmissionLine::TransmissionLine() : Component(ComponentType::TRANSMISSION_LINE, DEFAULT_TRANSMISSION_LINE_OHMS) {
}

/**
 * @brief   Copies the transmission line with its name, characteristic impedance and parameters.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> TransmissionLine::Clone() const {
    return std::make_unique<TransmissionLine>(*this);
}

/**
 * @brief   Returns the delay and loss figures of the line besides its characteristic impedance.
 * @return  Delay, conductor resistances, dielectric conductance/loss tangent, and reference frequency.
 */
const TransmissionLineParameters &TransmissionLine::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the delay and loss figures of the line.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void TransmissionLine::SetParameters(const TransmissionLineParameters &parameters) {
    if (IsValidParameters(parameters)) {
        m_Parameters = parameters;
    }
}

/**
 * @brief   Checks whether a set of transmission line figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True for a positive finite delay and reference frequency, and non-negative finite loss figures.
 */
bool TransmissionLine::IsValidParameters(const TransmissionLineParameters &parameters) {
    return std::isfinite(parameters.Delay) && parameters.Delay > 0.0 && std::isfinite(parameters.DcResistance)
        && parameters.DcResistance >= 0.0 && std::isfinite(parameters.SkinResistance)
        && parameters.SkinResistance >= 0.0 && std::isfinite(parameters.DcConductance)
        && parameters.DcConductance >= 0.0 && std::isfinite(parameters.LossTangent) && parameters.LossTangent >= 0.0
        && std::isfinite(parameters.ReferenceFrequency) && parameters.ReferenceFrequency > 0.0;
}

/**
 * @brief   Works out the lowest frequency at which the line spans a quarter wavelength (\f$f_{\lambda/4} = 1 / (4
 * T_d)\f$).
 * @return  Quarter-wave frequency in Hertz.
 */
double TransmissionLine::GetQuarterWaveFrequency() const {
    return 0.25 / m_Parameters.Delay;
}

} // namespace Core
