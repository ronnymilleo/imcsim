/**
 * @file    current_source.h
 * @brief   An independent current source in the simulation model.
 */

#ifndef IMCSIM_CURRENT_SOURCE_H
#define IMCSIM_CURRENT_SOURCE_H

#include "source.h"

namespace Core {

/**
 * @class   CurrentSource
 * @brief   An independent current source between two nodes.
 * @details As in SPICE, a positive current flows through the source from its first terminal to its second, so
 *          it leaves the source at the second terminal and is pushed into the node there.
 */
class CurrentSource : public Source {
public:
    CurrentSource();
    ~CurrentSource() override = default;
};

} // namespace Core

#endif // IMCSIM_CURRENT_SOURCE_H
