/**
 * @file    nodeset.h
 * @brief   A marker that gives the solver a voltage to start looking for the operating point of its node from.
 */

#ifndef IMCSIM_NODESET_H
#define IMCSIM_NODESET_H

#include "circuit/components/component.h"
#include <memory>

namespace Core {

/**
 * @class   Nodeset
 * @brief   One terminal and a voltage: the node it sits on is where the solver starts its search for the operating
 *          point.
 * @details It has no netlist line and no name, like an initial voltage: the circuit builder turns it into a nodeset of
 *          its node (Circuit::AddNodeset()). The voltage is the value of the part. It is a guess and not a held
 *          voltage: the solver first finds the point with the node pulled to it, then lets go and converges again, so
 *          a circuit with several stable states, such as a latch, ends in the one nearest the guess, and a circuit with
 *          one ends where it always does. Every analysis that starts from an operating point reads it, and a transient
 *          that starts from initial conditions (`uic`) has none to read.
 */
class Nodeset : public Component {
public:
    Nodeset();
    ~Nodeset() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_NODESET_H
