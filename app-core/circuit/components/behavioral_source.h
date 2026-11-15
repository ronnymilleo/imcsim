/**
 * @file    behavioral_source.h
 * @brief   Sources whose voltage or current is a free expression of node voltages and currents (SPICE B).
 */

#ifndef IMCSIM_BEHAVIORAL_SOURCE_H
#define IMCSIM_BEHAVIORAL_SOURCE_H

#include "circuit/components/component.h"
#include <memory>
#include <string>

namespace Core {

/**
 * @class   BehavioralSource
 * @brief   A source whose output is an expression, such as V(IN)*V(LO) or 1m*tanh(V(IN)/26m), written as a SPICE B
 *          element.
 * @details The expression names voltages by net, V(OUT) or V(IN, OUT), and currents as the results name them, I(R1) or
 *          I(Q1.C), so it survives edits that number the nodes again. A voltage source drives its expression from its
 *          + terminal to its - terminal; a current source drives it through itself from + to -, as an independent
 *          current source does. The functions and operators are those of ngspice, which checks them when it runs.
 */
class BehavioralSource : public Component {
public:
    explicit BehavioralSource(ComponentType type);
    ~BehavioralSource() override = default;

    std::unique_ptr<Component> Clone() const override;

    const std::string &GetExpression() const;
    void SetExpression(std::string expression);

private:
    std::string m_Expression;
};

// Kinds
bool IsBehavioralSource(ComponentType type);

} // namespace Core

#endif // IMCSIM_BEHAVIORAL_SOURCE_H
