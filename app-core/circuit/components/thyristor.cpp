/**
 * @file    thyristor.cpp
 * @brief   A thyristor (SCR) or TRIAC that fires on gate current, latches on, and releases below its holding current.
 */

#include "circuit/components/thyristor.h"

#include <format>
#include <memory>

namespace Core {

/**
 * @brief   Creates a thyristor that fires at 1 mA of gate current, holds on above 5 mA of main current and drops
 *          0.8 V plus 10 mOhm on the on-state line.
 * @param[in] type  THYRISTOR for a one-way SCR, TRIAC for a bidirectional one; any other type is refused by the
 *                  caller, which only places these two.
 */
Thyristor::Thyristor(const ComponentType type) : Component(type, 0.0) {
    m_Parameters.IsBidirectional = type == ComponentType::TRIAC;
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
 * @brief   Sets the parameters of the thyristor, keeping the direction the part type fixes.
 * @param[in] parameters  New parameters; check them with IsValidParameters() first.
 */
void Thyristor::SetParameters(const ThyristorParameters &parameters) {
    m_Parameters = parameters;
    m_Parameters.IsBidirectional = GetType() == ComponentType::TRIAC;
}

/**
 * @brief   Checks whether a set of thyristor parameters can be simulated.
 * @param[in] parameters  Candidate parameters.
 * @return  True when every resistance is positive, the on-state line lies inside the blocking one and every current
 *          and voltage is non-negative, with a breakover, a critical dv/dt and a turn-off time of zero meaning
 *          disabled.
 */
bool Thyristor::IsValidParameters(const ThyristorParameters &parameters) {
    return parameters.OnResistance > 0.0 && parameters.OnResistance < parameters.OffResistance
        && parameters.OnThresholdVoltage >= 0.0 && parameters.HoldingCurrent > 0.0
        && parameters.GateTriggerCurrent > 0.0 && parameters.GateTriggerVoltage >= 0.0
        && parameters.GateResistance > 0.0 && parameters.BreakoverVoltage >= 0.0 && parameters.CriticalDvDt >= 0.0
        && parameters.TurnOffTime >= 0.0;
}

const char *GetThyristorSubcircuitName() {
    return "imcsim_scr";
}

/**
 * @brief   Writes the two-transistor subcircuit that ngspice runs for a thyristor (SCR).
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
 *          base-emitter shunts, close to the figures of Nodalys, which takes them as parameters. The emitter
 *          junction capacitances soften the switching, so the pair converges. The collector junctions have none: a
 *          rising off-state voltage would push its displacement current into the bases, and with the 100k shunts the
 *          pair would fire at a fraction of a volt per microsecond, as a 325 V mains sine does.
 * @note    There is no forward breakover and no dv/dt firing: the pair blocks whatever forward voltage the circuit
 *          puts on it, however fast it rises, as a thyristor driven correctly does. Nodalys models both.
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
           ".model IMCSIM_SCR_QP PNP(IS=1e-14 BF=100 CJE=20p)\n"
           ".model IMCSIM_SCR_QN NPN(IS=1e-14 BF=100 CJE=20p)\n"
           ".model IMCSIM_SCR_DG D(IS=1e-12 N=0.001)\n"
           ".model IMCSIM_SCR_DT D(IS=1e-12 N=0.001)\n"
           ".ends imcsim_scr\n";
}

const char *GetTriacSubcircuitName() {
    return "imcsim_triac";
}

/**
 * @brief   Writes the subcircuit that ngspice runs for a TRIAC.
 * @return  The ".subckt" block, newline included.
 * @details Two SCR pairs of the imcsim_scr kind sit anti-parallel between MT2 and MT1, each with its own vanishing
 *          anode diode so it conducts one way only: the pair P latches when MT2 is positive, the pair N when MT1 is.
 *          The pair N also has one at its cathode, MT2, so while MT2 is positive the collector junction of its NPN
 *          cannot carry current from MT2 into the gate stage, which would fire the pair P.
 *          A gate stage referenced to MT1 routes gate current of either polarity to whichever pair is forward-biased,
 *          so the device fires in all four quadrants. A positive gate drives the NPN base of P directly through a
 *          vanishing diode, and a PNP sensor turns on a switched path into the NPN base of N for the fourth quadrant;
 *          a negative gate pulls the PNP base of N directly, and an NPN sensor switches a pull from the PNP base of P
 *          for the second quadrant. Each sensor reads the gate against MT1 through a base resistor, and a bleed holds
 *          its input at MT1, so a sensor conducts only while the gate actually draws current and the device blocks
 *          with the gate open or tied to MT1, whatever MT2 does. The collector of each sensor is pulled toward MT2
 *          through a diode, only in the quadrant where it steers: without it, MT2 of the other polarity would bias
 *          the sensor's collector junction forward and drive the gate stage by itself, firing the device at mains
 *          voltage. The diodes keep the latched base current from leaking
 *          back out of the gate. `RG` and `RT` are parameters of the instance, RG setting the steering paths; the
 *          holding and trigger currents and the on-state threshold emerge from the transistors, close to the figures of
 *          Nodalys. The stage steers with transistor gain, so at the firing instant it draws more gate current than
 *          the single junction of Nodalys, which the cross-validation test bounds. As in imcsim_scr, the collector
 *          junctions have no capacitance, so a rising off-state voltage does not fire a pair.
 * @note    There is no breakover and no dv/dt firing: the pairs block whatever voltage the circuit puts on them,
 *          however fast it rises, as a TRIAC driven correctly does. Nodalys models both.
 */
std::string FormatTriacSubcircuit() {
    return ".subckt imcsim_triac mt2 mt1 g params: RG=100 RT=0.01\n"
           "Dtp mt2 nrtp IMCSIM_TRIAC_DT\n"
           "Rtp nrtp nap {RT}\n"
           "Q1p n2p n1p nap IMCSIM_TRIAC_QP\n"
           "Q2p n1p n2p mt1 IMCSIM_TRIAC_QN\n"
           "R1p n1p nap 100k\n"
           "R2p n2p mt1 100k\n"
           "Dtn mt1 nrtn IMCSIM_TRIAC_DT\n"
           "Rtn nrtn nan {RT}\n"
           "Q1n n2n n1n nan IMCSIM_TRIAC_QP\n"
           "Q2n n1n n2n nkn IMCSIM_TRIAC_QN\n"
           "R1n n1n nan 100k\n"
           "R2n n2n nkn 100k\n"
           "Dkn nkn mt2 IMCSIM_TRIAC_DT\n"
           "Dgp g pg IMCSIM_TRIAC_DG\n"
           "Rsp pg mt1 1k\n"
           "Qsp cp nbp pg IMCSIM_TRIAC_QP\n"
           "Rbp nbp mt1 1k\n"
           "Dp pg np2p IMCSIM_TRIAC_DG\n"
           "Rp np2p n2p {RG}\n"
           "Dpn cp np2n IMCSIM_TRIAC_DG\n"
           "Rpn np2n n2n {RG}\n"
           "Rcp cp ncp 100k\n"
           "Dcp ncp mt2 IMCSIM_TRIAC_DG\n"
           "Dgn ng g IMCSIM_TRIAC_DG\n"
           "Rsn mt1 ng 1k\n"
           "Qsn cn nbn ng IMCSIM_TRIAC_QN\n"
           "Rbn nbn mt1 1k\n"
           "Dn n1n nn1n IMCSIM_TRIAC_DG\n"
           "Rn nn1n ng {RG}\n"
           "Dnp n1p nn1p IMCSIM_TRIAC_DG\n"
           "Rnp nn1p cn {RG}\n"
           "Rcn cn ncn 100k\n"
           "Dcn mt2 ncn IMCSIM_TRIAC_DG\n"
           "Rlk g mt1 1G\n"
           ".model IMCSIM_TRIAC_QP PNP(IS=1e-14 BF=100 CJE=20p)\n"
           ".model IMCSIM_TRIAC_QN NPN(IS=1e-14 BF=100 CJE=20p)\n"
           ".model IMCSIM_TRIAC_DG D(IS=1e-12 N=0.001)\n"
           ".model IMCSIM_TRIAC_DT D(IS=1e-12 N=0.001)\n"
           ".ends imcsim_triac\n";
}

} // namespace Core