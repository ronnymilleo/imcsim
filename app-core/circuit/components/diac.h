/**
 * @file    diac.h
 * @brief   A DIAC, the gate-less trigger diode that breaks over either way and holds until its current falls.
 */

#ifndef IMCSIM_DIAC_H
#define IMCSIM_DIAC_H

#include "circuit/components/component.h"

#include <memory>
#include <string>

namespace Core {

/**
 * @struct  DiacParameters
 * @brief   Datasheet figures of a DIAC, in SI units.
 * @details The device blocks either way until the magnitude of its voltage reaches the breakover voltage, then snaps
 *          back onto the on-state line \f$V = V_0 + R_T I\f$, mirrored about zero, and releases when the magnitude
 *          of its current falls below the holding current. The datasheet snapback \f$\Delta V\f$ is the breakover
 *          voltage less \f$V_0\f$. The defaults are those of a DB3: 32 V breakover and a 5 V snapback.
 */
struct DiacParameters {
    double BreakoverVoltage = 32.0;   ///< \f$V_{BO}\f$: voltage magnitude that fires the device, in Volts.
    double OnThresholdVoltage = 27.0; ///< Threshold \f$V_0\f$ of the on-state line, in Volts.
    double OnResistance = 10.0;       ///< Slope resistance \f$R_T\f$ of the on-state line, in Ohms.
    double OffResistance = 1e9;       ///< Blocking resistance below the breakover, in Ohms.
    double HoldingCurrent = 1e-3;     ///< \f$I_H\f$: current magnitude below which the device releases, in Amperes.

    bool operator==(const DiacParameters &) const = default;
};

/**
 * @class   Diac
 * @brief   A two-terminal DIAC, with MT2 on terminal 1 and MT1 on terminal 2.
 * @details The part has no value; its figures are set one by one. Every run starts blocking. Nodalys simulates it as
 *          the latching device of the TRIAC with its gate tied to MT1, fired by breakover only. ngspice refuses it:
 *          the netlist writes it as the imcsim_diac subcircuit, which the Netlist window shows and exports, but the
 *          TRIAC that a DIAC triggers does not block at mains voltage on ngspice.
 */
class Diac : public Component {
public:
    Diac();
    ~Diac() override = default;

    std::unique_ptr<Component> Clone() const override;

    const DiacParameters &GetParameters() const;
    void SetParameters(const DiacParameters &parameters);

    static bool IsValidParameters(const DiacParameters &parameters);

private:
    DiacParameters m_Parameters;
};

// Netlist
const char *GetDiacSubcircuitName();
std::string FormatDiacSubcircuit();

} // namespace Core

#endif // IMCSIM_DIAC_H
