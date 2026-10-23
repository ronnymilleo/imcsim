/**
 * @file    inductor.h
 * @brief   An inductor in the simulation model.
 */

#ifndef IMCSIM_INDUCTOR_H
#define IMCSIM_INDUCTOR_H

#include "component.h"
#include <memory>

namespace Core {

/**
 * @class   Inductor
 * @brief   An inductor in the simulation model.
 */
class Inductor : public Component {
public:
    Inductor();
    ~Inductor() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_INDUCTOR_H
