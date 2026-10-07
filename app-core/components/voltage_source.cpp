/**
 * @file    voltage_source.cpp
 * @brief   An independent voltage source in the simulation model.
 */

#include "voltage_source.h"

namespace Core {

namespace {

constexpr double DefaultVoltage = 5.0;
constexpr double DefaultAmplitude = 1.0;
constexpr double DefaultFrequency = 1e3;

} // namespace

/**
 * @brief   Creates a DC voltage source with the default voltage.
 * @note    The sine parameters start at 1 V and 1 kHz without offset, ready for switching to AC.
 */
VoltageSource::VoltageSource()
    : Component(ComponentType::VoltageSource, DefaultVoltage), m_Amplitude(DefaultAmplitude),
      m_Frequency(DefaultFrequency) {
}

/**
 * @brief   Returns whether the source is DC or AC.
 * @return  The current source type.
 */
VoltageSource::SourceType VoltageSource::GetSourceType() const {
    return m_Type;
}

/**
 * @brief   Switches the source between DC and AC, keeping the parameters of both.
 * @param[in] type  New source type.
 */
void VoltageSource::SetSourceType(const SourceType type) {
    m_Type = type;
}

/**
 * @brief   Returns the peak amplitude of the sine.
 * @return  Amplitude in volts.
 */
double VoltageSource::GetAmplitude() const {
    return m_Amplitude;
}

/**
 * @brief   Sets the peak amplitude of the sine.
 * @param[in] amplitude  Amplitude in volts; any value, a negative one inverts the sine.
 */
void VoltageSource::SetAmplitude(const double amplitude) {
    m_Amplitude = amplitude;
}

/**
 * @brief   Returns the frequency of the sine.
 * @return  Frequency in hertz.
 */
double VoltageSource::GetFrequency() const {
    return m_Frequency;
}

/**
 * @brief   Sets the frequency of the sine.
 * @param[in] frequency  Frequency in hertz; check it with IsValidFrequency() first.
 */
void VoltageSource::SetFrequency(const double frequency) {
    m_Frequency = frequency;
}

/**
 * @brief   Checks whether a frequency can be simulated.
 * @param[in] frequency  Candidate frequency in hertz.
 * @return  True when it is positive.
 */
bool VoltageSource::IsValidFrequency(const double frequency) const {
    return frequency > 0.0;
}

/**
 * @brief   Returns the DC level the sine oscillates around.
 * @return  Offset in volts.
 */
double VoltageSource::GetOffset() const {
    return m_Offset;
}

/**
 * @brief   Sets the DC level the sine oscillates around.
 * @param[in] offset  Offset in volts.
 */
void VoltageSource::SetOffset(const double offset) {
    m_Offset = offset;
}

/**
 * @brief   Returns a readable name for a source type.
 * @param[in] type  Source type.
 * @return  "DC" or "AC"; ParseSourceType() reads it back.
 */
const char *GetSourceTypeName(const VoltageSource::SourceType type) {
    switch (type) {
    case VoltageSource::SourceType::DC:
        return "DC";
    case VoltageSource::SourceType::AC:
        return "AC";
    }
    return "Unknown";
}

/**
 * @brief   Finds the source type named by a text.
 * @param[in] text  A name as written by GetSourceTypeName(), case sensitive.
 * @return  The matching type, or no value for an unknown name.
 */
std::optional<VoltageSource::SourceType> ParseSourceType(const std::string_view text) {
    for (const VoltageSource::SourceType type : {VoltageSource::SourceType::DC, VoltageSource::SourceType::AC}) {
        if (text == GetSourceTypeName(type)) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace Core
