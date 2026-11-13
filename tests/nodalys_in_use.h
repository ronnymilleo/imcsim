/**
 * @file    nodalys_in_use.h
 * @brief   Puts Nodalys in use as the simulation backend for the length of a scope in a test.
 */

#ifndef IMCSIM_NODALYS_IN_USE_H
#define IMCSIM_NODALYS_IN_USE_H

namespace TestSupport {

/**
 * @class   NodalysInUse
 * @brief   Makes Nodalys the backend while it lives and restores the default one, ngspice, afterwards, even when a
 *          check ends the scope early.
 */
class NodalysInUse {
public:
    NodalysInUse();
    ~NodalysInUse();
    NodalysInUse(const NodalysInUse &) = delete;
    NodalysInUse &operator=(const NodalysInUse &) = delete;
};

} // namespace TestSupport

#endif // IMCSIM_NODALYS_IN_USE_H
