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

    SourceType GetSourceType() const;

private:
    SourceType m_Type{SourceType::DC};
    double m_Amplitude{};
    double m_Frequency{};
    double m_Offset{};
};

} // namespace Core

#endif // IMCSIM_VOLTAGE_SOURCE_H
