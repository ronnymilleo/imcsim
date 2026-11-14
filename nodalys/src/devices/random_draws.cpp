/**
 * @file    random_draws.cpp
 * @brief   Random draws written out by hand, so a seed gives the same numbers on every platform.
 */

#include "devices/random_draws.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace Nodalys {

namespace {

/// Bits of the engine output that are dropped, leaving the 53 of a double's significand.
constexpr int MANTISSA_DISCARDED_BITS = 11;
/// 2^-53, the weight of the lowest kept bit.
constexpr double MANTISSA_SCALE = 1.0 / 9007199254740992.0;
/// Largest mean that Knuth's multiplication takes in one piece, since it compares against exp(-lambda), which would
/// underflow and lose precision for a large lambda.
constexpr double POISSON_CHUNK_LAMBDA = 30.0;
/// Above this mean the Poisson draw is a rounded normal one, since splitting into chunks costs time in proportion to
/// lambda and the skewness of the distribution is below 0.04 here.
constexpr double POISSON_EXACT_LIMIT = 1000.0;

/**
 * @brief   Draws a Poisson number with a mean small enough for exp(-lambda) to be accurate.
 * @param[in,out] engine  Source of the bits.
 * @param[in] lambda      Mean, at most POISSON_CHUNK_LAMBDA.
 * @return  The count, by Knuth's method: uniform numbers are multiplied until the product falls below exp(-lambda).
 */
int DrawPoissonKnuth(std::mt19937_64 &engine, const double lambda) {
    const double limit = std::exp(-lambda);
    int count = 0;
    double product = DrawUniform(engine);
    while (product > limit) {
        ++count;
        product *= DrawUniform(engine);
    }
    return count;
}

} // namespace

/**
 * @brief   Draws a number between 0 and 1.
 * @param[in,out] engine  Source of the bits.
 * @return  A value from 0 up to but not including 1, made of the top 53 bits of the engine.
 */
double DrawUniform(std::mt19937_64 &engine) {
    return static_cast<double>(engine() >> MANTISSA_DISCARDED_BITS) * MANTISSA_SCALE;
}

/**
 * @brief   Draws a number of a normal distribution with a mean of 0 and a standard deviation of 1.
 * @param[in,out] engine  Source of the bits.
 * @return  The number, by the Box-Muller transform; the second number of each pair is dropped, so a draw consumes
 *          two engine outputs whatever came before.
 */
double DrawStandardNormal(std::mt19937_64 &engine) {
    // One minus the uniform number lies in (0, 1], which the logarithm accepts
    const double radius = std::sqrt(-2.0 * std::log(1.0 - DrawUniform(engine)));
    const double angle = 2.0 * std::numbers::pi * DrawUniform(engine);
    return radius * std::cos(angle);
}

/**
 * @brief   Draws a number of an exponential distribution.
 * @param[in,out] engine  Source of the bits.
 * @param[in] mean        Mean of the distribution, above 0.
 * @return  The number, by inversion of the distribution function.
 */
double DrawExponential(std::mt19937_64 &engine, const double mean) {
    return -mean * std::log(1.0 - DrawUniform(engine));
}

/**
 * @brief   Draws a number of a Poisson distribution.
 * @param[in,out] engine  Source of the bits.
 * @param[in] lambda      Mean of the distribution; a value of 0 or less gives 0.
 * @return  The count. A mean above POISSON_CHUNK_LAMBDA is split into chunks, since the sum of independent Poisson
 *          numbers is a Poisson number of the summed mean, so the draw stays exact up to POISSON_EXACT_LIMIT. Beyond
 *          it the count is a normal number of the same mean and variance, rounded and kept at 0 or more.
 */
int DrawPoisson(std::mt19937_64 &engine, const double lambda) {
    if (lambda <= 0.0) {
        return 0;
    }
    if (lambda > POISSON_EXACT_LIMIT) {
        const double approximation = lambda + std::sqrt(lambda) * DrawStandardNormal(engine);
        return static_cast<int>(std::max(0.0, std::round(approximation)));
    }
    int count = 0;
    double remaining = lambda;
    while (remaining > POISSON_CHUNK_LAMBDA) {
        count += DrawPoissonKnuth(engine, POISSON_CHUNK_LAMBDA);
        remaining -= POISSON_CHUNK_LAMBDA;
    }
    return count + DrawPoissonKnuth(engine, remaining);
}

} // namespace Nodalys
