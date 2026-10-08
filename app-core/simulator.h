/**
 * @file    simulator.h
 * @brief   Runs circuits through the ngspice shared library and returns their results.
 */

#ifndef IMCSIM_SIMULATOR_H
#define IMCSIM_SIMULATOR_H

#include "circuit.h"
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  SimulatorMessage
 * @brief   One line printed by ngspice while it ran.
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
 * @struct  TransientSettings
 * @brief   Time span and resolution of a transient simulation.
 */
struct TransientSettings {
    double StopTime = 10e-3;
    // Largest step between output points; ngspice may take smaller ones where the signals change fast
    double TimeStep = 10e-6;

    bool operator==(const TransientSettings &) const = default;
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
 * @struct  ACSweepSettings
 * @brief   Frequency range and resolution of an AC sweep, with points spaced logarithmically.
 */
struct ACSweepSettings {
    double StartFrequency = 1.0;
    double StopFrequency = 1e6;
    int PointsPerDecade = 20;

    bool operator==(const ACSweepSettings &) const = default;
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
 * @struct  SweepRange
 * @brief   A source and the values a DC sweep takes it through, in the unit of the source (volts or amperes).
 * @details Source is the name of a voltage source, a current source or a VCC rail. The values go from Start to
 *          Stop in steps of Step, which may be negative to sweep downward.
 */
struct SweepRange {
    std::string Source;
    double Start = 0.0;
    double Stop = 5.0;
    double Step = 0.1;

    bool operator==(const SweepRange &) const = default;
};

/**
 * @struct  DCSweepSettings
 * @brief   The source a DC sweep runs through its values and, optionally, a second source stepped once per curve.
 * @details With a second source, the whole sweep repeats for each of its values, which draws a family of curves,
 *          such as the collector characteristics of a transistor for several base currents.
 */
struct DCSweepSettings {
    SweepRange Swept;
    std::optional<SweepRange> Stepped;
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
 * @struct  SimulationRun
 * @brief   Outcome of a simulation, with everything ngspice printed.
 */
template <typename Data> struct SimulationRun {
    std::expected<Data, std::string> Result;
    std::vector<SimulatorMessage> Messages;
};

using OperatingPointRun = SimulationRun<OperatingPoint>;
using TransientRun = SimulationRun<Transient>;
using ACSweepRun = SimulationRun<ACSweep>;
using DCSweepRun = SimulationRun<DCSweep>;

OperatingPointRun RunOperatingPoint(const Circuit &circuit);
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings);
ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings);
DCSweepRun RunDCSweep(const Circuit &circuit, const DCSweepSettings &settings);

// Names in the results
std::vector<std::string> GetSweepableSources(const Circuit &circuit);
std::vector<std::string> GetCurrentNames(const Component &component);

} // namespace Core

#endif // IMCSIM_SIMULATOR_H
