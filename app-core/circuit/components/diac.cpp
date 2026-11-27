/**
 * @file    diac.cpp
 * @brief   A DIAC, the gate-less trigger diode that breaks over either way and holds until its current falls.
 */

#include "circuit/components/diac.h"

#include <memory>

namespace Core {

/**
 * @brief   Creates a DIAC with the figures of a DB3: it breaks over at 32 V, snaps back to a 27 V on-state line of
 *          10 Ohm and releases below 1 mA.
 */
Diac::Diac() : Component(ComponentType::DIAC, 0.0) {
}

/**
 * @brief   Copies the component with its name and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Diac::Clone() const {
    return std::make_unique<Diac>(*this);
}

/**
 * @brief   Returns the figures of the DIAC.
 * @return  The breakover voltage, the on-state line, the blocking resistance and the holding current.
 */
const DiacParameters &Diac::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the figures of the DIAC.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void Diac::SetParameters(const DiacParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of DIAC figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the resistances are positive with the on-state one below the blocking one, the holding current
 *          is positive, and the on-state line starts below the breakover, so the device snaps back when it fires and
 *          its release lies below the breakover.
 */
bool Diac::IsValidParameters(const DiacParameters &parameters) {
    const double release_voltage = parameters.OnThresholdVoltage + parameters.OnResistance * parameters.HoldingCurrent;
    return parameters.OnResistance > 0.0 && parameters.OnResistance < parameters.OffResistance
        && parameters.HoldingCurrent > 0.0 && parameters.OnThresholdVoltage >= 0.0
        && release_voltage < parameters.BreakoverVoltage;
}

/**
 * @brief   Returns the SPICE subcircuit name written for a DIAC in exported netlists.
 * @return  Null-terminated subcircuit name `"imcsim_diac"`.
 */
const char *GetDiacSubcircuitName() {
    return "imcsim_diac";
}

/**
 * @brief   Writes the subcircuit that ngspice runs for a DIAC.
 * @return  The ".subckt" block, newline included.
 * @details A behavioral source reads the magnitude of the voltage across the device, and a switch with hysteresis on
 *          it holds the state: it turns on at the breakover voltage and off at \f$V_0 + R_T I_H\f$, the voltage of the
 *          on-state line at the holding current, so it releases when the current falls below it. The state, a node at
 *          1 V while on and at almost nothing while off, scales a behavioral current that follows the on-state line
 *          past each threshold and carries nothing between them, the same continuous line as the device of Nodalys.
 *          A 1 us time constant on the state node lets the current rise over a few steps instead of within one,
 *          which ngspice otherwise cannot follow at the instant the switch turns. Every figure is a parameter of the
 *          instance.
 * @note    imcsim does not run it: the ngspice backend refuses a DIAC, so the subcircuit is only in the netlist the
 *          Netlist window shows and exports. A lone DIAC charging a capacitor breaks over and releases as on
 *          Nodalys, but in a dimmer the switch can stop ngspice with a time step too small.
 */
std::string FormatDiacSubcircuit() {
    return ".subckt imcsim_diac mt2 mt1 params: VBO=32 V0=27 RT=10 IH=1m ROFF=1G\n"
           "Roff mt2 mt1 {ROFF}\n"
           "Bsense ns 0 V=abs(V(mt2,mt1))\n"
           "Vst nsrc 0 DC 1\n"
           "Sst nsrc nst ns 0 IMCSIM_DIAC_SW\n"
           "Rst nst 0 1k\n"
           "Cst nst 0 1n\n"
           "Bon mt2 mt1 I=V(nst)*(max(V(mt2,mt1)-{V0},0)+min(V(mt2,mt1)+{V0},0))/{RT}\n"
           ".model IMCSIM_DIAC_SW SW(VT={(VBO+V0+RT*IH)/2} VH={(VBO-V0-RT*IH)/2} RON=1 ROFF=1G)\n"
           ".ends imcsim_diac\n";
}

} // namespace Core
