/**
 * @file    source.cpp
 * @brief   Independent sources with a DC, sine, pulse, exponential, single-frequency FM, amplitude-modulated or
 *          piecewise linear waveform, shared by voltage and current sources.
 */

#include "source.h"

#include "spice_value.h"
#include <cstddef>
#include <format>

namespace Core {

namespace {

// Splits a text of "time value" pairs on commas and whitespace
std::vector<std::string_view> SplitPWLTokens(const std::string_view text) {
    std::vector<std::string_view> tokens;
    std::string_view rest = text;
    while (true) {
        const std::size_t start = rest.find_first_not_of(" \t\r\n,");
        if (start == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(start);
        const std::size_t end = rest.find_first_of(" \t\r\n,");
        tokens.push_back(rest.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(end);
    }
    return tokens;
}

} // namespace

/**
 * @brief   Creates a DC source; only derived sources call it, with defaults in their own unit.
 * @param[in] type        Kind of source.
 * @param[in] value       DC level.
 * @param[in] amplitude   Peak of the sine, the SFFM and the AM, which run at 1 kHz from 0.
 * @param[in] pulse_high  High level of the pulse and peak of the exponential; the PWL points peak there too.
 */
Source::Source(const ComponentType type, const double value, const double amplitude, const double pulse_high)
    : Component(type, value) {
    m_AC.Amplitude = amplitude;
    m_Pulse.High = pulse_high;
    m_EXP.PeakValue = pulse_high;
    m_SFFM.Amplitude = amplitude;
    m_AM.Amplitude = amplitude;
    m_PWL.Points = {{0.0, 0.0}, {1e-3, pulse_high}, {2e-3, 0.0}};
}

/**
 * @brief   Returns the waveform of the source.
 * @return  The current source type.
 */
Source::SourceType Source::GetSourceType() const {
    return m_Type;
}

/**
 * @brief   Switches the waveform of the source, keeping the parameters of every type.
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
 * @param[in] ac  New amplitude, frequency, offset and AC magnitude; check them with IsValidAC() first.
 */
void Source::SetAC(const ACParameters &ac) {
    m_AC = ac;
}

/**
 * @brief   Checks whether a sine can be simulated.
 * @param[in] ac  Candidate amplitude, frequency, offset and AC magnitude.
 * @return  True when the frequency is positive. The amplitude, offset and magnitude can be any value; a negative
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
 * @brief   Returns the shape of the exponential.
 * @return  Levels in the unit of the source and times in seconds.
 */
const EXPParameters &Source::GetEXP() const {
    return m_EXP;
}

/**
 * @brief   Changes the shape of the exponential.
 * @param[in] exp  New levels and times; check them with IsValidEXP() first.
 */
void Source::SetEXP(const EXPParameters &exp) {
    m_EXP = exp;
}

/**
 * @brief   Checks whether an exponential can be simulated.
 * @param[in] exp  Candidate levels and times.
 * @return  True when the delays are not negative, the fall does not start before the rise and both time constants
 *          are positive. Levels can be any value.
 */
bool Source::IsValidEXP(const EXPParameters &exp) const {
    return exp.RiseDelay >= 0.0 && exp.FallDelay >= exp.RiseDelay && exp.RiseTimeConstant > 0.0 &&
           exp.FallTimeConstant > 0.0;
}

/**
 * @brief   Returns the shape of the single-frequency FM.
 * @return  Levels in the unit of the source and frequencies in hertz.
 */
const SFFMParameters &Source::GetSFFM() const {
    return m_SFFM;
}

/**
 * @brief   Changes the shape of the single-frequency FM.
 * @param[in] sffm  New levels and frequencies; check them with IsValidSFFM() first.
 */
void Source::SetSFFM(const SFFMParameters &sffm) {
    m_SFFM = sffm;
}

/**
 * @brief   Checks whether a single-frequency FM can be simulated.
 * @param[in] sffm  Candidate levels and frequencies.
 * @return  True when the carrier and signal frequencies are positive. The offset, amplitude and modulation index can
 *          be any value.
 */
bool Source::IsValidSFFM(const SFFMParameters &sffm) const {
    return sffm.CarrierFrequency > 0.0 && sffm.SignalFrequency > 0.0;
}

/**
 * @brief   Returns the shape of the amplitude modulation.
 * @return  Levels in the unit of the source, frequencies in hertz and the delay in seconds.
 */
const AMParameters &Source::GetAM() const {
    return m_AM;
}

/**
 * @brief   Changes the shape of the amplitude modulation.
 * @param[in] am  New levels, frequencies and delay; check them with IsValidAM() first.
 */
void Source::SetAM(const AMParameters &am) {
    m_AM = am;
}

/**
 * @brief   Checks whether an amplitude modulation can be simulated.
 * @param[in] am  Candidate levels, frequencies and delay.
 * @return  True when the carrier and modulating frequencies are positive and the delay is not negative. The offset
 *          and amplitude can be any value.
 */
bool Source::IsValidAM(const AMParameters &am) const {
    return am.CarrierFrequency > 0.0 && am.ModulatingFrequency > 0.0 && am.Delay >= 0.0;
}

/**
 * @brief   Returns the piecewise linear points.
 * @return  Times in seconds and values in the unit of the source.
 */
const PWLParameters &Source::GetPWL() const {
    return m_PWL;
}

/**
 * @brief   Changes the piecewise linear points.
 * @param[in] pwl  New points; check them with IsValidPWL() first.
 */
void Source::SetPWL(const PWLParameters &pwl) {
    m_PWL = pwl;
}

/**
 * @brief   Checks whether piecewise linear points can be simulated.
 * @param[in] pwl  Candidate points.
 * @return  True when there are at least two points, no time is negative and times strictly increase.
 */
bool Source::IsValidPWL(const PWLParameters &pwl) const {
    if (pwl.Points.size() < 2) {
        return false;
    }
    for (std::size_t index = 0; index < pwl.Points.size(); ++index) {
        const PWLPoint &point = pwl.Points[index];
        if (point.Time < 0.0 || (index > 0 && point.Time <= pwl.Points[index - 1].Time)) {
            return false;
        }
    }
    return true;
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
 * @return  "DC", "AC", "Pulse", "EXP", "SFFM", "AM" or "PWL"; ParseSourceType() reads it back.
 */
const char *GetSourceTypeName(const Source::SourceType type) {
    switch (type) {
    case Source::SourceType::DC:
        return "DC";
    case Source::SourceType::AC:
        return "AC";
    case Source::SourceType::Pulse:
        return "Pulse";
    case Source::SourceType::EXP:
        return "EXP";
    case Source::SourceType::SFFM:
        return "SFFM";
    case Source::SourceType::AM:
        return "AM";
    case Source::SourceType::PWL:
        return "PWL";
    }
    return "Unknown";
}

/**
 * @brief   Finds the source type named by a text.
 * @param[in] text  A name as written by GetSourceTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<Source::SourceType> ParseSourceType(const std::string_view text) {
    for (const Source::SourceType type :
         {Source::SourceType::DC, Source::SourceType::AC, Source::SourceType::Pulse, Source::SourceType::EXP,
          Source::SourceType::SFFM, Source::SourceType::AM, Source::SourceType::PWL}) {
        if (text == GetSourceTypeName(type)) {
            return type;
        }
    }
    return std::nullopt;
}

/**
 * @brief   Reads piecewise linear points from the text of the part editor.
 * @param[in] text  "time value" pairs separated by commas or whitespace, such as "0 0, 1m 5, 2m 0", with SPICE
 *                  suffixes on either number.
 * @return  The points, or no value when a number cannot be read, there are fewer than two points or the times are
 *          negative or not strictly increasing.
 */
std::optional<std::vector<PWLPoint>> ParsePWLPoints(const std::string_view text) {
    const std::vector<std::string_view> tokens = SplitPWLTokens(text);
    if (tokens.size() < 4 || tokens.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<PWLPoint> points;
    points.reserve(tokens.size() / 2);
    for (std::size_t index = 0; index < tokens.size(); index += 2) {
        const std::optional<double> time = ParseValue(tokens[index], "");
        const std::optional<double> value = ParseValue(tokens[index + 1], "");
        if (!time || !value || *time < 0.0 || (!points.empty() && *time <= points.back().Time)) {
            return std::nullopt;
        }
        points.push_back({.Time = *time, .Value = *value});
    }
    return points;
}

/**
 * @brief   Writes piecewise linear points as the text of the part editor.
 * @param[in] points  Points to write.
 * @return  Text such as "0 0, 1m 5, 2m 0"; ParsePWLPoints() reads it back.
 */
std::string FormatPWLPoints(const std::vector<PWLPoint> &points) {
    std::string text;
    for (const PWLPoint &point : points) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::format("{} {}", FormatValue(point.Time), FormatValue(point.Value));
    }
    return text;
}

} // namespace Core
