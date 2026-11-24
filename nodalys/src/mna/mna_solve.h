/**
 * @file    mna_solve.h
 * @brief   Row-equilibrated LU factorization and single-step iterative refinement for real and complex MNA matrices.
 */

#ifndef NODALYS_MNA_SOLVE_H
#define NODALYS_MNA_SOLVE_H

#include "linear/dense_matrix.h"
#include <complex>
#include <vector>

namespace Nodalys {

// Matrix assembly
[[nodiscard]] ComplexDenseMatrix
AssembleAcMatrix(const DenseMatrix &dc_matrix, const DenseMatrix &ac_matrix, double omega, bool transpose = false);

// Linear system solvers
[[nodiscard]] bool
SolveRealSystem(const DenseMatrix &a_orig, const std::vector<double> &b_orig, std::vector<double> &x);
[[nodiscard]] bool SolveComplexSystem(const ComplexDenseMatrix &y_orig,
                                      const std::vector<std::complex<double>> &b_orig,
                                      std::vector<std::complex<double>> &x);
[[nodiscard]] bool SolveComplexSystemMultiple(const ComplexDenseMatrix &y_orig,
                                              const std::vector<std::vector<std::complex<double>>> &b_orig_list,
                                              std::vector<std::vector<std::complex<double>>> &x_list);

} // namespace Nodalys

#endif // NODALYS_MNA_SOLVE_H
