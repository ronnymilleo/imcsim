/**
 * @file    source.cpp
 * @brief   Independent sources with a DC, sine or pulse waveform, shared by voltage and current sources.
 */

#include "source.h"

namespace Core {

/**
 * @brief   Creates a DC source; only derived sources call it, with defaults in their own unit.
 * @param[in] type        Kind of source.
 * @param[in] value       DC level.
 * @param[in] amplitude   Peak of the sine, which runs at 1 kHz without offset.
 * @param[in] pulse_high  High level of the pulse, which starts from 0 and repeats at 1 kHz.
 */
Source::Source(const ComponentType type, const double value, const double amplitude, const double pulse_high)
    : Component(type, value) {
    m_AC.Amplitude = amplitude;
    m_Pulse.High = pulse_high;
}

/**
 * @brief   Returns whether the source is DC or AC.
 * @return  The current source type.
 */
Source::SourceType Source::GetSourceType() const {
    return m_Type;
}

/**
 * @brief   Switches the source between DC and AC, keeping the parameters of both.
 * @param[in] type  New source type.
 */
void Source::SetSourceType(const SourceType type) {
    m_Type = type;
}

/**
 * @brief   Returns the sine of the source.
 * @return  Amplitude and offset in the unit of the source, frequency in hertz.
 */
const ACParameters &Source::GetAC() const {
    return m_AC;
}

/**
 * @brief   Changes the sine of the source.
 * @param[in] ac  New amplitude, frequency and offset; check them with IsValidAC() first.
 */
void Source::SetAC(const ACParameters &ac) {
    m_AC = ac;
}

/**
 * @brief   Checks whether a sine can be simulated.
 * @param[in] ac  Candidate amplitude, frequency and offset.
 * @return  True when the frequency is positive. The amplitude and offset can be any value; a negative
 *          amplitude inverts the sine.
 */
bool Source::IsValidAC(const ACParameters &ac) const {
    return ac.Frequency > 0.0;
}

/**
 * @brief   Returns the shape of the pulse.
 * @return  Levels in the unit of the source and times in seconds.
 */
const PulseParameters &Source::GetPulse() const {
    return m_Pulse;
}

/**
 * @brief   Changes the shape of the pulse.
 * @param[in] pulse  New levels and times; check them with IsValidPulse() first.
 */
void Source::SetPulse(const PulseParameters &pulse) {
    m_Pulse = pulse;
}

/**
 * @brief   Checks whether a pulse can be simulated.
 * @param[in] pulse  Candidate levels and times.
 * @return  True when no time is negative and the period is positive. Levels can be any value.
 * @note    A period shorter than the rise, width and fall together is accepted, as ngspice does, but the pulse
 *          then never reaches its full shape.
 */
bool Source::IsValidPulse(const PulseParameters &pulse) const {
    return pulse.Delay >= 0.0 && pulse.RiseTime >= 0.0 && pulse.FallTime >= 0.0 && pulse.Width >= 0.0 &&
           pulse.Period > 0.0;
}

/**
 * @brief   Tells whether a kind of component is a Source, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for voltage and current sources. The VCC rail is a fixed DC supply, not a Source.
 */
bool IsSource(const ComponentType type) {
    return type == ComponentType::VoltageSource || type == ComponentType::CurrentSource;
}

/**
 * @brief   Returns a readable name for a source type.
 * @param[in] type  Source type.
 * @return  "DC", "AC" or "Pulse"; ParseSourceType() reads it back.
 */
const char *GetSourceTypeName(const Source::SourceType type) {
    switch (type) {
    case Source::SourceType::DC:
        return "DC";
    case Source::SourceType::AC:
        return "AC";
    case Source::SourceType::Pulse:
        return "Pulse";
    }
    return "Unknown";
}

/**
 * @brief   Finds the source type named by a text.
 * @param[in] text  A name as written by GetSourceTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<Source::SourceType> ParseSourceType(const std::string_view text) {
    for (const Source::SourceType type : {Source::SourceType::DC, Source::SourceType::AC, Source::SourceType::Pulse}) {
        if (text == GetSourceTypeName(type)) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace Core
