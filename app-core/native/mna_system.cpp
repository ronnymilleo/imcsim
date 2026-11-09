/**
 * @file    mna_system.cpp
 * @brief   The Modified Nodal Analysis (MNA) linear system assembled and solved at each Newton-Raphson iteration.
 */

#include "native/mna_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Core::Native {

MnaSystem::MnaSystem(const int node_count, const int branch_count)
    : m_A(static_cast<std::size_t>(node_count + branch_count), static_cast<std::size_t>(node_count + branch_count)),
      m_Rhs(static_cast<std::size_t>(node_count + branch_count), 0.0),
      m_IsNodeUnknown(static_cast<std::size_t>(node_count + branch_count), false),
      m_NodeUnknowns(node_count) {
    std::fill_n(m_IsNodeUnknown.begin(), node_count, true);
}

MnaSystem::MnaSystem(std::vector<bool> is_node_unknown)
    : m_A(is_node_unknown.size(), is_node_unknown.size()),
      m_Rhs(is_node_unknown.size(), 0.0),
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

bool MnaSystem::Solve(std::vector<double> &x) const {
    // Factorize a copy of m_A so that m_A remains intact if CheckResidual() needs to inspect it
    DenseMatrix factors = m_A;
    return factors.FactorizeLU() && factors.Solve(m_Rhs, x);
}

bool MnaSystem::CheckResidual(const std::vector<double> &x, const double voltage_abstol,
                              const double current_abstol, const double reltol) const {
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

} // namespace Core::Native
