/**
 * @file    pencil_reduction.cpp
 * @brief   Conditions a real matrix pencil (A, B) before the QZ iteration: scaling and removal of algebraic equations.
 */

#include "linear/eigen/pencil_reduction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nodalys {

namespace {

/// Equilibration sweeps over the rows and columns; each one halves the spread that is left, and ten is far past it.
constexpr int BALANCE_SWEEPS = 10;
/// A difference smaller than this many roundings of the terms is cancellation noise, not a value.
constexpr double CANCELLATION_ROUNDINGS = 8.0;

/**
 * @struct  AlgebraicPivot
 * @brief   The entry of A chosen to eliminate one algebraic row or column.
 */
struct AlgebraicPivot {
    bool IsRow = false;
    std::size_t Row = 0;
    std::size_t Col = 0;
    double Magnitude = 0.0;
};

DenseMatrix RemoveRowAndColumn(const DenseMatrix &matrix, const std::size_t drop_row, const std::size_t drop_col) {
    const std::size_t n = matrix.Rows();
    DenseMatrix reduced(n - 1, n - 1);
    std::size_t dst_r = 0;
    for (std::size_t r = 0; r < n; ++r) {
        if (r == drop_row) {
            continue;
        }
        std::size_t dst_c = 0;
        for (std::size_t c = 0; c < n; ++c) {
            if (c == drop_col) {
                continue;
            }
            reduced.At(dst_r, dst_c) = matrix.At(r, c);
            ++dst_c;
        }
        ++dst_r;
    }
    return reduced;
}

/**
 * @brief   Subtracts and snaps to exactly zero what cancels down to rounding noise.
 * @details An algebraic row is one whose B entries are exactly zero, which the stamps give for a node with no
 *          dynamics. Elimination can leave 1e-17 where the mathematics says 0, and a tolerance relative to the largest
 *          entry cannot tell that from a 1 pF capacitor beside a 10 uF one, so the noise is removed where it is made.
 */
double SubtractExactly(const double value, const double term) {
    const double result = value - term;
    const double noise =
        CANCELLATION_ROUNDINGS * std::numeric_limits<double>::epsilon() * (std::abs(value) + std::abs(term));
    return std::abs(result) <= noise ? 0.0 : result;
}

bool IsRowZero(const DenseMatrix &matrix, const std::size_t row) {
    for (std::size_t c = 0; c < matrix.Cols(); ++c) {
        if (matrix.At(row, c) != 0.0) {
            return false;
        }
    }
    return true;
}

bool IsColumnZero(const DenseMatrix &matrix, const std::size_t col) {
    for (std::size_t r = 0; r < matrix.Rows(); ++r) {
        if (matrix.At(r, col) != 0.0) {
            return false;
        }
    }
    return true;
}

void UpdateBestRowPivot(const DenseMatrix &a, const DenseMatrix &b, AlgebraicPivot &best) {
    for (std::size_t r = 0; r < a.Rows(); ++r) {
        if (!IsRowZero(b, r)) {
            continue;
        }
        for (std::size_t c = 0; c < a.Cols(); ++c) {
            const double value = std::abs(a.At(r, c));
            if (value > best.Magnitude) {
                best = {.IsRow = true, .Row = r, .Col = c, .Magnitude = value};
            }
        }
    }
}

void UpdateBestColumnPivot(const DenseMatrix &a, const DenseMatrix &b, AlgebraicPivot &best) {
    for (std::size_t c = 0; c < a.Cols(); ++c) {
        if (!IsColumnZero(b, c)) {
            continue;
        }
        for (std::size_t r = 0; r < a.Rows(); ++r) {
            const double value = std::abs(a.At(r, c));
            if (value > best.Magnitude) {
                best = {.IsRow = false, .Row = r, .Col = c, .Magnitude = value};
            }
        }
    }
}

/// Clears the pivot row with operations on the columns, which leave the eigenvalues alone.
void EliminateRowPivot(DenseMatrix &a, DenseMatrix &b, const std::size_t pivot_row, const std::size_t pivot_col) {
    const std::size_t n = a.Rows();
    const double pivot = a.At(pivot_row, pivot_col);
    for (std::size_t j = 0; j < n; ++j) {
        if (j == pivot_col) {
            continue;
        }
        const double factor = a.At(pivot_row, j) / pivot;
        if (factor == 0.0) {
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) {
            a.At(i, j) = SubtractExactly(a.At(i, j), factor * a.At(i, pivot_col));
            b.At(i, j) = SubtractExactly(b.At(i, j), factor * b.At(i, pivot_col));
        }
    }
}

/// Clears the pivot column with operations on the rows, which leave the eigenvalues alone.
void EliminateColumnPivot(DenseMatrix &a, DenseMatrix &b, const std::size_t pivot_row, const std::size_t pivot_col) {
    const std::size_t n = a.Rows();
    const double pivot = a.At(pivot_row, pivot_col);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == pivot_row) {
            continue;
        }
        const double factor = a.At(i, pivot_col) / pivot;
        if (factor == 0.0) {
            continue;
        }
        for (std::size_t j = 0; j < n; ++j) {
            a.At(i, j) = SubtractExactly(a.At(i, j), factor * a.At(pivot_row, j));
            b.At(i, j) = SubtractExactly(b.At(i, j), factor * b.At(pivot_row, j));
        }
    }
}

/// Scales every row, or every column, so that its entries of A and B add up to about 1.
void EquilibrateRows(DenseMatrix &a, DenseMatrix &b) {
    for (std::size_t r = 0; r < a.Rows(); ++r) {
        double size = 0.0;
        for (std::size_t c = 0; c < a.Cols(); ++c) {
            size += std::abs(a.At(r, c)) + std::abs(b.At(r, c));
        }
        if (size == 0.0) {
            continue;
        }
        const double factor = std::ldexp(1.0, -std::ilogb(size));
        for (std::size_t c = 0; c < a.Cols(); ++c) {
            a.At(r, c) *= factor;
            b.At(r, c) *= factor;
        }
    }
}

void EquilibrateColumns(DenseMatrix &a, DenseMatrix &b) {
    for (std::size_t c = 0; c < a.Cols(); ++c) {
        double size = 0.0;
        for (std::size_t r = 0; r < a.Rows(); ++r) {
            size += std::abs(a.At(r, c)) + std::abs(b.At(r, c));
        }
        if (size == 0.0) {
            continue;
        }
        const double factor = std::ldexp(1.0, -std::ilogb(size));
        for (std::size_t r = 0; r < a.Rows(); ++r) {
            a.At(r, c) *= factor;
            b.At(r, c) *= factor;
        }
    }
}

} // namespace

/**
 * @brief   Multiplies B by a power of two so that A and B have the same largest entry.
 * @details Dividing the roots by the same factor keeps them near 1, so the relative tolerances of the steps after it
 *          mean the same for a pencil of siemens and farads as for one of ohms and henries.
 * @param[in,out] mat_a  Matrix A, left alone.
 * @param[in,out] mat_b  Matrix B, scaled.
 * @return  The factor the roots of the scaled pencil must be multiplied by to give those of the original; 1 when B has
 *          no entry.
 */
double ScaleFrequency(DenseMatrix &mat_a, DenseMatrix &mat_b) {
    const double a_size = MaxAbsEntry(mat_a);
    const double b_size = MaxAbsEntry(mat_b);
    if (a_size == 0.0 || b_size == 0.0) {
        return 1.0;
    }
    const double factor = std::ldexp(1.0, std::ilogb(a_size) - std::ilogb(b_size));
    for (std::size_t r = 0; r < mat_b.Rows(); ++r) {
        for (std::size_t c = 0; c < mat_b.Cols(); ++c) {
            mat_b.At(r, c) *= factor;
        }
    }
    return factor;
}

/**
 * @brief   Scales the rows and columns of the pencil by powers of two until each has about the same size.
 * @details A diagonal scaling of both matrices leaves the eigenvalues alone, and powers of two leave the entries
 *          exact. MNA pencils mix siemens with ohms, farads with henries, so entries span many decades; without this
 *          the rounding of the Householder steps is set by the largest entry and the roots of the small rows and
 *          columns, the slow and the fast ones, are lost.
 * @param[in,out] mat_a  Matrix A.
 * @param[in,out] mat_b  Matrix B.
 */
void BalancePencil(DenseMatrix &mat_a, DenseMatrix &mat_b) {
    for (int sweep = 0; sweep < BALANCE_SWEEPS; ++sweep) {
        EquilibrateRows(mat_a, mat_b);
        EquilibrateColumns(mat_a, mat_b);
    }
}

/**
 * @brief   Removes the algebraic rows and columns, those with no entry in B, by exact elimination.
 * @details Each step takes the largest entry of A in such a row or column as the pivot, clears its column or row with
 *          operations that keep the eigenvalues, and drops the pair, so the infinite eigenvalues they stand for never
 *          reach the QZ iteration. It stops when none is left, or when what is left is zero in A as well, a
 *          singular remainder that QZ cannot make better.
 * @param[in,out] mat_a  Matrix A, which shrinks.
 * @param[in,out] mat_b  Matrix B, which shrinks the same way.
 */
void DeflateAlgebraicRowsAndColumns(DenseMatrix &mat_a, DenseMatrix &mat_b) {
    while (mat_a.Rows() > 0) {
        AlgebraicPivot best;
        UpdateBestRowPivot(mat_a, mat_b, best);
        UpdateBestColumnPivot(mat_a, mat_b, best);
        if (best.Magnitude == 0.0) {
            break;
        }
        if (best.IsRow) {
            EliminateRowPivot(mat_a, mat_b, best.Row, best.Col);
        } else {
            EliminateColumnPivot(mat_a, mat_b, best.Row, best.Col);
        }
        mat_a = RemoveRowAndColumn(mat_a, best.Row, best.Col);
        mat_b = RemoveRowAndColumn(mat_b, best.Row, best.Col);
    }
}

/**
 * @brief   Finds the largest absolute value among the entries of a matrix.
 * @param[in] matrix  Matrix to measure.
 * @return  The largest absolute entry; 0 for an empty matrix.
 */
double MaxAbsEntry(const DenseMatrix &matrix) {
    double max_value = 0.0;
    for (std::size_t r = 0; r < matrix.Rows(); ++r) {
        for (std::size_t c = 0; c < matrix.Cols(); ++c) {
            max_value = std::max(max_value, std::abs(matrix.At(r, c)));
        }
    }
    return max_value;
}

} // namespace Nodalys
