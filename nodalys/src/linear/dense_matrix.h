/**
 * @file    dense_matrix.h
 * @brief   Dense matrix with LU factorization and forward/back substitution, for the equation system of Nodalys.
 * @details Algorithm: LU decomposition with partial pivoting (row permutation), then one forward and one backward
 *          substitution, as in Vlach & Singhal, "Computer Methods for Circuit Analysis and Design", chapter on the
 *          solution of linear equations. Partial pivoting keeps the error growth bounded for the diagonally weak
 *          systems MNA produces.
 */

#ifndef NODALYS_DENSE_MATRIX_H
#define NODALYS_DENSE_MATRIX_H

#include <complex>
#include <cstddef>
#include <vector>

namespace Nodalys {

/**
 * @class   BasicDenseMatrix
 * @brief   A dense matrix of real (`double`) or complex (`std::complex<double>`) scalars that factorizes itself in
 *          place and solves linear systems.
 */
template <typename Scalar> class BasicDenseMatrix {
public:
    BasicDenseMatrix(std::size_t rows, std::size_t cols);

    std::size_t Rows() const;
    std::size_t Cols() const;
    Scalar &At(std::size_t row, std::size_t col);
    Scalar At(std::size_t row, std::size_t col) const;
    void Add(std::size_t row, std::size_t col, Scalar value);
    void Reset();

    /**
     * @brief   Factorizes the matrix in place as P A = L U with partial pivoting.
     * @return  False when a pivot is (numerically) zero, which means the system is singular.
     * @note    After a call, At() reads the L and U factors, not the original matrix; Solve() uses them.
     */
    bool FactorizeLU();

    /**
     * @brief   Solves A x = b with the factors of FactorizeLU().
     * @param[in]  rhs  Right-hand side.
     * @param[out] x    Solution; sized by the call.
     * @return  False when the matrix was not factorized or the size mismatches.
     */
    bool Solve(const std::vector<Scalar> &rhs, std::vector<Scalar> &x) const;

private:
    std::size_t m_Rows;
    std::size_t m_Cols;
    std::vector<Scalar> m_Data;
    /// Row permutation of the pivoting: pivot order, one entry per row, set by FactorizeLU().
    std::vector<std::size_t> m_Pivot;
    bool m_Factored = false;
};

using DenseMatrix = BasicDenseMatrix<double>;
using ComplexDenseMatrix = BasicDenseMatrix<std::complex<double>>;

} // namespace Nodalys

#endif // NODALYS_DENSE_MATRIX_H
