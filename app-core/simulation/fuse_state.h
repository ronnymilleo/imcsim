/**
 * @file    fuse_state.h
 * @brief   Tells from the results of a transient whether and when a fuse blew.
 * @details Read from the voltage across the fuse and the current through it, so every simulation backend shares it.
 */

#ifndef IMCSIM_FUSE_STATE_H
#define IMCSIM_FUSE_STATE_H

#include "circuit/components/component.h"
#include "simulation/simulation_results.h"
#include <optional>
#include <span>

namespace Core {

std::optional<double> FindFuseBlowTime(const Component &part, std::span<const int> nodes, const Transient &result);

} // namespace Core

#endif // IMCSIM_FUSE_STATE_H
