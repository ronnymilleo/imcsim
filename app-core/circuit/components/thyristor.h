/**
 * @file    thyristor.h
 * @brief   A thyristor (SCR) or TRIAC that fires on gate current, latches on, and releases below its holding current.
 */

#ifndef IMCSIM_THYRISTOR_H
#define IMCSIM_THYRISTOR_H

#include "circuit/components/component.h"
#include <memory>
#include <string>
#include <string_view>

namespace Core {

/**
 * @struct  ThyristorParameters
 * @brief   Datasheet figures of a thyristor (SCR) or TRIAC, in SI units.
 * @details The on-state is the datasheet line \f$V_{TM} = V_0 + R_T I_T\f$. Nodalys takes every figure as a parameter;
 *          the ngspice subcircuit follows the gate and slope resistances exactly, while its holding and trigger
 *          currents and its on-state threshold emerge from the two-transistor pair, close to these defaults. The
 *          breakover, the critical dv/dt and the turn-off time run on Nodalys only; the ngspice pair has none of
 *          them. `IsBidirectional` is set by the part, not by the editor: a TRIAC conducts both ways and fires in
 *          all four gate quadrants, an SCR one way only.
 */
struct ThyristorParameters {
    double OnResistance = 0.01;       ///< Slope resistance \f$R_T\f$ of the on-state line, in Ohms.
    double OnThresholdVoltage = 0.8;  ///< Threshold \f$V_0\f$ of the on-state line, in Volts.
    double OffResistance = 1e9;       ///< Blocking resistance of the off-state, in Ohms.
    double HoldingCurrent = 5e-3;     ///< \f$I_H\f$: main current below which the latch releases, in Amperes.
    double GateTriggerCurrent = 1e-3; ///< \f$I_{GT}\f$: gate current that fires the device, in Amperes.
    double GateTriggerVoltage = 0.8;  ///< \f$V_{GT}\f$: gate junction threshold, in Volts.
    double GateResistance = 100.0;    ///< \f$R_G\f$: series gate resistance, in Ohms.
    double BreakoverVoltage = 0.0;    ///< \f$V_{BO}\f$: breakover voltage; zero disables it, in Volts.
    /// Critical \f$dv/dt\f$: a faster rise of the main-terminal voltage fires a blocking device; zero disables it,
    /// in Volts per second (Nodalys only).
    double CriticalDvDt = 0.0;
    /// \f$t_q\f$: recovery time after a release, during which a re-applied forward voltage re-fires the device;
    /// zero disables it, in seconds (Nodalys only).
    double TurnOffTime = 0.0;

    // Datasheet ratings, which no engine reads and the results are checked against; zero leaves each unchecked
    /// \f$V_{DRM}\f$: repetitive peak off-state voltage, taken as \f$V_{RRM}\f$ too, in Volts.
    double MaxRepetitivePeakVoltage = 0.0;
    /// On-state current: \f$I_{T(AV)}\f$, the average forward current of an SCR, or \f$I_{T(RMS)}\f$ of a TRIAC, whose
    /// average is near zero, in Amperes.
    double MaxOnStateCurrent = 0.0;
    double MaxSurgeCurrent = 0.0; ///< \f$I_{TSM}\f$: non-repetitive peak surge current, in Amperes.
    /// \f$I^2t\f$ for fusing, over the 10 ms of a 50 Hz half-cycle, in square Amperes times seconds.
    double MaxI2t = 0.0;

    /// True for a TRIAC, which conducts both ways; the part sets it from its type and the editor leaves it alone.
    bool IsBidirectional = false;

    bool operator==(const ThyristorParameters &) const = default;
};

/**
 * @class   Thyristor
 * @brief   A three-terminal thyristor: an SCR with anode on terminal 1, cathode on terminal 2 and gate on terminal 3,
 *          or a TRIAC with MT2 on terminal 1, MT1 on terminal 2 and gate on terminal 3.
 * @details The same class serves both, the component type telling which; a TRIAC sets `IsBidirectional`. The part has
 *          no value; its parameters are set one by one. Every run starts blocking, and the circuit fires it. Nodalys
 *          simulates it as one latching device; ngspice runs the imcsim_scr two-transistor subcircuit for an SCR and
 *          the imcsim_triac subcircuit, two anti-parallel pairs with a four-quadrant gate stage, for a TRIAC. The gate
 *          drive must be referenced to the cathode (MT1), as the gate current returns through it.
 */
class Thyristor : public Component {
public:
    explicit Thyristor(ComponentType type = ComponentType::THYRISTOR);
    ~Thyristor() override = default;

    std::unique_ptr<Component> Clone() const override;

    const ThyristorParameters &GetParameters() const;
    void SetParameters(const ThyristorParameters &parameters);
    static bool IsValidParameters(const ThyristorParameters &parameters);

private:
    ThyristorParameters m_Parameters;
};

// Netlist
const char *GetThyristorSubcircuitName();
std::string FormatThyristorSubcircuit();
const char *GetTriacSubcircuitName();
std::string FormatTriacSubcircuit();

} // namespace Core

#endif // IMCSIM_THYRISTOR_H