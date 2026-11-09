/**
 * @file    dense_matrix.cpp
 * @brief   Dense matrix with LU factorization and forward/back substitution, for the equation system of the native
 *          solver.
 */

#include "native/linear/dense_matrix.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Core::Native {

DenseMatrix::DenseMatrix(const std::size_t rows, const std::size_t cols)
    : m_Rows(rows), m_Cols(cols), m_Data(rows * cols, 0.0), m_Pivot(rows) {
}

std::size_t DenseMatrix::Rows() const {
    return m_Rows;
}

std::size_t DenseMatrix::Cols() const {
    return m_Cols;
}

double &DenseMatrix::At(const std::size_t row, const std::size_t col) {
    return m_Data[row * m_Cols + col];
}

double DenseMatrix::At(const std::size_t row, const std::size_t col) const {
    return m_Data[row * m_Cols + col];
}

void DenseMatrix::Add(const std::size_t row, const std::size_t col, const double value) {
    At(row, col) += value;
}

void DenseMatrix::Reset() {
    std::ranges::fill(m_Data, 0.0);
    m_Factored = false;
}

/**
 * @brief   Factorizes the matrix in place as P A = L U with partial pivoting: for each column the largest pivot row
 *          swaps to the diagonal, the column below the diagonal becomes L, and the rest becomes U.
 * @return  False when a pivot is (numerically) zero, which means the system is singular.
 * @note    A zero pivot is tested against a floor scaled by the largest matrix entry, so tiny but usable
 *          conductances keep working while exact singularities are caught.
 */
bool DenseMatrix::FactorizeLU() {
    const std::size_t n = std::min(m_Rows, m_Cols);
    const double largest =
        std::ranges::max(m_Data | std::views::transform([](const double v) { return std::fabs(v); }));
    const double pivot_floor = largest * 1e-30;
    for (std::size_t col = 0; col < n; ++col) {
        // Partial pivoting: the row with the largest entry in this column leads, which bounds the error growth
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < m_Rows; ++row) {
            if (std::fabs(At(row, col)) > std::fabs(At(pivot, col))) {
                pivot = row;
            }
        }
        m_Pivot[col] = pivot;
        if (std::fabs(At(pivot, col)) <= pivot_floor) {
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
            const double factor = At(row, col) / At(col, col);
            At(row, col) = factor;
            if (factor == 0.0) {
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
bool DenseMatrix::Solve(const std::vector<double> &rhs, std::vector<double> &x) const {
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

} // namespace Core::Native
