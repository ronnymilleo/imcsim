/**
 * @file    mna_solve.cpp
 * @brief   Row-equilibrated LU factorization and single-step iterative refinement for real and complex MNA matrices.
 */

#include "mna/mna_solve.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Nodalys {

namespace {

/// Context forwarded from `ComplexStamper` into a `ComplexDenseMatrix` during AC assembly.
struct MatrixStamperContext {
    ComplexDenseMatrix &Target;
    bool Transpose = false;
};

void ApplyFrequencyEntry(const int row, const int col, const std::complex<double> value, void *raw_context) {
    auto &ctx = *static_cast<MatrixStamperContext *>(raw_context);
    const auto r = static_cast<std::size_t>(ctx.Transpose ? col : row);
    const auto c = static_cast<std::size_t>(ctx.Transpose ? row : col);
    if (r < ctx.Target.Rows() && c < ctx.Target.Cols()) {
        ctx.Target.At(r, c) += value;
    }
}

std::vector<double> EquilibrateRealMatrix(DenseMatrix &factors) {
    const std::size_t n = factors.Rows();
    std::vector<double> row_scale(n, 1.0);
    for (std::size_t r = 0; r < n; ++r) {
        double max_entry = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            max_entry = std::max(max_entry, std::fabs(factors.At(r, c)));
        }
        if (max_entry > 0.0) {
            row_scale[r] = 1.0 / max_entry;
            for (std::size_t c = 0; c < n; ++c) {
                factors.At(r, c) *= row_scale[r];
            }
        }
    }
    return row_scale;
}

std::vector<double> EquilibrateComplexMatrix(ComplexDenseMatrix &factors) {
    const std::size_t n = factors.Rows();
    std::vector<double> row_scale(n, 1.0);
    for (std::size_t r = 0; r < n; ++r) {
        double max_entry = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            max_entry = std::max(max_entry, std::abs(factors.At(r, c)));
        }
        if (max_entry > 0.0) {
            row_scale[r] = 1.0 / max_entry;
            for (std::size_t c = 0; c < n; ++c) {
                factors.At(r, c) *= row_scale[r];
            }
        }
    }
    return row_scale;
}

void RefineRealSolution(const DenseMatrix &a_orig,
                        const DenseMatrix &factors,
                        const std::vector<double> &b_orig,
                        const std::vector<double> &row_scale,
                        std::vector<double> &x) {
    const std::size_t n = a_orig.Rows();
    std::vector<double> scaled_residual(n, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        double ax = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            ax += a_orig.At(r, c) * x[c];
        }
        scaled_residual[r] = (b_orig[r] - ax) * row_scale[r];
    }
    std::vector<double> dx;
    if (factors.Solve(scaled_residual, dx)) {
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += dx[i];
        }
    }
}

void RefineComplexSolution(const ComplexDenseMatrix &y_orig,
                           const ComplexDenseMatrix &factors,
                           const std::vector<std::complex<double>> &b_orig,
                           const std::vector<double> &row_scale,
                           std::vector<std::complex<double>> &x) {
    const std::size_t n = y_orig.Rows();
    std::vector<std::complex<double>> scaled_residual(n, std::complex<double>{0.0, 0.0});
    for (std::size_t r = 0; r < n; ++r) {
        std::complex<double> ax{0.0, 0.0};
        for (std::size_t c = 0; c < n; ++c) {
            ax += y_orig.At(r, c) * x[c];
        }
        scaled_residual[r] = (b_orig[r] - ax) * row_scale[r];
    }
    std::vector<std::complex<double>> dx;
    if (factors.Solve(scaled_residual, dx)) {
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += dx[i];
        }
    }
}

} // namespace

/**
 * @brief   Assembles the complex AC matrix \f$Y(j\omega) = A_{\text{DC}} + j\omega C_{\text{AC}} +
 *          \Delta Y_{\text{freq}}(\omega)\f$ (or its transpose).
 * @param[in] dc_matrix         Real DC conductance/Jacobian matrix.
 * @param[in] ac_matrix         Reactive coefficient matrix.
 * @param[in] omega             Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[in] transpose         True to assemble \f$Y(j\omega)^T\f$ instead of \f$Y(j\omega)\f$.
 * @param[in] frequency_stamps  Distributed/dispersive callbacks contributing frequency-dependent deltas.
 * @return  Assembled complex dense matrix.
 */
ComplexDenseMatrix AssembleAcMatrix(const DenseMatrix &dc_matrix,
                                    const DenseMatrix &ac_matrix,
                                    const double omega,
                                    const bool transpose,
                                    const std::vector<FrequencyStamp> &frequency_stamps) {
    const std::size_t n = dc_matrix.Rows();
    ComplexDenseMatrix y_orig(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            const std::size_t src_r = transpose ? c : r;
            const std::size_t src_c = transpose ? r : c;
            y_orig.At(r, c) = std::complex<double>(dc_matrix.At(src_r, src_c), omega * ac_matrix.At(src_r, src_c));
        }
    }
    if (omega > 0.0 && !frequency_stamps.empty()) {
        MatrixStamperContext ctx{.Target = y_orig, .Transpose = transpose};
        const ComplexStamper stamper{.Apply = ApplyFrequencyEntry, .Context = &ctx};
        for (const FrequencyStamp &stamp : frequency_stamps) {
            stamp.Apply(stamper, omega);
        }
    }
    return y_orig;
}

/**
 * @brief   Solves \f$A \cdot x = b\f$ with row equilibration, partial-pivoting LU, and one iterative-refinement pass.
 * @param[in]  a_orig  Unscaled square system matrix.
 * @param[in]  b_orig  Unscaled right-hand side vector.
 * @param[out] x       Solution vector.
 * @return  False when the matrix is singular.
 */
bool SolveRealSystem(const DenseMatrix &a_orig, const std::vector<double> &b_orig, std::vector<double> &x) {
    const std::size_t n = a_orig.Rows();
    DenseMatrix factors = a_orig;
    const std::vector<double> row_scale = EquilibrateRealMatrix(factors);
    std::vector<double> scaled_rhs = b_orig;
    for (std::size_t r = 0; r < n; ++r) {
        scaled_rhs[r] *= row_scale[r];
    }
    if (!factors.FactorizeLU() || !factors.Solve(scaled_rhs, x)) {
        return false;
    }
    RefineRealSolution(a_orig, factors, b_orig, row_scale, x);
    return true;
}

/**
 * @brief   Solves a complex linear system for multiple right-hand sides reusing one equilibrated LU factorization.
 * @param[in]  y_orig       Unscaled square complex matrix.
 * @param[in]  b_orig_list  List of unscaled complex right-hand side vectors.
 * @param[out] x_list       Output list of complex solution vectors.
 * @return  False when the matrix is singular.
 */
bool SolveComplexSystemMultiple(const ComplexDenseMatrix &y_orig,
                                const std::vector<std::vector<std::complex<double>>> &b_orig_list,
                                std::vector<std::vector<std::complex<double>>> &x_list) {
    const std::size_t n = y_orig.Rows();
    ComplexDenseMatrix factors = y_orig;
    const std::vector<double> row_scale = EquilibrateComplexMatrix(factors);
    if (!factors.FactorizeLU()) {
        return false;
    }

    x_list.resize(b_orig_list.size());
    for (std::size_t rhs_idx = 0; rhs_idx < b_orig_list.size(); ++rhs_idx) {
        const auto &b_orig = b_orig_list[rhs_idx];
        std::vector<std::complex<double>> scaled_rhs = b_orig;
        for (std::size_t r = 0; r < n; ++r) {
            scaled_rhs[r] *= row_scale[r];
        }
        auto &x = x_list[rhs_idx];
        if (!factors.Solve(scaled_rhs, x)) {
            return false;
        }
        RefineComplexSolution(y_orig, factors, b_orig, row_scale, x);
    }
    return true;
}

/**
 * @brief   Solves \f$Y \cdot x = b\f$ for a single complex right-hand side with row equilibration and refinement.
 * @param[in]  y_orig  Unscaled square complex matrix.
 * @param[in]  b_orig  Unscaled complex right-hand side vector.
 * @param[out] x       Complex solution vector.
 * @return  False when the matrix is singular.
 */
bool SolveComplexSystem(const ComplexDenseMatrix &y_orig,
                        const std::vector<std::complex<double>> &b_orig,
                        std::vector<std::complex<double>> &x) {
    const std::vector<std::vector<std::complex<double>>> b_list = {b_orig};
    std::vector<std::vector<std::complex<double>>> x_list;
    if (!SolveComplexSystemMultiple(y_orig, b_list, x_list)) {
        return false;
    }
    x = std::move(x_list[0]);
    return true;
}

} // namespace Nodalys
