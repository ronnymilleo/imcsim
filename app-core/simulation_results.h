/**
 * @file    simulation_results.h
 * @brief   What a simulation returns: the results of each analysis, and the outcome of a run with its messages.
 */

#ifndef IMCSIM_SIMULATION_RESULTS_H
#define IMCSIM_SIMULATION_RESULTS_H

#include <expected>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  SimulatorMessage
 * @brief   One line printed by the simulator while it ran.
 * @details ngspice prints warnings and errors on its error stream, such as "singular matrix" for a node with
 *          no DC path to ground, which often explains odd results even when the run succeeds.
 */
struct SimulatorMessage {
    std::string Text;
    bool FromErrorStream = false;
};

/**
 * @struct  ComponentCurrent
 * @brief   The current through one component at the operating point.
 */
struct ComponentCurrent {
    std::string Name;
    double Current = 0.0;
};

/**
 * @struct  ComponentTrace
 * @brief   Values of one component at every point of an analysis, such as its current over time.
 */
struct ComponentTrace {
    std::string Name;
    std::vector<double> Values;
};

/**
 * @struct  OperatingPoint
 * @brief   DC operating point of a circuit: the voltage of every node and the current through every component.
 * @details Currents follow SPICE: positive when flowing through the component from its first terminal to its
 *          second. A voltage source that delivers power therefore shows a negative current.
 */
struct OperatingPoint {
    // Indexed by node number; node 0 is ground and always 0 V
    std::vector<double> NodeVoltages;
    // One per named component, in circuit order; ground carries none
    std::vector<ComponentCurrent> Currents;
    // Problems in the results that ngspice does not report, such as a diode in reverse breakdown
    std::vector<std::string> Warnings;
};

/**
 * @struct  Transient
 * @brief   Node voltages and component currents over time, with currents signed as in OperatingPoint.
 */
struct Transient {
    std::vector<double> Times;
    // Indexed by node number, then by time point; node 0 is ground and always 0 V
    std::vector<std::vector<double>> NodeVoltages;
    // One per named component, in circuit order, with a value per time point
    std::vector<ComponentTrace> Currents;
    // Problems in the results that ngspice does not report, such as a diode in reverse breakdown
    std::vector<std::string> Warnings;
};

/**
 * @struct  ACSweep
 * @brief   Small-signal response of every node and component across frequency, relative to the AC sources.
 * @details Voltages are in dB relative to 1 V and currents in dB relative to 1 A; currents are signed as in
 *          OperatingPoint, which shows in their phase.
 */
struct ACSweep {
    std::vector<double> Frequencies;
    // Indexed by node number, then by frequency point; node 0 is ground and stays empty
    std::vector<std::vector<double>> NodeMagnitudesDecibels;
    std::vector<std::vector<double>> NodePhasesDegrees;
    // One per named component, in circuit order, with a value per frequency point
    std::vector<ComponentTrace> CurrentMagnitudesDecibels;
    std::vector<ComponentTrace> CurrentPhasesDegrees;
};

/**
 * @struct  DCSweepCurve
 * @brief   Node voltages and component currents along a DC sweep, for one value of the stepped source.
 */
struct DCSweepCurve {
    // Value of the stepped source for this curve; 0 when there is no stepped source
    double StepValue = 0.0;
    // Indexed by node number, then by swept value; node 0 is ground and always 0 V
    std::vector<std::vector<double>> NodeVoltages;
    // One per current, as in Transient, with a value per swept value
    std::vector<ComponentTrace> Currents;
};

/**
 * @struct  DCSweep
 * @brief   Operating points of a circuit while one source sweeps through its values, as one curve or a family.
 * @details Currents are signed as in OperatingPoint. Units are "V" or "A", after the kind of each source.
 */
struct DCSweep {
    std::string SweptSource;
    std::string SweptUnit;
    // The X axis of every curve
    std::vector<double> SweptValues;
    // Empty when no source is stepped
    std::string SteppedSource;
    std::string SteppedUnit;
    // One per value of the stepped source, or a single curve
    std::vector<DCSweepCurve> Curves;
};

/**
 * @struct  NoiseContribution
 * @brief   The share of one part in the noise at the output: its density at every frequency and its total.
 * @details A part written as several elements, such as an op-amp macromodel, adds up the noise of all of them.
 */
struct NoiseContribution {
    std::string Name;
    std::vector<double> OutputDensities;
    double OutputTotal = 0.0;
};

/**
 * @struct  Noise
 * @brief   The noise of a circuit at a node across frequency, and the same noise referred to its input source.
 * @details Densities are RMS per root hertz: V/sqrt(Hz) at the output, and V/sqrt(Hz) or A/sqrt(Hz) at the input,
 *          after the kind of the input source. Totals are RMS over the whole frequency range.
 */
struct Noise {
    int OutputNode = 0;
    std::string InputSource;
    std::string InputUnit;
    std::vector<double> Frequencies;
    std::vector<double> OutputDensities;
    std::vector<double> InputDensities;
    double OutputTotal = 0.0;
    double InputTotal = 0.0;
    // Every part that adds noise, the largest total first
    std::vector<NoiseContribution> Contributions;
};

/**
 * @struct  TransferFunction
 * @brief   Small-signal DC gain from an input source to an output, with the input and output impedances.
 * @details The gain is dimensionless (V/V) for a voltage source input and a transimpedance (V/A) for a current
 *          source. Both impedances are in ohms: the input is the resistance seen by the source and the output the
 *          resistance seen looking back into the output. An open path is reported around 1e20 ohm, as ngspice does.
 */
struct TransferFunction {
    int OutputNode = 0;
    std::string InputSource;
    // Unit of the input source, "V" or "A", which sets the unit of the gain
    std::string InputUnit;
    double Gain = 0.0;
    double InputImpedance = 0.0;
    double OutputImpedance = 0.0;
};

/**
 * @struct  TruthTableRow
 * @brief   One combination of the inputs and the voltages it gives at the output nodes.
 * @details The voltages are empty when ngspice found no operating point for the combination, as a latch whose
 *          outputs depend on its history has none.
 */
struct TruthTableRow {
    // Whether each input is high, in the order of TruthTable::InputNames
    std::vector<bool> InputsHigh;
    // One voltage per output node, in the order of TruthTable::OutputNodes
    std::vector<double> OutputVoltages;

    bool HasOperatingPoint() const;
};

/**
 * @struct  TruthTable
 * @brief   Every combination of the chosen logic inputs, in binary counting order, with the outputs it gives.
 */
struct TruthTable {
    std::vector<std::string> InputNames;
    // High voltage of each input, so a view can tell what level the signals swing to
    std::vector<double> InputLevels;
    std::vector<int> OutputNodes;
    std::vector<TruthTableRow> Rows;
    std::vector<std::string> Warnings;
};

/**
 * @struct  SimulationRun
 * @brief   Outcome of a simulation, with everything the simulator printed.
 */
template <typename Data> struct SimulationRun {
    std::expected<Data, std::string> Result;
    std::vector<SimulatorMessage> Messages;
};

using OperatingPointRun = SimulationRun<OperatingPoint>;
using TransientRun = SimulationRun<Transient>;
using ACSweepRun = SimulationRun<ACSweep>;
using DCSweepRun = SimulationRun<DCSweep>;
using NoiseRun = SimulationRun<Noise>;
using TransferFunctionRun = SimulationRun<TransferFunction>;
using TruthTableRun = SimulationRun<TruthTable>;

} // namespace Core

#endif // IMCSIM_SIMULATION_RESULTS_H
