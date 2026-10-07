/**
 * @file    voltage_source.h
 * @brief   An independent voltage source in the simulation model.
 */

#ifndef IMCSIM_VOLTAGE_SOURCE_H
#define IMCSIM_VOLTAGE_SOURCE_H

#include "component.h"

namespace Core {

/**
 * @class   VoltageSource
 * @brief   An independent voltage source between two nodes, positive on the first terminal.
 * @details A DC source holds its voltage in the component value. An AC source is a sine of amplitude,
 *          frequency and offset instead, and the component value is kept but not used.
 */
class VoltageSource : public Component {
public:
    /**
     * @enum    SourceType
     * @brief   Waveform of the source: direct or alternating current.
     */
    enum class SourceType {
        DC,
        AC
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

private:
    SourceType m_Type{SourceType::DC};
    double m_Amplitude{};
    double m_Frequency{};
    double m_Offset{};
};

// Source type names
const char *GetSourceTypeName(VoltageSource::SourceType type);
std::optional<VoltageSource::SourceType> ParseSourceType(std::string_view text);

} // namespace Core

#endif // IMCSIM_VOLTAGE_SOURCE_H
