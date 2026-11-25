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

std::vector<std::complex<double>> ComputeGeneralizedEigenvalues(const DenseMatrix &mat_a, const DenseMatrix &mat_b);

} // namespace Nodalys

#endif // NODALYS_QZ_H
