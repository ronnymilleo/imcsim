/**
 * @file    root_refinement.h
 * @brief   Refines the generalized eigenvalues of a real pencil (A, B) so each is accurate to its own size.
 * @details QZ finds every eigenvalue to about a rounding of the largest one, so in a circuit with a junction pole at
 *          3e12 rad/s a root near 1e4 is good to a few parts in a thousand, and which way it falls depends on the
 *          rounding of the platform. Inverse iteration on the pencil itself, in complex arithmetic, brings each root
 *          to a rounding of its own size, and gives the residual that tells a root at the origin from a small one.
 */

#ifndef NODALYS_ROOT_REFINEMENT_H
#define NODALYS_ROOT_REFINEMENT_H

#include "linear/dense_matrix.h"
#include <complex>
#include <vector>

namespace Nodalys {

// Refinement
std::vector<std::complex<double>> RefineRoots(const DenseMatrix &mat_a, const DenseMatrix &mat_b,
                                              const std::vector<std::complex<double>> &roots);

} // namespace Nodalys

#endif // NODALYS_ROOT_REFINEMENT_H
