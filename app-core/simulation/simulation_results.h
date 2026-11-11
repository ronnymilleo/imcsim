/**
 * @file    simulation_results.h
 * @brief   What a simulation returns: the results of each analysis, and the outcome of a run with its messages.
 */

#ifndef IMCSIM_SIMULATION_RESULTS_H
#define IMCSIM_SIMULATION_RESULTS_H

#include "components/component.h"
#include <complex>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
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
    /// Indexed by node number; node 0 is ground and always 0 V.
    std::vector<double> NodeVoltages;
    /// One per named component, in circuit order; ground carries none.
    std::vector<ComponentCurrent> Currents;
    /// Problems in the results that ngspice does not report, such as a diode in reverse breakdown.
    std::vector<std::string> Warnings;
};

/**
 * @struct  Transient
 * @brief   Node voltages and component currents over time, with currents signed as in OperatingPoint.
 */
struct Transient {
    std::vector<double> Times;
    /// Indexed by node number, then by time point; node 0 is ground and always 0 V.
    std::vector<std::vector<double>> NodeVoltages;
    /// One per named component, in circuit order, with a value per time point.
    std::vector<ComponentTrace> Currents;
    /// Problems in the results that ngspice does not report, such as a diode in reverse breakdown.
    std::vector<std::string> Warnings;
    /// False while the run still goes on and after a stop, when the points end before the stop time.
    bool IsComplete = true;
};

/**
 * @struct  ACSweep
 * @brief   Small-signal response of every node and component across frequency, relative to the AC sources.
 * @details Voltages are in dB relative to 1 V and currents in dB relative to 1 A; currents are signed as in
 *          OperatingPoint, which shows in their phase.
 */
struct ACSweep {
    std::vector<double> Frequencies;
    /// Indexed by node number, then by frequency point; node 0 is ground and stays empty.
    std::vector<std::vector<double>> NodeMagnitudesDecibels;
    /// Indexed as NodeMagnitudesDecibels.
    std::vector<std::vector<double>> NodePhasesDegrees;
    /// One per named component, in circuit order, with a value per frequency point.
    std::vector<ComponentTrace> CurrentMagnitudesDecibels;
    /// Indexed as CurrentMagnitudesDecibels.
    std::vector<ComponentTrace> CurrentPhasesDegrees;
};

/**
 * @struct  DCSweepCurve
 * @brief   Node voltages and component currents along a DC sweep, for one value of the stepped source.
 */
struct DCSweepCurve {
    /// Value of the stepped source for this curve; 0 when there is no stepped source.
    double StepValue = 0.0;
    /// Indexed by node number, then by swept value; node 0 is ground and always 0 V.
    std::vector<std::vector<double>> NodeVoltages;
    /// One per current, as in Transient, with a value per swept value.
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
    /// The X axis of every curve.
    std::vector<double> SweptValues;
    /// Empty when no source is stepped.
    std::string SteppedSource;
    /// Empty when no source is stepped.
    std::string SteppedUnit;
    /// One per value of the stepped source, or a single curve.
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
    /// Every part that adds noise, the largest total first.
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
    /// Unit of the input source, "V" or "A", which sets the unit of the gain.
    std::string InputUnit;
    double Gain = 0.0;
    double InputImpedance = 0.0;
    double OutputImpedance = 0.0;
};

/**
 * @struct  PoleZeroRoot
 * @brief   One pole or zero of a transfer function, with a conjugate pair kept as one row.
 * @details Real and Imaginary are in rad/s. A complex root is stored once, with the positive imaginary part, and
 *          stands for its conjugate too; a real root has no imaginary part. Quality factor and damping ratio belong to
 *          complex roots only, and a root on the imaginary axis has an infinite quality factor, kept as no value.
 */
struct PoleZeroRoot {
    bool IsPole = true;
    double Real = 0.0;
    double Imaginary = 0.0;
    double NaturalFrequencyHertz = 0.0;
    std::optional<double> QualityFactor;
    std::optional<double> DampingRatio;
    /// A pole there makes the circuit unstable, or the solver failed to find it right.
    bool IsRightHalfPlane = false;
};

/**
 * @struct  PoleZero
 * @brief   The poles and zeros from an input source to the voltage of an output node.
 * @details Poles come first, then zeros, each by growing natural frequency. A pole and a zero at the same place cancel
 *          and are left out; CancelledPairs counts them, since ngspice reports them for parts the signal never reaches.
 *          Warnings tell what makes the list doubtful, such as roots the solver gave up on.
 */
struct PoleZero {
    int OutputNode = 0;
    std::string InputSource;
    /// Unit of the input source, "V" or "A", which tells whether the transfer is a gain or a transimpedance.
    std::string InputUnit;
    std::vector<PoleZeroRoot> Roots;
    int CancelledPairs = 0;
    std::vector<std::string> Warnings;
};

/**
 * @struct  SParameters
 * @brief   The scattering matrix of the ports of a circuit at every frequency of a sweep.
 * @details The matrix is square, one row and one column per port, numbered from 1 as the ports are. Values holds the
 *          matrices one after the other, frequency by frequency and row by row, so S(i, j) at a frequency is the wave
 *          leaving port i for a wave entering port j. The waves are power waves, normalized to the reference impedance
 *          of each port.
 */
struct SParameters {
    std::vector<double> Frequencies;
    /// One reference impedance in ohms per port, in the order of the port numbers.
    std::vector<double> ReferenceOhms;
    std::vector<std::complex<double>> Values;
    std::vector<std::string> Warnings;

    int GetPortCount() const;
    std::complex<double> Get(std::size_t frequency, int row, int column) const;
};

/**
 * @struct  PartSensitivity
 * @brief   How much the output node voltage changes with the value of one part.
 * @details Absolute is in volts per unit of the part (V/Ohm, V/F, V/V...). Normalized is the change in volts for a
 *          100 % change of the value, which compares parts of different kinds; its sign is the sign of the change of
 *          the output when the value grows. A block reports its most influential element inside, named by Element,
 *          and a potentiometer the two halves together, as its total resistance.
 */
struct PartSensitivity {
    std::string Name;
    /// Kind of the part, which tells how the schematic labels it.
    ComponentType Type = ComponentType::RESISTOR;
    /// The element inside the block that has the largest normalized sensitivity, such as "R1"; empty for other parts.
    std::string Element;
    /// Unit of the value of the part, as Component::GetUnit() gives it; empty for a gain.
    std::string Unit;
    /// None for an element inside a block, whose value the results do not carry.
    std::optional<double> Value;
    double Absolute = 0.0;
    double Normalized = 0.0;
};

/**
 * @struct  Sensitivity
 * @brief   The DC sensitivity of the voltage of a node to every part with a value, around the operating point.
 * @details Parts are listed from the largest absolute normalized sensitivity to the smallest. Diodes, transistors,
 *          op-amps, switches and other parts without a value of their own are not listed.
 */
struct Sensitivity {
    int OutputNode = 0;
    std::vector<PartSensitivity> Parts;
};

/**
 * @struct  TruthTableRow
 * @brief   One combination of the inputs and the voltages it gives at the output nodes.
 * @details The voltages are empty when ngspice found no operating point for the combination, as a latch whose
 *          outputs depend on its history has none.
 */
struct TruthTableRow {
    /// Whether each input is high, in the order of TruthTable::InputNames.
    std::vector<bool> InputsHigh;
    /// One voltage per output node, in the order of TruthTable::OutputNodes.
    std::vector<double> OutputVoltages;

    bool HasOperatingPoint() const;
};

/**
 * @struct  TruthTable
 * @brief   Every combination of the chosen logic inputs, in binary counting order, with the outputs it gives.
 */
struct TruthTable {
    std::vector<std::string> InputNames;
    /// High voltage of each input, so a view can tell what level the signals swing to.
    std::vector<double> InputLevels;
    std::vector<int> OutputNodes;
    std::vector<TruthTableRow> Rows;
    std::vector<std::string> Warnings;
};

/**
 * @struct  ComponentPhasors
 * @brief   Complex peak harmonic phasors for one component current, indexed from DC (0) to H.
 */
struct ComponentPhasors {
    std::string Name;
    std::vector<std::complex<double>> Phasors;
};

/**
 * @struct  HarmonicBalance
 * @brief   Periodic steady-state spectra and one-cycle waveforms solved in the frequency domain.
 * @details Phasors store peak complex amplitudes indexed from DC (0) to the highest harmonic, matching the
 *          real signal $x(t) = X_0 + \sum_{k=1}^{H} \operatorname{Re}(X_k e^{j k \omega_0 t})$.
 */
struct HarmonicBalance {
    double FundamentalFrequency = 0.0;
    std::vector<double> Frequencies;
    /// Indexed by node number, then by harmonic index; node 0 is ground and stays zero.
    std::vector<std::vector<std::complex<double>>> NodePhasors;
    /// One per named component, in circuit order.
    std::vector<ComponentPhasors> CurrentPhasors;
    /// Sample instants across one fundamental period $[0, T_0)$.
    std::vector<double> Times;
    /// Reconstructed periodic voltage waveforms indexed by node number, then by time sample.
    std::vector<std::vector<double>> NodeWaveforms;
    /// Reconstructed periodic current waveforms per component.
    std::vector<ComponentTrace> CurrentWaveforms;
    std::vector<std::string> Warnings;
    int Iterations = 0;
    /// Solver strategy that converged: "linear", "direct", "source stepping", or "gmin stepping".
    std::string Strategy;
};

[[nodiscard]] double ComputeHarmonicBalanceThd(std::span<const std::complex<double>> phasors);

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
using SensitivityRun = SimulationRun<Sensitivity>;
using PoleZeroRun = SimulationRun<PoleZero>;
using SParametersRun = SimulationRun<SParameters>;
using TransferFunctionRun = SimulationRun<TransferFunction>;
using TruthTableRun = SimulationRun<TruthTable>;
using HarmonicBalanceRun = SimulationRun<HarmonicBalance>;

} // namespace Core

#endif // IMCSIM_SIMULATION_RESULTS_H
