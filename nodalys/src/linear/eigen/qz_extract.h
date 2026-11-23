/**
 * @file    qz_extract.h
 * @brief   Phase 3 (Qzval) of Moler-Stewart QZ: extracts finite eigenvalues from quasi-triangular (A, B).
 */

#ifndef NODALYS_LINEAR_EIGEN_QZ_EXTRACT_H
#define NODALYS_LINEAR_EIGEN_QZ_EXTRACT_H

#include "linear/dense_matrix.h"
#include <complex>
#include <vector>

namespace Nodalys {

/**
 * @brief   Extracts finite eigenvalues from quasi-triangular A and triangular B.
 * @param[in,out] a              Quasi-triangular matrix.
 * @param[in,out] b              Upper triangular matrix.
 * @param[in]     epsb           Singularity floor for B diagonal.
 * @param[in]     infinite_beta  Largest beta that still counts as an infinite eigenvalue.
 * @return  Vector of finite complex eigenvalues.
 */
[[nodiscard]] std::vector<std::complex<double>>
ExtractQzEigenvalues(DenseMatrix &a, DenseMatrix &b, double epsb, double infinite_beta);

} // namespace Nodalys

#endif // NODALYS_LINEAR_EIGEN_QZ_EXTRACT_H
