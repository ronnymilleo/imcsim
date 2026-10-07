/**
 * @file    simulator.h
 * @brief   Runs circuits through the ngspice shared library and returns their results.
 */

#ifndef IMCSIM_SIMULATOR_H
#define IMCSIM_SIMULATOR_H

#include "circuit.h"
#include <expected>
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

OperatingPointRun RunOperatingPoint(const Circuit &circuit);
TransientRun RunTransient(const Circuit &circuit, const TransientSettings &settings);
ACSweepRun RunACSweep(const Circuit &circuit, const ACSweepSettings &settings);

} // namespace Core

#endif // IMCSIM_SIMULATOR_H
