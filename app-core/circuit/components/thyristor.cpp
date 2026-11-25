/**
 * @file    thyristor.cpp
 * @brief   A thyristor (SCR) that fires on gate current, latches on, and releases below its holding current.
 */

#include "circuit/components/thyristor.h"

#include <format>
#include <memory>

namespace Core {

/**
 * @brief   Creates a thyristor that fires at 1 mA of gate current, holds on above 5 mA of anode current and drops
 *          0.9 V plus 10 mOhm on the on-state line.
 */
Thyristor::Thyristor() : Component(ComponentType::THYRISTOR, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Thyristor::Clone() const {
    return std::make_unique<Thyristor>(*this);
}

/**
 * @brief   Returns the parameters of the thyristor.
 * @return  The on-state line, the holding and trigger currents and the resistances.
 */
const ThyristorParameters &Thyristor::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the parameters of the thyristor.
 * @param[in] parameters  New parameters; check them with IsValidParameters() first.
 */
void Thyristor::SetParameters(const ThyristorParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of thyristor parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when every resistance is positive, the on-state line lies inside the blocking one and every current
 *          and voltage is non-negative, with a breakover of zero meaning disabled.
 */
bool Thyristor::IsValidParameters(const ThyristorParameters &parameters) {
    return parameters.OnResistance > 0.0 && parameters.OnResistance < parameters.OffResistance
        && parameters.OnThresholdVoltage >= 0.0 && parameters.HoldingCurrent > 0.0
        && parameters.GateTriggerCurrent > 0.0 && parameters.GateTriggerVoltage >= 0.0
        && parameters.GateResistance > 0.0 && parameters.BreakoverVoltage >= 0.0;
}

const char *GetThyristorSubcircuitName() {
    return "imcsim_scr";
}

/**
 * @brief   Writes the two-transistor subcircuit that ngspice runs for a thyristor.
 * @return  The ".subckt" block, newline included.
 * @details A PNP and an NPN with the collector of each into the base of the other: the pair is the PNPN structure,
 *          and it latches by regeneration once the gate drives the base of the NPN, releasing when the anode current
 *          falls below what keeps the loop gain above one. The gate drives the base through a diode of a vanishing
 *          drop in series with the gate resistance: the diode lets the drive in without the pair's base current
 *          leaking back out of the gate once latched, as the reverse-blocking junction of Nodalys does not conduct.
 *          A second vanishing diode sits in series with the anode, so the drive of a reverse-biased thyristor
 *          cannot find a path through the pair and out of its anode, which the junctions of a real device block.
 *          The gate resistance and the slope resistance of the on-state line are parameters of the instance; the
 *          holding and trigger currents and the on-state threshold emerge from the transistors and their
 *          base-emitter shunts, close to the figures of Nodalys, which takes them as parameters. The junction
 *          capacitances soften the switching, so the pair converges.
 * @note    There is no forward breakover: the pair blocks whatever forward voltage the circuit puts on it, as a
 *          thyristor driven correctly does.
 */
std::string FormatThyristorSubcircuit() {
    return ".subckt imcsim_scr a k g params: RG=100 RT=0.01\n"
           "Dt a nrt IMCSIM_SCR_DT\n"
           "Rt nrt na {RT}\n"
           "Q1 n2 n1 na IMCSIM_SCR_QP\n"
           "Q2 n1 n2 k IMCSIM_SCR_QN\n"
           "Dg g ng IMCSIM_SCR_DG\n"
           "Rg ng n2 {RG}\n"
           "R1 n1 na 100k\n"
           "R2 n2 k 100k\n"
           ".model IMCSIM_SCR_QP PNP(IS=1e-14 BF=100 CJE=20p CJC=20p)\n"
           ".model IMCSIM_SCR_QN NPN(IS=1e-14 BF=100 CJE=20p CJC=20p)\n"
           ".model IMCSIM_SCR_DG D(IS=1e-12 N=0.001)\n"
           ".model IMCSIM_SCR_DT D(IS=1e-12 N=0.001)\n"
           ".ends imcsim_scr\n";
}

} // namespace Core
