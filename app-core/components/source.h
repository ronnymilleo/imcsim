/**
 * @file    source.h
 * @brief   Independent sources with a DC, sine, pulse, exponential, single-frequency FM, amplitude-modulated or
 *          piecewise linear waveform, shared by voltage and current sources.
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
 * @struct  EXPParameters
 * @brief   Shape of an exponential source, as in the SPICE EXP source: values in the unit of the source, times in
 *          seconds.
 * @details The source starts at InitialValue, rises towards PeakValue with time constant RiseTimeConstant from
 *          RiseDelay on, then decays back towards InitialValue with time constant FallTimeConstant from FallDelay on.
 */
struct EXPParameters {
    double InitialValue = 0.0;
    double PeakValue = 5.0;
    double RiseDelay = 1e-3;
    double RiseTimeConstant = 0.5e-3;
    double FallDelay = 5e-3;
    double FallTimeConstant = 1e-3;
};

/**
 * @struct  SFFMParameters
 * @brief   Shape of a single-frequency FM source, as in the SPICE SFFM source: levels in the unit of the source
 *          and frequencies in hertz.
 * @details The source is Offset plus Amplitude times a carrier at CarrierFrequency whose phase swings by
 *          ModulationIndex times a sine at SignalFrequency.
 */
struct SFFMParameters {
    double Offset = 0.0;
    double Amplitude = 1.0;
    double CarrierFrequency = 1e3;
    double ModulationIndex = 5.0;
    double SignalFrequency = 100.0;
};

/**
 * @struct  AMParameters
 * @brief   Shape of an amplitude-modulated source, as in the SPICE AM source: levels in the unit of the source and
 *          frequencies in hertz.
 * @details The source is Offset plus Amplitude times a sine at ModulatingFrequency times a sine at
 *          CarrierFrequency, both measured from Delay on; until the delay the transient holds it at 0, while the
 *          operating point stays at the offset.
 */
struct AMParameters {
    double Amplitude = 1.0;
    double Offset = 0.0;
    double ModulatingFrequency = 100.0;
    double CarrierFrequency = 1e3;
    double Delay = 0.0;
};

/**
 * @struct  PWLPoint
 * @brief   One point of a piecewise linear source: a time in seconds and the value the source holds there, in the
 *          unit of the source.
 */
struct PWLPoint {
    double Time = 0.0;
    double Value = 0.0;
};

/**
 * @struct  PWLParameters
 * @brief   Shape of a piecewise linear source, as in the SPICE PWL source: the value at each time, joined by straight
 *          lines and held at the first and last value outside the points.
 * @details The points are ordered by time; at least two are needed, times are not negative and strictly increase.
 */
struct PWLParameters {
    std::vector<PWLPoint> Points = {{0.0, 0.0}, {1e-3, 5.0}, {2e-3, 0.0}};
};

/**
 * @class   Source
 * @brief   An independent source between two nodes, whose value follows one of seven waveforms.
 * @details A DC source holds its level in the component value. The other types keep it but do not use it, holding
 *          their own parameters instead: a sine (AC), a repeating pulse, an exponential, a single-frequency FM, an
 *          amplitude modulation or piecewise linear points. Every type keeps its parameters while another one is
 *          selected. Levels are in the unit of the derived source, volts or amperes.
 */
class Source : public Component {
public:
    /**
     * @enum    SourceType
     * @brief   Waveform of the source: direct current, a sine, a repeating pulse, an exponential, a
     *          single-frequency FM, an amplitude modulation or piecewise linear points.
     */
    enum class SourceType {
        DC,
        AC,
        Pulse,
        EXP,
        SFFM,
        AM,
        PWL
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

    // Exponential parameters, used when the source is an exponential
    const EXPParameters &GetEXP() const;
    void SetEXP(const EXPParameters &exp);
    bool IsValidEXP(const EXPParameters &exp) const;

    // Single-frequency FM parameters, used when the source is an SFFM
    const SFFMParameters &GetSFFM() const;
    void SetSFFM(const SFFMParameters &sffm);
    bool IsValidSFFM(const SFFMParameters &sffm) const;

    // Amplitude modulation parameters, used when the source is an AM
    const AMParameters &GetAM() const;
    void SetAM(const AMParameters &am);
    bool IsValidAM(const AMParameters &am) const;

    // Piecewise linear parameters, used when the source is a PWL
    const PWLParameters &GetPWL() const;
    void SetPWL(const PWLParameters &pwl);
    bool IsValidPWL(const PWLParameters &pwl) const;

protected:
    Source(ComponentType type, double value, double amplitude, double pulse_high);

private:
    SourceType m_Type{SourceType::DC};
    ACParameters m_AC;
    PulseParameters m_Pulse;
    EXPParameters m_EXP;
    SFFMParameters m_SFFM;
    AMParameters m_AM;
    PWLParameters m_PWL;
};

// Sources
bool IsSource(ComponentType type);
const char *GetSourceTypeName(Source::SourceType type);
std::optional<Source::SourceType> ParseSourceType(std::string_view text);

// Piecewise linear points, edited as one text of "time value" pairs such as "0 0, 1m 5, 2m 0"
std::optional<std::vector<PWLPoint>> ParsePWLPoints(std::string_view text);
std::string FormatPWLPoints(const std::vector<PWLPoint> &points);

} // namespace Core

#endif // IMCSIM_SOURCE_H
