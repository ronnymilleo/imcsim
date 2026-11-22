/**
 * @file    independent_sources.h
 * @brief   Independent voltage and current sources and their waveform periodicity metadata.
 */

#ifndef NODALYS_INDEPENDENT_SOURCES_H
#define NODALYS_INDEPENDENT_SOURCES_H

#include "devices/device.h"
#include <functional>

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
 * @class   VoltageSourceDevice
 * @brief   An independent voltage source between two nodes as a branch unknown.
 * @details Its value comes from a waveform function of time, or from a settable DC level: a DC sweep overrides it
 *          with the sweep value and a live run with the changed value. At the operating point the waveform sources
 *          report the level they start from, like the netlist writes them, so the operating point matches the start
 *          of the transient.
 */
class VoltageSourceDevice : public Device {
public:
    /// Value of the source at the stamped point, given its DC level and the time context.
    using Waveform = std::function<double(double dc_value, const TimeContext &time)>;
    /// Next corner of the waveform strictly after an instant, or infinity; see Device::NextBreakpoint().
    using Breakpoints = std::function<double(double time)>;

    /**
     * @struct  CallbackSet
     * @brief   Fresh waveform, breakpoint and seed callbacks created for an isolated clone of a stateful source.
     */
    struct CallbackSet {
        Waveform Evaluate;
        Breakpoints NextBreakpoint;
        std::function<void(std::uint64_t)> SetSeed;
    };

    /// Creates an independent CallbackSet when cloning a source whose waveform carries mutable state (e.g. TRNOISE).
    using CallbackFactory = std::function<CallbackSet()>;

    VoltageSourceDevice(int node_a,
                        int node_b,
                        int branch,
                        double dc_value,
                        Waveform waveform,
                        double ac_magnitude = 0.0,
                        SourcePeriodicity periodicity = {});

    double GetDcValue() const;
    void SetDcValue(double value);

    double GetAcMagnitude() const { return m_AcMagnitude; }

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    int GetBranch() const { return m_Branch; }

    const SourcePeriodicity &GetPeriodicity() const { return m_Periodicity; }

    void SetBreakpoints(Breakpoints breakpoints);
    void SetSeedCallback(std::function<void(std::uint64_t)> set_seed);
    void SetCallbackFactory(CallbackFactory factory);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool SupportsHarmonicBalance() const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void SetSourceScale(double scale) override;
    double NextBreakpoint(double time) const override;
    void SetRandomSeed(std::uint64_t seed) override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    int m_Branch;
    double m_DcValue;
    Waveform m_Waveform;
    Breakpoints m_Breakpoints;
    std::function<void(std::uint64_t)> m_SetSeed;
    CallbackFactory m_CallbackFactory;
    double m_AcMagnitude;
    SourcePeriodicity m_Periodicity;
    double m_Scale = 1.0;
};

/**
 * @class   CurrentSourceDevice
 * @brief   An independent current source between two nodes, positive from the first node to the second.
 */
class CurrentSourceDevice : public Device {
public:
    CurrentSourceDevice(int node_a,
                        int node_b,
                        double dc_value,
                        VoltageSourceDevice::Waveform waveform,
                        double ac_magnitude = 0.0,
                        SourcePeriodicity periodicity = {});

    double GetDcValue() const;
    void SetDcValue(double value);

    double GetAcMagnitude() const { return m_AcMagnitude; }

    int GetNodeA() const { return m_NodeA; }

    int GetNodeB() const { return m_NodeB; }

    const SourcePeriodicity &GetPeriodicity() const { return m_Periodicity; }

    double SamplePeriodicCurrent(int terminal, double time_seconds) const;
    void SetBreakpoints(VoltageSourceDevice::Breakpoints breakpoints);
    void SetSeedCallback(std::function<void(std::uint64_t)> set_seed);
    void SetCallbackFactory(VoltageSourceDevice::CallbackFactory factory);

    void Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) override;
    void StampAc(MnaSystem &reactive, const std::vector<double> &x_dc) const override;
    bool SupportsHarmonicBalance() const override;
    double TerminalCurrent(int terminal, const std::vector<double> &x) const override;
    std::complex<double> AcTerminalCurrent(int terminal,
                                           const std::vector<double> &x_dc,
                                           const std::vector<std::complex<double>> &x_ac,
                                           double omega) const override;
    void SetSourceScale(double scale) override;
    double NextBreakpoint(double time) const override;
    void SetRandomSeed(std::uint64_t seed) override;
    [[nodiscard]] std::unique_ptr<Device> Clone() const override;

private:
    int m_NodeA;
    int m_NodeB;
    double m_DcValue;
    VoltageSourceDevice::Waveform m_Waveform;
    VoltageSourceDevice::Breakpoints m_Breakpoints;
    std::function<void(std::uint64_t)> m_SetSeed;
    VoltageSourceDevice::CallbackFactory m_CallbackFactory;
    double m_AcMagnitude;
    SourcePeriodicity m_Periodicity;
    double m_Scale = 1.0;
    double m_Value = 0.0;
};

} // namespace Nodalys

#endif // NODALYS_INDEPENDENT_SOURCES_H
