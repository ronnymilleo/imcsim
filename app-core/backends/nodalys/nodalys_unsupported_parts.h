/**
 * @file    nodalys_unsupported_parts.h
 * @brief   The parts each analysis of Nodalys cannot simulate, with the reason shown to the user.
 */

#ifndef IMCSIM_NODALYS_UNSUPPORTED_PARTS_H
#define IMCSIM_NODALYS_UNSUPPORTED_PARTS_H

#include "simulation/simulation_backend.h"

namespace Core {

void AddNodalysUnsupportedParts(BackendCapabilities &capabilities);

} // namespace Core

#endif // IMCSIM_NODALYS_UNSUPPORTED_PARTS_H
