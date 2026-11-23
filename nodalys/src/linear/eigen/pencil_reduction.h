/**
 * @file    pencil_reduction.h
 * @brief   Conditions a real matrix pencil (A, B) before the QZ iteration: scaling and removal of algebraic equations.
 * @details An MNA pencil mixes siemens with farads and henries and has rows with no dynamics at all (a node with no
 *          capacitor, a source branch). Left alone, the rounding of the QZ steps is set by the largest entry and the
 *          algebraic rows leak spurious infinite eigenvalues, so the slow and the fast roots come out wrong.
 */

#ifndef NODALYS_PENCIL_REDUCTION_H
#define NODALYS_PENCIL_REDUCTION_H

#include "linear/dense_matrix.h"

namespace Nodalys {

// Scaling
double ScaleFrequency(DenseMatrix &mat_a, DenseMatrix &mat_b);
void BalancePencil(DenseMatrix &mat_a, DenseMatrix &mat_b);

// Elimination
void DeflateAlgebraicRowsAndColumns(DenseMatrix &mat_a, DenseMatrix &mat_b);

// Measures
double MaxAbsEntry(const DenseMatrix &matrix);

} // namespace Nodalys

#endif // NODALYS_PENCIL_REDUCTION_H
