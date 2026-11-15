/**
 * @file    nodalys_in_use.cpp
 * @brief   Puts Nodalys in use as the simulation backend for the length of a scope in a test.
 */

#include "nodalys_in_use.h"

#include "backends/nodalys/nodalys_backend.h"
#include "simulation/simulation_backend.h"
#include <memory>

namespace TestSupport {

NodalysInUse::NodalysInUse() {
    Core::SetBackend(std::make_unique<Core::NodalysBackend>());
}

NodalysInUse::~NodalysInUse() {
    Core::SetBackend(nullptr);
}

} // namespace TestSupport
