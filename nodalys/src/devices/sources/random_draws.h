/**
 * @file    random_draws.h
 * @brief   Random draws written out by hand, so a seed gives the same numbers on every platform.
 *
 * @details The standard distributions (`std::normal_distribution` and the others) are not specified to the bit, so
 *          libstdc++, libc++ and MSVC draw different values from one engine state. These functions turn the bits of a
 *          `std::mt19937_64`, which the standard does specify, into numbers with fixed arithmetic. The only
 *          platform-dependent part is the last digit of `log`, `sqrt` and `cos`.
 */

#ifndef NODALYS_RANDOM_DRAWS_H
#define NODALYS_RANDOM_DRAWS_H

#include <random>

namespace Nodalys {

// Draws
double DrawUniform(std::mt19937_64 &engine);
double DrawStandardNormal(std::mt19937_64 &engine);
double DrawExponential(std::mt19937_64 &engine, double mean);
int DrawPoisson(std::mt19937_64 &engine, double lambda);

} // namespace Nodalys

#endif // NODALYS_RANDOM_DRAWS_H
