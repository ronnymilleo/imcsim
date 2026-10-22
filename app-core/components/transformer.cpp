/**
 * @file    transformer.cpp
 * @brief   A transformer in the simulation model: two coupled inductors.
 */

#include "transformer.h"

namespace Core {

namespace {

constexpr double DEFAULT_PRIMARY_INDUCTANCE = 100e-3;

} // namespace

/**
 * @brief   Creates a 1:1 transformer with a primary of 100 mH, a coupling of 0.999 and windings of 0.1 Ohm.
 */
Transformer::Transformer() : Component(ComponentType::TRANSFORMER, DEFAULT_PRIMARY_INDUCTANCE) {
}

/**
 * @brief   Returns the figures of the transformer besides the primary inductance.
 * @return  Turns ratio, coupling and winding resistance.
 */
const TransformerParameters &Transformer::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the figures of the transformer besides the primary inductance.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void Transformer::SetParameters(const TransformerParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of transformer figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True for a positive turns ratio and winding resistance and a coupling above 0 and below 1, since SPICE
 *          rejects a coupling of 1.
 */
bool Transformer::IsValidParameters(const TransformerParameters &parameters) {
    return parameters.TurnsRatio > 0.0 && parameters.WindingResistance > 0.0 && parameters.Coupling > 0.0 &&
           parameters.Coupling < 1.0;
}

/**
 * @brief   Works out the inductance of the secondary.
 * @return  The primary inductance over the square of the turns ratio.
 */
double Transformer::GetSecondaryInductance() const {
    return m_Value / (m_Parameters.TurnsRatio * m_Parameters.TurnsRatio);
}

} // namespace Core
