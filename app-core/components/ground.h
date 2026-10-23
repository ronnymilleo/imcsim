/**
 * @file    ground.h
 * @brief   A ground reference (0 V node) in the simulation model.
 */

#ifndef IMCSIM_GROUND_H
#define IMCSIM_GROUND_H

#include "component.h"
#include <memory>

namespace Core {

/**
 * @class   Ground
 * @brief   A ground reference (0 V node) in the simulation model.
 */
class Ground : public Component {
public:
    Ground();
    ~Ground() override = default;

    std::unique_ptr<Component> Clone() const override;
};

} // namespace Core

#endif // IMCSIM_GROUND_H
