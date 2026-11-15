/**
 * @file    result_store.cpp
 * @brief   The results of the simulations of the schematic, with their steps and versions.
 */

#include "results/result_store.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace GUI {

/**
 * @brief   Drops every result, since none matches the circuit any more.
 */
void ResultStore::OnCircuitChanged() {
    m_Transient.reset();
    m_TransientSteps.reset();
    OnLiveChanged();
}

/**
 * @brief   Drops the results a live run does not make, which no longer match the circuit; the transient stays.
 */
void ResultStore::OnLiveChanged() {
    m_OperatingPoint.reset();
    m_OperatingPointSteps.reset();
    m_ACSweep.reset();
    m_ACSweepSteps.reset();
    m_DCSweep.reset();
    ClearMonteCarlo();
    m_Noise.reset();
    m_TransferFunction.reset();
    m_Sensitivity.reset();
    m_PoleZero.reset();
    m_SParameters.reset();
    m_TruthTable.reset();
    m_HarmonicBalance.reset();
}

/**
 * @brief   Stores the result of an operating point simulation of the current schematic.
 * @param[in] operating_point  Node voltages, numbered like Schematic::GetConnectivity(), or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetOperatingPoint(std::optional<Core::OperatingPoint> operating_point) {
    m_OperatingPointSteps.reset();
    ClearMonteCarlo();
    m_OperatingPoint = std::move(operating_point);
    ++m_OperatingPointVersion;
}

/**
 * @brief   Returns the last operating point simulated for the current schematic.
 * @return  The node voltages, or no value when there was no simulation since the last change.
 */
const std::optional<Core::OperatingPoint> &ResultStore::GetOperatingPoint() const {
    return m_OperatingPoint;
}

/**
 * @brief   Stores the operating points of a stepped run, one per value of its parameter or per Monte Carlo run.
 * @param[in] steps  Every step, with at least one result, and the selected one, which GetOperatingPoint() returns, so
 *                   the schematic shows it.
 * @note    The results are dropped on the next change, like those of a single run.
 */
void ResultStore::SetOperatingPointSteps(StepFamily<Core::OperatingPoint> steps) {
    ClearMonteCarlo();
    steps.Selected = std::min(steps.Selected, steps.Results.size() - 1);
    m_OperatingPoint = steps.Results[steps.Selected];
    m_OperatingPointSteps = std::move(steps);
    ++m_OperatingPointVersion;
}

/**
 * @brief   Returns every step of the last stepped operating point.
 * @return  The steps, or no value when the last operating point was a single run or there is none.
 */
const std::optional<StepFamily<Core::OperatingPoint>> &ResultStore::GetOperatingPointSteps() const {
    return m_OperatingPointSteps;
}

/**
 * @brief   Shows another step of the stepped operating point wherever one result is shown: the schematic and the
 *          tables of the Operating point tab.
 * @param[in] step  Index of the step; ignored when out of range or without steps.
 */
void ResultStore::SelectOperatingPointStep(const std::size_t step) {
    if (!m_OperatingPointSteps || step >= m_OperatingPointSteps->Results.size() ||
        step == m_OperatingPointSteps->Selected) {
        return;
    }
    m_OperatingPointSteps->Selected = step;
    m_OperatingPoint = m_OperatingPointSteps->Results[step];
    ++m_OperatingPointVersion;
}

/**
 * @brief   Returns a number that changes whenever an operating point result is stored.
 * @return  The operating point version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetOperatingPointVersion() const {
    return m_OperatingPointVersion;
}

/**
 * @brief   Stores the result of a transient simulation of the current schematic.
 * @param[in] transient  Node voltages over time, numbered like Schematic::GetConnectivity(), or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetTransient(std::optional<Core::Transient> transient) {
    m_TransientSteps.reset();
    ClearMonteCarlo();
    m_Transient = std::move(transient);
    ++m_TransientVersion;
    ++m_TransientDataVersion;
}

/**
 * @brief   Adds the points a transient has made since the last call to the one being made.
 * @param[in] chunk  The new points, numbered like Schematic::GetConnectivity(); the first chunk starts the result.
 * @note    The result stays incomplete until SetTransient() stores the whole run. A chunk that does not fit the
 *          result, because the circuit changed under it, is dropped.
 */
void ResultStore::AppendTransient(Core::Transient chunk) {
    if (!m_Transient) {
        chunk.IsComplete = false;
        SetTransient(std::move(chunk));
        return;
    }
    Core::Transient &whole = *m_Transient;
    const bool fits = whole.NodeVoltages.size() == chunk.NodeVoltages.size() &&
                      std::ranges::equal(whole.Currents, chunk.Currents, {}, &Core::ComponentTrace::Name,
                                         &Core::ComponentTrace::Name);
    if (!fits) {
        return;
    }
    whole.Times.insert(whole.Times.end(), chunk.Times.begin(), chunk.Times.end());
    for (std::size_t node = 0; node < chunk.NodeVoltages.size(); ++node) {
        whole.NodeVoltages[node].insert(whole.NodeVoltages[node].end(), chunk.NodeVoltages[node].begin(),
                                        chunk.NodeVoltages[node].end());
    }
    for (std::size_t current = 0; current < chunk.Currents.size(); ++current) {
        whole.Currents[current].Values.insert(whole.Currents[current].Values.end(),
                                              chunk.Currents[current].Values.begin(),
                                              chunk.Currents[current].Values.end());
    }
    ++m_TransientDataVersion;
}

/**
 * @brief   Returns the last transient simulated for the current schematic.
 * @return  The node voltages over time, or no value when there was no simulation since the last change.
 */
const std::optional<Core::Transient> &ResultStore::GetTransient() const {
    return m_Transient;
}

/**
 * @brief   Stores the transients of a stepped run, one per value of its parameter.
 * @param[in] steps  Every step, with at least one result, and the selected one, which GetTransient() returns.
 * @note    The results are dropped on the next change, like those of a single run.
 */
void ResultStore::SetTransientSteps(StepFamily<Core::Transient> steps) {
    ClearMonteCarlo();
    steps.Selected = std::min(steps.Selected, steps.Results.size() - 1);
    m_Transient = steps.Results[steps.Selected];
    m_TransientSteps = std::move(steps);
    ++m_TransientVersion;
    ++m_TransientDataVersion;
}

/**
 * @brief   Returns every step of the last stepped transient.
 * @return  The steps, or no value when the last transient was a single run or there is none.
 */
const std::optional<StepFamily<Core::Transient>> &ResultStore::GetTransientSteps() const {
    return m_TransientSteps;
}

/**
 * @brief   Shows another step of the stepped transient wherever one result is shown: the schematic, the statistics
 *          and the spectrum.
 * @param[in] step  Index of the step; ignored when out of range or without steps.
 */
void ResultStore::SelectTransientStep(const std::size_t step) {
    if (!m_TransientSteps || step >= m_TransientSteps->Results.size() || step == m_TransientSteps->Selected) {
        return;
    }
    m_TransientSteps->Selected = step;
    m_Transient = m_TransientSteps->Results[step];
    ++m_TransientVersion;
    ++m_TransientDataVersion;
}

/**
 * @brief   Returns a number that changes whenever a transient result is stored.
 * @return  The transient version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetTransientVersion() const {
    return m_TransientVersion;
}

/**
 * @brief   Returns a number that changes whenever the transient result changes, as a chunk is added too.
 * @return  The data version; compare it with a stored one to know whether the points to draw changed.
 */
std::size_t ResultStore::GetTransientDataVersion() const {
    return m_TransientDataVersion;
}

/**
 * @brief   Stores the result of an AC sweep of the current schematic.
 * @param[in] sweep  Node responses across frequency, numbered like Schematic::GetConnectivity(), or no value to clear
 * them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetACSweep(std::optional<Core::ACSweep> sweep) {
    m_ACSweepSteps.reset();
    ClearMonteCarlo();
    m_ACSweep = std::move(sweep);
    ++m_ACSweepVersion;
}

/**
 * @brief   Returns the last AC sweep simulated for the current schematic.
 * @return  The node responses, or no value when there was no simulation since the last change.
 */
const std::optional<Core::ACSweep> &ResultStore::GetACSweep() const {
    return m_ACSweep;
}

/**
 * @brief   Stores the AC sweeps of a stepped run, one per value of its parameter.
 * @param[in] steps  Every step, with at least one result, and the selected one, which GetACSweep() returns.
 * @note    The results are dropped on the next change, like those of a single run.
 */
void ResultStore::SetACSweepSteps(StepFamily<Core::ACSweep> steps) {
    ClearMonteCarlo();
    steps.Selected = std::min(steps.Selected, steps.Results.size() - 1);
    m_ACSweep = steps.Results[steps.Selected];
    m_ACSweepSteps = std::move(steps);
    ++m_ACSweepVersion;
}

/**
 * @brief   Returns every step of the last stepped AC sweep.
 * @return  The steps, or no value when the last AC sweep was a single run or there is none.
 */
const std::optional<StepFamily<Core::ACSweep>> &ResultStore::GetACSweepSteps() const {
    return m_ACSweepSteps;
}

/**
 * @brief   Measures another step of the stepped AC sweep in the statistics.
 * @param[in] step  Index of the step; ignored when out of range or without steps.
 */
void ResultStore::SelectACSweepStep(const std::size_t step) {
    if (!m_ACSweepSteps || step >= m_ACSweepSteps->Results.size() || step == m_ACSweepSteps->Selected) {
        return;
    }
    m_ACSweepSteps->Selected = step;
    m_ACSweep = m_ACSweepSteps->Results[step];
    ++m_ACSweepVersion;
}

/**
 * @brief   Returns a number that changes whenever an AC sweep result is stored.
 * @return  The AC sweep version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetACSweepVersion() const {
    return m_ACSweepVersion;
}

/**
 * @brief   Stores the result of a DC sweep of the current schematic.
 * @param[in] sweep  Node voltages and currents along the sweep, numbered like Schematic::GetConnectivity(), or no value
 * to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetDCSweep(std::optional<Core::DCSweep> sweep) {
    ClearMonteCarlo();
    m_DCSweep = std::move(sweep);
    ++m_DCSweepVersion;
}

/**
 * @brief   Returns the last DC sweep simulated for the current schematic.
 * @return  The curves of the sweep, or no value when there was no simulation since the last change.
 */
const std::optional<Core::DCSweep> &ResultStore::GetDCSweep() const {
    return m_DCSweep;
}

/**
 * @brief   Returns a number that changes whenever a DC sweep result is stored.
 * @return  The DC sweep version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetDCSweepVersion() const {
    return m_DCSweepVersion;
}

/**
 * @brief   Stores what the runs of a Monte Carlo analysis drew and measured.
 * @param[in] table  Every run, the nominal one first.
 * @note    Call it after storing the traces of the runs, which drop the table of an earlier analysis. It is dropped on
 *          the next change, like the traces.
 */
void ResultStore::SetMonteCarlo(MonteCarloTable table) {
    m_MonteCarlo = std::move(table);
    ++m_MonteCarloVersion;
}

/**
 * @brief   Returns the runs of the last Monte Carlo analysis.
 * @return  The table, or no value when the last analysis was not a Monte Carlo one or there is none.
 */
const std::optional<MonteCarloTable> &ResultStore::GetMonteCarlo() const {
    return m_MonteCarlo;
}

/**
 * @return  The Monte Carlo version; compare it with a stored one to know whether the table was replaced.
 */
std::size_t ResultStore::GetMonteCarloVersion() const {
    return m_MonteCarloVersion;
}

/**
 * @brief   Stores the result of a noise analysis of the current schematic.
 * @param[in] noise  Noise densities, totals and contributions, or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetNoise(std::optional<Core::Noise> noise) {
    m_Noise = std::move(noise);
    ++m_NoiseVersion;
}

/**
 * @brief   Returns the last noise analysis simulated for the current schematic.
 * @return  The noise result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::Noise> &ResultStore::GetNoise() const {
    return m_Noise;
}

/**
 * @brief   Returns a number that changes whenever a noise result is stored.
 * @return  The noise version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetNoiseVersion() const {
    return m_NoiseVersion;
}

/**
 * @brief   Stores the result of a transfer function analysis of the current schematic.
 * @param[in] transfer_function  Gain, input impedance and output impedance, or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetTransferFunction(std::optional<Core::TransferFunction> transfer_function) {
    m_TransferFunction = std::move(transfer_function);
    ++m_TransferFunctionVersion;
}

/**
 * @brief   Returns the last transfer function simulated for the current schematic.
 * @return  The transfer function result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::TransferFunction> &ResultStore::GetTransferFunction() const {
    return m_TransferFunction;
}

/**
 * @brief   Returns a number that changes whenever a transfer function result is stored.
 * @return  The transfer function version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetTransferFunctionVersion() const {
    return m_TransferFunctionVersion;
}

/**
 * @brief   Stores the result of a sensitivity analysis of the current schematic.
 * @param[in] sensitivity  Sensitivity of the output node to each part, or no value to clear it.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetSensitivity(std::optional<Core::Sensitivity> sensitivity) {
    m_Sensitivity = std::move(sensitivity);
    ++m_SensitivityVersion;
}

/**
 * @brief   Returns the last sensitivity simulated for the current schematic.
 * @return  The sensitivity result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::Sensitivity> &ResultStore::GetSensitivity() const {
    return m_Sensitivity;
}

/**
 * @brief   Returns a number that changes whenever a sensitivity result is stored.
 * @return  The sensitivity version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetSensitivityVersion() const {
    return m_SensitivityVersion;
}

/**
 * @brief   Stores the result of a pole-zero analysis of the current schematic.
 * @param[in] pole_zero  Poles and zeros from the input source to the output node, or no value to clear them.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetPoleZero(std::optional<Core::PoleZero> pole_zero) {
    m_PoleZero = std::move(pole_zero);
    ++m_PoleZeroVersion;
}

/**
 * @brief   Returns the last pole-zero analysis run for the current schematic.
 * @return  The poles and zeros, or no value when there was no analysis since the last change.
 */
const std::optional<Core::PoleZero> &ResultStore::GetPoleZero() const {
    return m_PoleZero;
}

/**
 * @brief   Returns a number that changes whenever a pole-zero result is stored.
 * @return  The pole-zero version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetPoleZeroVersion() const {
    return m_PoleZeroVersion;
}

/**
 * @brief   Stores the result of an S-parameter analysis of the current schematic.
 * @param[in] s_parameters  The scattering matrix over the sweep, or no value to clear it.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetSParameters(std::optional<Core::SParameters> s_parameters) {
    m_SParameters = std::move(s_parameters);
    ++m_SParametersVersion;
}

/**
 * @brief   Returns the last S-parameter analysis run for the current schematic.
 * @return  The scattering matrix, or no value when there was no analysis since the last change.
 */
const std::optional<Core::SParameters> &ResultStore::GetSParameters() const {
    return m_SParameters;
}

/**
 * @brief   Returns a number that changes whenever an S-parameter result is stored.
 * @return  The S-parameter version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetSParametersVersion() const {
    return m_SParametersVersion;
}

/**
 * @brief   Stores the result of a truth table analysis of the current schematic.
 * @param[in] truth_table  The rows of the table, or no value to clear it.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetTruthTable(std::optional<Core::TruthTable> truth_table) {
    m_TruthTable = std::move(truth_table);
    ++m_TruthTableVersion;
}

/**
 * @brief   Returns the last truth table simulated for the current schematic.
 * @return  The truth table, or no value when there was no simulation since the last change.
 */
const std::optional<Core::TruthTable> &ResultStore::GetTruthTable() const {
    return m_TruthTable;
}

/**
 * @brief   Returns a number that changes whenever a truth table result is stored.
 * @return  The truth table version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetTruthTableVersion() const {
    return m_TruthTableVersion;
}

/**
 * @brief   Stores the result of a harmonic balance analysis of the current schematic.
 * @param[in] harmonic_balance  The harmonic spectra and reconstructed waveforms, or no value to clear it.
 * @note    The result is dropped on the next change, since it no longer matches the circuit.
 */
void ResultStore::SetHarmonicBalance(std::optional<Core::HarmonicBalance> harmonic_balance) {
    m_HarmonicBalance = std::move(harmonic_balance);
    ++m_HarmonicBalanceVersion;
}

/**
 * @brief   Returns the last harmonic balance analysis run for the current schematic.
 * @return  The harmonic balance result, or no value when there was no simulation since the last change.
 */
const std::optional<Core::HarmonicBalance> &ResultStore::GetHarmonicBalance() const {
    return m_HarmonicBalance;
}

/**
 * @brief   Returns a number that changes whenever a harmonic balance result is stored.
 * @return  The harmonic balance version; compare it with a stored one to know whether the result was replaced.
 */
std::size_t ResultStore::GetHarmonicBalanceVersion() const {
    return m_HarmonicBalanceVersion;
}

/**
 * @brief   Drops the table of a Monte Carlo analysis, which no longer matches the traces next to it.
 */
void ResultStore::ClearMonteCarlo() {
    if (m_MonteCarlo) {
        m_MonteCarlo.reset();
        ++m_MonteCarloVersion;
    }
}

} // namespace GUI
