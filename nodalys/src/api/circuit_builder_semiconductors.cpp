/**
 * @file    circuit_builder_semiconductors.cpp
 * @brief   Semiconductor device builder methods of CircuitBuilder.
 */

#include "nodalys/circuit_builder.h"

#include "api/circuit_builder_impl.h"
#include "compiler/circuit_description.h"
#include <limits>
#include <utility>

namespace Nodalys {

/**
 * @brief   Adds a semiconductor diode (or Zener/LED) with SPICE Shockley junction parameters.
 * @param[in] name     Element designator.
 * @param[in] anode    Anode node.
 * @param[in] cathode  Cathode node.
 * @param[in] params   Diode model parameters.
 * @param[in] area     Relative device area scale factor.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddDiode(std::string name,
                                         const NodeId anode,
                                         const NodeId cathode,
                                         const DiodeParameters &params,
                                         const double area) {
    m_Impl->Description.TrackNodes({anode, cathode});
    m_Impl->Description.Elements.push_back(
        DiodeSpec{.Name = std::move(name), .Anode = anode, .Cathode = cathode, .Params = params, .Area = area});
    return *this;
}

/**
 * @brief   Adds an NPN or PNP bipolar junction transistor using the Gummel-Poon model.
 * @param[in] name       Element designator.
 * @param[in] collector  Collector node.
 * @param[in] base       Base node.
 * @param[in] emitter    Emitter node.
 * @param[in] is_npn     True for NPN, false for PNP.
 * @param[in] params     BJT model parameters.
 * @param[in] area       Relative device area scale factor.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddBjt(std::string name,
                                       const NodeId collector,
                                       const NodeId base,
                                       const NodeId emitter,
                                       const bool is_npn,
                                       const BjtParameters &params,
                                       const double area) {
    m_Impl->Description.TrackNodes({collector, base, emitter});
    m_Impl->Description.Elements.push_back(BjtSpec{
        .Name = std::move(name),
        .Collector = collector,
        .Base = base,
        .Emitter = emitter,
        .IsNpn = is_npn,
        .Params = params,
        .Area = area,
    });
    return *this;
}

/**
 * @brief   Adds an NMOS or PMOS field-effect transistor using the SPICE level-1 Shichman-Hodges model.
 * @param[in] name     Element designator.
 * @param[in] drain    Drain node.
 * @param[in] gate     Gate node.
 * @param[in] source   Source node.
 * @param[in] is_nmos  True for NMOS, false for PMOS.
 * @param[in] params   MOSFET model parameters.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddMosfet(std::string name,
                                          const NodeId drain,
                                          const NodeId gate,
                                          const NodeId source,
                                          const bool is_nmos,
                                          const MosfetParameters &params) {
    m_Impl->Description.TrackNodes({drain, gate, source});
    m_Impl->Description.Elements.push_back(MosfetSpec{
        .Name = std::move(name),
        .Drain = drain,
        .Gate = gate,
        .Source = source,
        .IsNmos = is_nmos,
        .Params = params,
    });
    return *this;
}

/**
 * @brief   Adds a latching thyristor (SCR) or TRIAC fired by gate current and held on by its main current.
 * @param[in] name     Element designator.
 * @param[in] anode    Anode (MT2) node.
 * @param[in] cathode  Cathode (MT1) node.
 * @param[in] gate     Gate node.
 * @param[in] params   Thyristor model parameters; `IsBidirectional` makes it a TRIAC.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddThyristor(std::string name,
                                             const NodeId anode,
                                             const NodeId cathode,
                                             const NodeId gate,
                                             const ThyristorParameters &params) {
    m_Impl->Description.TrackNodes({anode, cathode, gate});
    m_Impl->Description.Elements.push_back(ThyristorSpec{
        .Name = std::move(name),
        .Anode = anode,
        .Cathode = cathode,
        .Gate = gate,
        .Params = params,
    });
    return *this;
}

/**
 * @brief   Adds a DIAC, a gate-less trigger diode that breaks over either way and holds until its current falls.
 * @details The device is the latching core of the TRIAC with its gate tied to MT1, so the gate never fires it: only
 *          the breakover does, on the magnitude of the voltage. It reports one current, into MT2, as a two-terminal
 *          part does.
 * @param[in] name    Element designator.
 * @param[in] mt2     Main terminal 2 node, the one the current is reported into.
 * @param[in] mt1     Main terminal 1 node.
 * @param[in] params  Breakover, on-state and holding figures.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddDiac(std::string name, const NodeId mt2, const NodeId mt1, const DiacParameters &params) {
    m_Impl->Description.TrackNodes({mt2, mt1});
    m_Impl->Description.Elements.push_back(ThyristorSpec{
        .Name = std::move(name),
        .Anode = mt2,
        .Cathode = mt1,
        .Gate = mt1,
        .Params = {.OnResistance = params.OnResistance,
                   .OnThresholdVoltage = params.OnThresholdVoltage,
                   .OffResistance = params.OffResistance,
                   .HoldingCurrent = params.HoldingCurrent,
                   .GateTriggerCurrent = std::numeric_limits<double>::infinity(),
                   .BreakoverVoltage = params.BreakoverVoltage,
                   .IsBidirectional = true},
        .HasGate = false,
    });
    return *this;
}

} // namespace Nodalys
