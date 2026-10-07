/**
 * @file    voltage_source.h
 * @brief   An independent voltage source in the simulation model.
 */

#ifndef IMCSIM_VOLTAGE_SOURCE_H
#define IMCSIM_VOLTAGE_SOURCE_H

#include "component.h"

namespace Core {

/**
 * @struct  PulseParameters
 * @brief   Shape of a repeating pulse, as in the SPICE PULSE source: times in seconds, levels in volts.
 * @details The source sits at Low for Delay, ramps to High in RiseTime, stays there for Width, ramps back in
 *          FallTime and starts again every Period.
 */
struct PulseParameters {
    double Low = 0.0;
    double High = 5.0;
    double Delay = 0.0;
    double RiseTime = 1e-6;
    double FallTime = 1e-6;
    double Width = 0.5e-3;
    double Period = 1e-3;
};

/**
 * @class   VoltageSource
 * @brief   An independent voltage source between two nodes, positive on the first terminal.
 * @details A DC source holds its voltage in the component value. An AC source is a sine of amplitude,
 *          frequency and offset instead, and a pulse source a repeating pulse; both keep the component value
 *          but do not use it. Every type keeps its parameters while another one is selected.
 */
class VoltageSource : public Component {
public:
    /**
     * @enum    SourceType
     * @brief   Waveform of the source: direct current, a sine or a repeating pulse.
     */
    enum class SourceType {
        DC,
        AC,
        Pulse
    };

    VoltageSource();
    ~VoltageSource() override = default;

    // Waveform
    SourceType GetSourceType() const;
    void SetSourceType(SourceType type);

    // Sine parameters, used when the source is AC
    double GetAmplitude() const;
    void SetAmplitude(double amplitude);
    double GetFrequency() const;
    void SetFrequency(double frequency);
    bool IsValidFrequency(double frequency) const;
    double GetOffset() const;
    void SetOffset(double offset);

    // Pulse parameters, used when the source is a pulse
    const PulseParameters &GetPulse() const;
    void SetPulse(const PulseParameters &pulse);
    bool IsValidPulse(const PulseParameters &pulse) const;

private:
    SourceType m_Type{SourceType::DC};
    double m_Amplitude{};
    double m_Frequency{};
    double m_Offset{};
    PulseParameters m_Pulse;
};

// Source type names
const char *GetSourceTypeName(VoltageSource::SourceType type);
std::optional<VoltageSource::SourceType> ParseSourceType(std::string_view text);

} // namespace Core

#endif // IMCSIM_VOLTAGE_SOURCE_H
