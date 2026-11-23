/**
 * @file    independent_sources.h
 * @brief   Independent voltage and current sources and their waveform periodicity metadata.
 */

#ifndef NODALYS_INDEPENDENT_SOURCES_H
#define NODALYS_INDEPENDENT_SOURCES_H

#include "devices/device.h"
#include "devices/sensitivity.h"
#include "devices/waveforms.h"

namespace Nodalys {

/**
 * @enum    SourceWaveformKind
 * @brief   Classification of an independent source's waveform for Harmonic Balance validation.
 */
enum class SourceWaveformKind {
    DC,
    SINE,
    PULSE,
    CLOCK,
    NON_PERIODIC
};

/**
 * @struct  SourcePeriodicity
 * @brief   Periodicity metadata attached to independent voltage and current sources.
 */
struct SourcePeriodicity {
    SourceWaveformKind Kind = SourceWaveformKind::DC;
    double Frequency = 0.0;
    double MinEdgeTime = 0.0;
    bool IsSupportedInHb = true;
};

/**
 * @class   IndependentSourceDevice
 * @brief   Shared base for independent voltage and current sources, holding nodes, DC/AC levels, waveform callbacks,
 *          periodicity metadata, and source-stepping scale.
 * @details Its value comes from a waveform function of time, or from a settable DC level: a DC sweep overrides it
 *          with the sweep value and a live run with the changed value. At the operating point the waveform sources
 *          report the level they start from, like the netlist writes them, so the operating point matches the start
 *          of the transient.
 */
class IndependentSourceDevice : public Device, public SensitiveDevice {
public:
    virtual bool IsVoltageSource() const = 0;

    double GetDcValue() const;
    void SetDcValue(double value);

    double GetAcMagnitude() const { return m_AcMagnitude; }

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    const SourcePeriodicity &GetPeriodicity() const { return m_Periodicity; }

    void SetBreakpoints(BreakpointCallback breakpoints);
    void SetSeedCallback(SeedCallback set_seed);
    void SetCallbackFactory(SourceCallbackFactory factory);

    bool SupportsHarmonicBalance() const override;
    void SetSourceScale(double scale) override;
    double NextBreakpoint(double time) const override;
    void SetRandomSeed(std::uint64_t seed) override;

    [[nodiscard]] const SensitiveDevice *AsSensitive() const override { return this; }

    [[nodiscard]] IndependentSourceDevice *AsIndependentSource() override { return this; }

    [[nodiscard]] const IndependentSourceDevice *AsIndependentSource() const override { return this; }

protected:
    IndependentSourceDevice(int node_a,
                            int node_b,
                            double dc_value,
                            WaveformCallback waveform,
                            double ac_magnitude,
                            SourcePeriodicity periodicity);

    double EvaluateValue(const TimeContext &time) const;
    void RefreshClonedCallbacks();

private:
    int m_NodeA;
    int m_NodeB;
    double m_DcValue;
    WaveformCallback m_Waveform;
    BreakpointCallback m_Breakpoints;
    SeedCallback m_SetSeed;
    SourceCallbackFactory m_CallbackFactory;
    double m_AcMagnitude;
    SourcePeriodicity m_Periodicity;
    double m_Scale = 1.0;
};

/**
 * @class   CurrentSourceDevice
 * @brief   An independent current source between two nodes, positive from the first node to the second.
 */
class CurrentSourceDevice : public IndependentSourceDevice {
public:
    CurrentSourceDevice(int node_a,
                        int node_b,
                        double dc_value,
                        WaveformCallback waveform,
                        double ac_magnitude = 0.0,
                        SourcePeriodicity periodicity = {});

    bool IsVoltageSource() const override { return false; }

    double SamplePeriodicCurrent(int terminal, double time_seconds) const;

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    double m_Value = 0.0;
};

/**
 * @class   VoltageSourceDevice
 * @brief   An independent voltage source between two nodes as a branch unknown.
 */
class VoltageSourceDevice : public IndependentSourceDevice {
public:
    VoltageSourceDevice(int node_a,
                        int node_b,
                        int branch,
                        double dc_value,
                        WaveformCallback waveform,
                        double ac_magnitude = 0.0,
                        SourcePeriodicity periodicity = {});

    bool IsVoltageSource() const override { return true; }

    int GetBranch() const { return m_Branch; }

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    bool ApplyLiveChange(LiveQuantity quantity, double value) override;
    [[nodiscard]] std::optional<SensitivityDescriptor> DescribeSensitivity() const override;
    [[nodiscard]] double DcSensitivityDerivative(const DcAdjointView &point) const override;
    [[nodiscard]] std::complex<double> AcSensitivityDerivative(const AcAdjointView &point) const override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_Branch;
};

} // namespace Nodalys

#endif // NODALYS_INDEPENDENT_SOURCES_H
