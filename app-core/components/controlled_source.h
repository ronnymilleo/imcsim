/**
 * @file    controlled_source.h
 * @brief   Sources whose output follows a voltage or a current elsewhere in the circuit (SPICE E, F, G and H).
 */

#ifndef IMCSIM_CONTROLLED_SOURCE_H
#define IMCSIM_CONTROLLED_SOURCE_H

#include "component.h"
#include <string>

namespace Core {

/**
 * @class   ControlledSource
 * @brief   A dependent source: its output is its gain times a controlling voltage or current.
 * @details Voltage-controlled sources (VCVS, VCCS) sense the voltage between two control terminals of their own.
 *          Current-controlled sources (CCCS, CCVS) have only their two output terminals and follow a current the
 *          results report, named as the plots name it, such as "R1" or "Q1.C". The value is the gain, in V/V, A/V,
 *          A/A or V/A, and may be negative.
 */
class ControlledSource : public Component {
public:
    explicit ControlledSource(ComponentType type);
    ~ControlledSource() override = default;

    const std::string &GetControllingCurrent() const;
    void SetControllingCurrent(std::string current);

private:
    std::string m_ControllingCurrent;
};

// Kinds
bool IsControlledSource(ComponentType type);
bool IsCurrentControlled(ComponentType type);

} // namespace Core

#endif // IMCSIM_CONTROLLED_SOURCE_H
