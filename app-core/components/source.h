/**
 * @file    source.h
 * @brief   Independent sources with a DC, sine or pulse waveform, shared by voltage and current sources.
 */

#ifndef IMCSIM_SOURCE_H
#define IMCSIM_SOURCE_H

#include "component.h"

namespace Core {

/**
 * @struct  ACParameters
 * @brief   An AC source: the sine it makes in a transient, as in the SPICE SIN source, and its magnitude in an AC
 *          sweep, as the SPICE AC value; amplitude, offset and magnitude in the unit of the source, frequency in
 *          hertz.
 * @details The amplitude is the peak of the sine. The magnitude is apart from it, so a transient can use a small,
 *          realistic signal while the AC sweep, which is linear, uses 1 and reads directly as gain.
 */
struct ACParameters {
    double Amplitude = 1.0;
    double Frequency = 1e3;
    double Offset = 0.0;
    double Magnitude = 1.0;
};

/**
 * @struct  PulseParameters
 * @brief   Shape of a repeating pulse, as in the SPICE PULSE source: times in seconds, levels in the unit of
 *          the source.
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
 * @class   Source
 * @brief   An independent source between two nodes, whose value follows a DC level, a sine or a pulse.
 * @details A DC source holds its level in the component value. An AC source is a sine of amplitude, frequency
 *          and offset instead, and a pulse source a repeating pulse; both keep the component value but do not
 *          use it. Every type keeps its parameters while another one is selected. Levels are in the unit of the
 *          derived source, volts or amperes.
 */
class Source : public Component {
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

    ~Source() override = default;

    // Waveform
    SourceType GetSourceType() const;
    void SetSourceType(SourceType type);

    // Sine parameters, used when the source is AC
    const ACParameters &GetAC() const;
    void SetAC(const ACParameters &ac);
    bool IsValidAC(const ACParameters &ac) const;

    // Pulse parameters, used when the source is a pulse
    const PulseParameters &GetPulse() const;
    void SetPulse(const PulseParameters &pulse);
    bool IsValidPulse(const PulseParameters &pulse) const;

protected:
    Source(ComponentType type, double value, double amplitude, double pulse_high);

private:
    SourceType m_Type{SourceType::DC};
    ACParameters m_AC;
    PulseParameters m_Pulse;
};

// Sources
bool IsSource(ComponentType type);
const char *GetSourceTypeName(Source::SourceType type);
std::optional<Source::SourceType> ParseSourceType(std::string_view text);

} // namespace Core

#endif // IMCSIM_SOURCE_H
