/**
 * @file    trace_math_samples.h
 * @brief   Where the traces of a math channel come from: the samples of a transient or a DC sweep, or the phasors of an
 *          AC sweep.
 */

#ifndef IMCSIM_TRACE_MATH_SAMPLES_H
#define IMCSIM_TRACE_MATH_SAMPLES_H

#include "results/trace_math_units.h"
#include "simulation/simulator.h"
#include <complex>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @class   RealSamples
 * @brief   The samples of a transient or a DC sweep, where every trace is a real value at each X value.
 */
class RealSamples {
public:
    /// A real sample, one per X value.
    using Value = double;

    RealSamples(std::span<const double> xs,
                Dimension x_unit,
                const std::vector<std::vector<double>> &node_voltages,
                const std::vector<Core::ComponentTrace> &currents);

    std::size_t GetCount() const;
    Dimension GetXUnit() const;
    [[nodiscard]] std::expected<std::vector<double>, std::string> GetVoltage(int node) const;
    [[nodiscard]] std::expected<std::vector<double>, std::string> GetCurrent(std::string_view name) const;
    std::vector<double> GetDerivative(const std::vector<double> &values) const;
    std::vector<double> GetIntegral(const std::vector<double> &values) const;

private:
    std::span<const double> m_Xs;
    Dimension m_XUnit;
    const std::vector<std::vector<double>> &m_NodeVoltages;
    const std::vector<Core::ComponentTrace> &m_Currents;
};

/**
 * @class   PhasorSamples
 * @brief   The samples of an AC sweep, where every trace is a phasor at each frequency, rebuilt from its magnitude in
 *          decibels and its phase in degrees.
 * @details A derivative multiplies by j*omega and an integral divides by it, which is what they are on a sine wave,
 *          so both stay exact at every frequency.
 */
class PhasorSamples {
public:
    /// A phasor, one per frequency.
    using Value = std::complex<double>;

    explicit PhasorSamples(const Core::ACSweep &sweep);

    std::size_t GetCount() const;
    static Dimension GetXUnit();
    [[nodiscard]] std::expected<std::vector<Value>, std::string> GetVoltage(int node) const;
    [[nodiscard]] std::expected<std::vector<Value>, std::string> GetCurrent(std::string_view name) const;
    std::vector<Value> GetDerivative(std::vector<Value> values) const;
    std::vector<Value> GetIntegral(std::vector<Value> values) const;

private:
    const Core::ACSweep &m_Sweep;

    Value GetJOmega(std::size_t index) const;
};

/**
 * @struct  PhasorFigures
 * @brief   A phasor as an AC sweep stores it: its magnitude in decibels and its phase in degrees.
 */
struct PhasorFigures {
    double MagnitudeDecibels = 0.0;
    double PhaseDegrees = 0.0;
};

// Phasors
std::vector<std::complex<double>> ToPhasors(const std::vector<double> &magnitudes_decibels,
                                            const std::vector<double> &phases_degrees);
PhasorFigures ToDecibelsAndDegrees(std::complex<double> phasor);

} // namespace GUI

#endif // IMCSIM_TRACE_MATH_SAMPLES_H
