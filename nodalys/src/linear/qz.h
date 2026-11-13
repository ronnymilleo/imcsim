/**
 * @file    qz.h
 * @brief   Generalized eigenvalue solver for real matrix pencils (A, B) via the QZ algorithm (Moler-Stewart).
 */

#ifndef NODALYS_QZ_H
#define NODALYS_QZ_H

#include "linear/dense_matrix.h"
#include <complex>
#include <vector>

namespace Nodalys {

/**
 * @brief   Computes the finite generalized eigenvalues lambda of the real matrix pencil (A, B) such that
 *          det(A - lambda * B) = 0.
 * @param[in] mat_a  Real square matrix of size N x N.
 * @param[in] mat_b  Real square matrix of size N x N.
 * @return  Vector of finite complex generalized eigenvalues in rad/s. Infinite and indeterminate eigenvalues are
 *          deflated.
 */
std::vector<std::complex<double>> ComputeGeneralizedEigenvalues(const DenseMatrix &mat_a, const DenseMatrix &mat_b);

} // namespace Nodalys

#endif // NODALYS_QZ_H
