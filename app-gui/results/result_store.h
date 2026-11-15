/**
 * @file    result_store.h
 * @brief   The results of the simulations of the schematic, with their steps and versions.
 */

#ifndef IMCSIM_RESULT_STORE_H
#define IMCSIM_RESULT_STORE_H

#include "results/monte_carlo_results.h"
#include "results/parameter_step.h"
#include "schematic/circuit_listener.h"
#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   ResultStore
 * @brief   The last result of each analysis of the circuit being edited, which no longer matches it once it changes.
 * @details The store is a CircuitListener of the Schematic: a change of the circuit drops every result at once, and a
 *          change of a part in a live run drops all but the transient, which the run keeps making. A stepped
 *          operating point, transient or AC sweep keeps every step and hands its selected step to the views that read
 *          one result. A version counts the results stored, so windows can tell a rerun from the one they showed.
 *          The application owns one store beside the schematic; a schematic of a block being edited has none, since
 *          no analysis runs on it.
 */
class ResultStore : public CircuitListener {
public:
    // Circuit changes
    void OnCircuitChanged() override;
    void OnLiveChanged() override;

    // Operating point
    void SetOperatingPoint(std::optional<Core::OperatingPoint> operating_point);
    const std::optional<Core::OperatingPoint> &GetOperatingPoint() const;
    void SetOperatingPointSteps(StepFamily<Core::OperatingPoint> steps);
    const std::optional<StepFamily<Core::OperatingPoint>> &GetOperatingPointSteps() const;
    void SelectOperatingPointStep(std::size_t step);
    std::size_t GetOperatingPointVersion() const;

    // Transient
    void SetTransient(std::optional<Core::Transient> transient);
    void AppendTransient(Core::Transient chunk);
    const std::optional<Core::Transient> &GetTransient() const;
    void SetTransientSteps(StepFamily<Core::Transient> steps);
    const std::optional<StepFamily<Core::Transient>> &GetTransientSteps() const;
    void SelectTransientStep(std::size_t step);
    std::size_t GetTransientVersion() const;
    std::size_t GetTransientDataVersion() const;

    // AC sweep
    void SetACSweep(std::optional<Core::ACSweep> sweep);
    const std::optional<Core::ACSweep> &GetACSweep() const;
    void SetACSweepSteps(StepFamily<Core::ACSweep> steps);
    const std::optional<StepFamily<Core::ACSweep>> &GetACSweepSteps() const;
    void SelectACSweepStep(std::size_t step);
    std::size_t GetACSweepVersion() const;

    // DC sweep
    void SetDCSweep(std::optional<Core::DCSweep> sweep);
    const std::optional<Core::DCSweep> &GetDCSweep() const;
    std::size_t GetDCSweepVersion() const;

    // Monte Carlo
    void SetMonteCarlo(MonteCarloTable table);
    const std::optional<MonteCarloTable> &GetMonteCarlo() const;
    std::size_t GetMonteCarloVersion() const;

    // Noise
    void SetNoise(std::optional<Core::Noise> noise);
    const std::optional<Core::Noise> &GetNoise() const;
    std::size_t GetNoiseVersion() const;

    // Transfer function
    void SetTransferFunction(std::optional<Core::TransferFunction> transfer_function);
    const std::optional<Core::TransferFunction> &GetTransferFunction() const;
    std::size_t GetTransferFunctionVersion() const;

    // Sensitivity
    void SetSensitivity(std::optional<Core::Sensitivity> sensitivity);
    const std::optional<Core::Sensitivity> &GetSensitivity() const;
    std::size_t GetSensitivityVersion() const;

    // Poles and zeros
    void SetPoleZero(std::optional<Core::PoleZero> pole_zero);
    const std::optional<Core::PoleZero> &GetPoleZero() const;
    std::size_t GetPoleZeroVersion() const;

    // S-parameters
    void SetSParameters(std::optional<Core::SParameters> s_parameters);
    const std::optional<Core::SParameters> &GetSParameters() const;
    std::size_t GetSParametersVersion() const;

    // Truth table
    void SetTruthTable(std::optional<Core::TruthTable> truth_table);
    const std::optional<Core::TruthTable> &GetTruthTable() const;
    std::size_t GetTruthTableVersion() const;

    // Harmonic balance
    void SetHarmonicBalance(std::optional<Core::HarmonicBalance> harmonic_balance);
    const std::optional<Core::HarmonicBalance> &GetHarmonicBalance() const;
    std::size_t GetHarmonicBalanceVersion() const;

private:
    // Results, dropped when the circuit changes
    /// The operating point the views read; for a stepped run, its selected step.
    std::optional<Core::OperatingPoint> m_OperatingPoint;
    /// Every step of the last stepped operating point; m_OperatingPoint holds the selected one.
    std::optional<StepFamily<Core::OperatingPoint>> m_OperatingPointSteps;
    /// The transient the views read; for a stepped run, its selected step.
    std::optional<Core::Transient> m_Transient;
    /// Every step of a stepped transient, or none.
    std::optional<StepFamily<Core::Transient>> m_TransientSteps;
    /// The AC sweep the views read; for a stepped run, its selected step.
    std::optional<Core::ACSweep> m_ACSweep;
    /// Every step of a stepped AC sweep, or none.
    std::optional<StepFamily<Core::ACSweep>> m_ACSweepSteps;
    std::optional<Core::DCSweep> m_DCSweep;
    /// What every run of the last Monte Carlo analysis drew and measured, or none.
    std::optional<MonteCarloTable> m_MonteCarlo;
    std::optional<Core::Noise> m_Noise;
    std::optional<Core::TransferFunction> m_TransferFunction;
    std::optional<Core::Sensitivity> m_Sensitivity;
    std::optional<Core::PoleZero> m_PoleZero;
    std::optional<Core::SParameters> m_SParameters;
    std::optional<Core::TruthTable> m_TruthTable;
    std::optional<Core::HarmonicBalance> m_HarmonicBalance;

    // Result versions, which change with every new result, so windows can tell a rerun from one they showed
    std::size_t m_OperatingPointVersion = 0;
    std::size_t m_TransientVersion = 0;
    /// Also counts the chunks added to a transient that is still being made.
    std::size_t m_TransientDataVersion = 0;
    std::size_t m_ACSweepVersion = 0;
    std::size_t m_DCSweepVersion = 0;
    std::size_t m_MonteCarloVersion = 0;
    std::size_t m_NoiseVersion = 0;
    std::size_t m_TransferFunctionVersion = 0;
    std::size_t m_SensitivityVersion = 0;
    std::size_t m_PoleZeroVersion = 0;
    std::size_t m_SParametersVersion = 0;
    std::size_t m_TruthTableVersion = 0;
    std::size_t m_HarmonicBalanceVersion = 0;

    void ClearMonteCarlo();
};

} // namespace GUI

#endif // IMCSIM_RESULT_STORE_H
