/**
 * @file    mna_system.cpp
 * @brief   The Modified Nodal Analysis (MNA) linear system assembled and solved at each Newton-Raphson iteration.
 */

#include "mna/mna_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Nodalys {

namespace {

bool SolveRealSystem(const DenseMatrix &a_orig, const std::vector<double> &b_orig, std::vector<double> &x) {
    const std::size_t n = a_orig.Rows();
    DenseMatrix factors = a_orig;
    std::vector<double> row_scale(n, 1.0);
    std::vector<double> scaled_rhs = b_orig;

    // 1. Row equilibration: scale each row by its largest entry magnitude so tiny conductances (e.g. Gmin = 1e-12)
    //    and unit branch coefficients (1.0) compete fairly during partial pivoting.
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
            scaled_rhs[r] *= row_scale[r];
        }
    }

    if (!factors.FactorizeLU() || !factors.Solve(scaled_rhs, x)) {
        return false;
    }

    // 2. One step of iterative refinement: r = b - A * x, solve A * dx = r, x += dx
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
    return true;
}

bool SolveComplexSystemMultiple(const ComplexDenseMatrix &y_orig,
                                const std::vector<std::vector<std::complex<double>>> &b_orig_list,
                                std::vector<std::vector<std::complex<double>>> &x_list) {
    const std::size_t n = y_orig.Rows();
    ComplexDenseMatrix factors = y_orig;
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
    return true;
}

bool SolveComplexSystem(const ComplexDenseMatrix &y_orig, const std::vector<std::complex<double>> &b_orig,
                        std::vector<std::complex<double>> &x) {
    const std::vector<std::vector<std::complex<double>>> b_list = {b_orig};
    std::vector<std::vector<std::complex<double>>> x_list;
    if (!SolveComplexSystemMultiple(y_orig, b_list, x_list)) {
        return false;
    }
    x = std::move(x_list[0]);
    return true;
}

} // namespace

MnaSystem::MnaSystem(const int node_count, const int branch_count)
    : m_A(static_cast<std::size_t>(node_count + branch_count), static_cast<std::size_t>(node_count + branch_count)),
      m_Rhs(static_cast<std::size_t>(node_count + branch_count), 0.0),
      m_IsNodeUnknown(static_cast<std::size_t>(node_count + branch_count), false), m_NodeUnknowns(node_count) {
    std::fill_n(m_IsNodeUnknown.begin(), node_count, true);
}

MnaSystem::MnaSystem(std::vector<bool> is_node_unknown)
    : m_A(is_node_unknown.size(), is_node_unknown.size()), m_Rhs(is_node_unknown.size(), 0.0),
      m_IsNodeUnknown(std::move(is_node_unknown)),
      m_NodeUnknowns(static_cast<int>(std::ranges::count(m_IsNodeUnknown, true))) {
}

int MnaSystem::Size() const {
    return static_cast<int>(m_A.Rows());
}

int MnaSystem::NodeIndex(const int node) const {
    return node <= 0 ? -1 : node - 1;
}

int MnaSystem::BranchIndex(const int ordinal) const {
    return m_NodeUnknowns + ordinal;
}

int MnaSystem::NodeUnknowns() const {
    return m_NodeUnknowns;
}

const DenseMatrix &MnaSystem::Matrix() const {
    return m_A;
}

const std::vector<double> &MnaSystem::Rhs() const {
    return m_Rhs;
}

bool MnaSystem::IsNodeUnknown(const int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < m_IsNodeUnknown.size() &&
           m_IsNodeUnknown[static_cast<std::size_t>(index)];
}

void MnaSystem::AddA(const int row, const int col, const double value) {
    if (row < 0 || col < 0) {
        return;
    }
    m_A.Add(static_cast<std::size_t>(row), static_cast<std::size_t>(col), value);
}

void MnaSystem::AddRhs(const int row, const double value) {
    if (row < 0) {
        return;
    }
    m_Rhs[static_cast<std::size_t>(row)] += value;
}

void MnaSystem::AddGminToNodes(const double conductance) {
    const int size = Size();
    for (int index = 0; index < size; ++index) {
        if (IsNodeUnknown(index)) {
            AddA(index, index, conductance);
        }
    }
}

void MnaSystem::Clear() {
    m_A.Reset();
    std::ranges::fill(m_Rhs, 0.0);
}

void MnaSystem::ClearRhs() {
    std::ranges::fill(m_Rhs, 0.0);
}

bool MnaSystem::Solve(std::vector<double> &x) const {
    return SolveRealSystem(m_A, m_Rhs, x);
}

bool MnaSystem::SolveTranspose(const std::vector<double> &rhs, std::vector<double> &x) const {
    const std::size_t n = m_A.Rows();
    if (rhs.size() != n) {
        return false;
    }
    DenseMatrix a_transpose(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            a_transpose.At(r, c) = m_A.At(c, r);
        }
    }
    return SolveRealSystem(a_transpose, rhs, x);
}

bool MnaSystem::SolveAc(const MnaSystem &reactive, const double omega, std::vector<std::complex<double>> &x_ac) const {
    const std::size_t n = m_A.Rows();
    if (reactive.m_A.Rows() != n) {
        return false;
    }

    // Assemble Y(j * omega) = A_dc + j * omega * C_ac and complex RHS b_ac
    ComplexDenseMatrix y_orig(n, n);
    std::vector<std::complex<double>> b_orig(n);
    for (std::size_t r = 0; r < n; ++r) {
        b_orig[r] = std::complex<double>(reactive.m_Rhs[r], 0.0);
        for (std::size_t c = 0; c < n; ++c) {
            y_orig.At(r, c) = std::complex<double>(m_A.At(r, c), omega * reactive.m_A.At(r, c));
        }
    }
    return SolveComplexSystem(y_orig, b_orig, x_ac);
}

bool MnaSystem::SolveAcMultiple(const MnaSystem &reactive, const double omega,
                                const std::vector<std::vector<std::complex<double>>> &rhs_list,
                                std::vector<std::vector<std::complex<double>>> &solutions) const {
    const std::size_t n = m_A.Rows();
    if (reactive.m_A.Rows() != n) {
        return false;
    }

    ComplexDenseMatrix y_orig(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            y_orig.At(r, c) = std::complex<double>(m_A.At(r, c), omega * reactive.m_A.At(r, c));
        }
    }
    return SolveComplexSystemMultiple(y_orig, rhs_list, solutions);
}

bool MnaSystem::SolveAcTranspose(const MnaSystem &reactive, const double omega, const int output_unknown,
                                 std::vector<std::complex<double>> &adjoint) const {
    const std::size_t n = m_A.Rows();
    if (reactive.m_A.Rows() != n || output_unknown < 0 || static_cast<std::size_t>(output_unknown) >= n) {
        return false;
    }

    // Assemble Y(j * omega)^T and unit basis RHS e_out
    ComplexDenseMatrix y_transpose(n, n);
    std::vector<std::complex<double>> b_orig(n, std::complex<double>(0.0, 0.0));
    b_orig[static_cast<std::size_t>(output_unknown)] = std::complex<double>(1.0, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            y_transpose.At(r, c) = std::complex<double>(m_A.At(c, r), omega * reactive.m_A.At(c, r));
        }
    }
    return SolveComplexSystem(y_transpose, b_orig, adjoint);
}

bool MnaSystem::CheckResidual(const std::vector<double> &x, const double voltage_abstol, const double current_abstol,
                              const double reltol) const {
    const int size = Size();
    for (int row = 0; row < size; ++row) {
        double sum = 0.0;
        double sum_abs = 0.0;
        for (int col = 0; col < size; ++col) {
            const double val = m_A.At(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
            const double term = val * x[static_cast<std::size_t>(col)];
            sum += term;
            sum_abs += std::fabs(term);
        }
        const double rhs = m_Rhs[static_cast<std::size_t>(row)];
        const double residual = std::fabs(sum - rhs);
        // Node rows represent KCL (units: Amperes); branch rows represent voltage relations (units: Volts)
        const double abstol = IsNodeUnknown(row) ? current_abstol : voltage_abstol;
        const double tol = abstol + reltol * std::max(sum_abs, std::fabs(rhs));
        if (residual > tol) {
            return false;
        }
    }
    return true;
}

} // namespace Nodalys
