/**
 * @file    dense_matrix.cpp
 * @brief   Dense matrix with LU factorization and forward/back substitution, for the equation system of Nodalys.
 */

#include "linear/dense_matrix.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Nodalys {

template <typename Scalar>
BasicDenseMatrix<Scalar>::BasicDenseMatrix(const std::size_t rows, const std::size_t cols)
    : m_Rows(rows), m_Cols(cols), m_Data(rows * cols, Scalar{0.0}), m_Pivot(rows) {
}

template <typename Scalar> std::size_t BasicDenseMatrix<Scalar>::Rows() const {
    return m_Rows;
}

template <typename Scalar> std::size_t BasicDenseMatrix<Scalar>::Cols() const {
    return m_Cols;
}

template <typename Scalar> Scalar &BasicDenseMatrix<Scalar>::At(const std::size_t row, const std::size_t col) {
    return m_Data[row * m_Cols + col];
}

template <typename Scalar> Scalar BasicDenseMatrix<Scalar>::At(const std::size_t row, const std::size_t col) const {
    return m_Data[row * m_Cols + col];
}

template <typename Scalar>
void BasicDenseMatrix<Scalar>::Add(const std::size_t row, const std::size_t col, const Scalar value) {
    At(row, col) += value;
}

template <typename Scalar> void BasicDenseMatrix<Scalar>::Reset() {
    std::ranges::fill(m_Data, Scalar{0.0});
    m_Factored = false;
}

/**
 * @brief   Factorizes the matrix in place as P A = L U with partial pivoting: for each column the largest pivot row
 *          swaps to the diagonal, the column below the diagonal becomes L, and the rest becomes U.
 * @return  False when a pivot is (numerically) zero, which means the system is singular.
 * @note    A zero pivot is tested against a floor scaled by the largest matrix entry, so tiny but usable
 *          conductances keep working while exact singularities are caught.
 */
template <typename Scalar> bool BasicDenseMatrix<Scalar>::FactorizeLU() {
    const std::size_t n = std::min(m_Rows, m_Cols);
    const double largest = std::ranges::max(m_Data | std::views::transform([](const Scalar v) { return std::abs(v); }));
    const double pivot_floor = largest * 1e-30;
    for (std::size_t col = 0; col < n; ++col) {
        // Partial pivoting: the row with the largest entry in this column leads, which bounds the error growth
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < m_Rows; ++row) {
            if (std::abs(At(row, col)) > std::abs(At(pivot, col))) {
                pivot = row;
            }
        }
        m_Pivot[col] = pivot;
        if (std::abs(At(pivot, col)) <= pivot_floor) {
            m_Factored = false;
            return false;
        }
        if (pivot != col) {
            for (std::size_t c = 0; c < m_Cols; ++c) {
                std::swap(At(pivot, c), At(col, c));
            }
        }
        // Eliminate the rows below the pivot
        for (std::size_t row = col + 1; row < m_Rows; ++row) {
            const Scalar factor = At(row, col) / At(col, col);
            At(row, col) = factor;
            if (factor == Scalar{0.0}) {
                continue;
            }
            for (std::size_t c = col + 1; c < m_Cols; ++c) {
                At(row, c) -= factor * At(col, c);
            }
        }
    }
    m_Factored = true;
    return true;
}

/**
 * @brief   Solves A x = b with the factors: permutes b, solves L y = P b forward, then U x = y backward.
 * @param[in]  rhs  Right-hand side.
 * @param[out] x    Solution; sized by the call.
 * @return  False when the matrix was not factorized or the size mismatches.
 */
template <typename Scalar>
bool BasicDenseMatrix<Scalar>::Solve(const std::vector<Scalar> &rhs, std::vector<Scalar> &x) const {
    if (!m_Factored || rhs.size() != m_Rows) {
        return false;
    }
    x = rhs;
    // Forward substitution with the row swaps of the pivoting applied first
    for (std::size_t row = 0; row < m_Rows; ++row) {
        if (m_Pivot[row] != row) {
            std::swap(x[row], x[m_Pivot[row]]);
        }
        for (std::size_t c = 0; c < row; ++c) {
            x[row] -= At(row, c) * x[c];
        }
    }
    // Backward substitution over the upper factor, whose diagonal holds the pivots
    const std::size_t n = std::min(m_Rows, m_Cols);
    for (std::size_t row = n; row-- > 0;) {
        for (std::size_t c = row + 1; c < n; ++c) {
            x[row] -= At(row, c) * x[c];
        }
        x[row] /= At(row, row);
    }
    return true;
}

template class BasicDenseMatrix<double>;
template class BasicDenseMatrix<std::complex<double>>;

} // namespace Nodalys
