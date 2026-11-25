/**
 * @file    mna_system.cpp
 * @brief   The Modified Nodal Analysis (MNA) linear system assembled and solved at each Newton-Raphson iteration.
 */

#include "mna/mna_system.h"

#include "linear/dense_matrix.h"
#include "mna/mna_solve.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Nodalys {

/**
 * @struct  MnaSystem::Storage
 * @brief   Private matrix storage backing MnaSystem so callers stay decoupled from the matrix layout.
 */
struct MnaSystem::Storage {
    DenseMatrix Matrix;

    /**
     * @brief   Allocates a zero-initialized square matrix of dimension `size`.
     * @param[in] size  Number of rows and columns.
     */
    explicit Storage(const std::size_t size) : Matrix(size, size) {}
};

/**
 * @brief   Constructs an MNA system where all node unknowns precede all branch unknowns.
 * @param[in] node_count    Number of non-ground node-voltage unknowns.
 * @param[in] branch_count  Number of branch-current unknowns.
 */
MnaSystem::MnaSystem(const int node_count, const int branch_count)
    : m_Storage(std::make_unique<Storage>(static_cast<std::size_t>(node_count + branch_count))),
      m_Rhs(static_cast<std::size_t>(node_count + branch_count), 0.0),
      m_IsNodeUnknown(static_cast<std::size_t>(node_count + branch_count), false), m_NodeUnknowns(node_count) {
    std::fill_n(m_IsNodeUnknown.begin(), node_count, true);
}

/**
 * @brief   Constructs an MNA system with an arbitrary ordering of node and branch unknowns.
 * @param[in] is_node_unknown  Flags indicating which unknown indices represent node voltages (`true`) vs. branch
 *                             currents (`false`).
 */
MnaSystem::MnaSystem(std::vector<bool> is_node_unknown)
    : m_Storage(std::make_unique<Storage>(is_node_unknown.size())), m_Rhs(is_node_unknown.size(), 0.0),
      m_IsNodeUnknown(std::move(is_node_unknown)),
      m_NodeUnknowns(static_cast<int>(std::ranges::count(m_IsNodeUnknown, true))) {
}

/**
 * @brief   Deep-copies an MNA system, including its matrix storage and right-hand side.
 * @param[in] other  System to copy.
 */
MnaSystem::MnaSystem(const MnaSystem &other)
    : m_Storage(other.m_Storage ? std::make_unique<Storage>(*other.m_Storage) : nullptr), m_Rhs(other.m_Rhs),
      m_IsNodeUnknown(other.m_IsNodeUnknown), m_FrequencyStamps(other.m_FrequencyStamps),
      m_NodeUnknowns(other.m_NodeUnknowns) {
}

/**
 * @brief   Deep-copies another MNA system into this instance.
 * @param[in] other  System to copy.
 * @return  Reference to this system.
 */
MnaSystem &MnaSystem::operator=(const MnaSystem &other) {
    if (this == &other) {
        return *this;
    }
    m_Storage = other.m_Storage ? std::make_unique<Storage>(*other.m_Storage) : nullptr;
    m_Rhs = other.m_Rhs;
    m_IsNodeUnknown = other.m_IsNodeUnknown;
    m_FrequencyStamps = other.m_FrequencyStamps;
    m_NodeUnknowns = other.m_NodeUnknowns;
    return *this;
}

/**
 * @brief   Move-constructs an MNA system.
 * @param[in,out] other  System to move from.
 */
MnaSystem::MnaSystem(MnaSystem &&other) noexcept = default;

/**
 * @brief   Move-assigns an MNA system.
 * @param[in,out] other  System to move from.
 * @return  Reference to this system.
 */
MnaSystem &MnaSystem::operator=(MnaSystem &&other) noexcept = default;

/**
 * @brief   Destroys the MNA system and its matrix storage.
 */
MnaSystem::~MnaSystem() = default;

/**
 * @brief   Returns the total number of unknowns (node voltages plus branch currents).
 * @return  System dimension.
 */
int MnaSystem::GetSize() const {
    return static_cast<int>(m_Storage->Matrix.Rows());
}

/**
 * @brief   Maps a 1-based circuit node number to its 0-based unknown index.
 * @param[in] node  Circuit node number (`0` for ground).
 * @return  Zero-based unknown index, or `-1` for ground.
 */
int MnaSystem::NodeIndex(const int node) const {
    return node <= 0 ? -1 : node - 1;
}

/**
 * @brief   Maps a 0-based branch ordinal to its unknown index (when all nodes precede all branches).
 * @param[in] ordinal  Zero-based branch ordinal.
 * @return  Unknown index for the branch current.
 */
int MnaSystem::BranchIndex(const int ordinal) const {
    return m_NodeUnknowns + ordinal;
}

/**
 * @brief   Returns the number of node-voltage unknowns in the system.
 * @return  Count of node-voltage unknowns.
 */
int MnaSystem::GetNodeUnknownCount() const {
    return m_NodeUnknowns;
}

/**
 * @brief   Returns assembled matrix entry \f$A(\text{row}, \text{col})\f$.
 * @param[in] row  Zero-based row index.
 * @param[in] col  Zero-based column index.
 * @return  Matrix entry value, or `0.0` when out of bounds or absent.
 */
double MnaSystem::Entry(const int row, const int col) const {
    const int size = GetSize();
    if (row < 0 || col < 0 || row >= size || col >= size) {
        return 0.0;
    }
    return m_Storage->Matrix.At(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
}

void MnaSystem::VisitNonZeros(void (*callback)(int, int, double, const void *), const void *context) const {
    const DenseMatrix &matrix = m_Storage->Matrix;
    const std::size_t n = matrix.Rows();
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            const double val = matrix.At(r, c);
            if (val != 0.0) {
                callback(static_cast<int>(r), static_cast<int>(c), val, context);
            }
        }
    }
}

/**
 * @brief   Materializes a dense copy of \f$A\f$ for inherently dense algorithms (e.g. QZ generalized eigenvalues in
 *          `.pz`).
 * @return  Dense copy of the assembled matrix.
 */
DenseMatrix MnaSystem::ToDenseMatrix() const {
    return m_Storage->Matrix;
}

/**
 * @brief   Returns the assembled right-hand side vector.
 * @return  Const reference to the right-hand side vector.
 */
const std::vector<double> &MnaSystem::Rhs() const {
    return m_Rhs;
}

/**
 * @brief   Reports whether an unknown index represents a node voltage or a branch current.
 * @param[in] index  Zero-based unknown index.
 * @return  True when `index` is a node voltage (KCL row); false when it is a branch current or out of range.
 */
bool MnaSystem::IsNodeUnknown(const int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < m_IsNodeUnknown.size()
        && m_IsNodeUnknown[static_cast<std::size_t>(index)];
}

/**
 * @brief   Adds `value` to matrix entry \f$A(\text{row}, \text{col})\f$; silently ignores negative indices (ground).
 * @param[in] row    Zero-based row index, or negative for ground.
 * @param[in] col    Zero-based column index, or negative for ground.
 * @param[in] value  Contribution to accumulate.
 */
void MnaSystem::AddA(const int row, const int col, const double value) {
    if (row < 0 || col < 0) {
        return;
    }
    m_Storage->Matrix.Add(static_cast<std::size_t>(row), static_cast<std::size_t>(col), value);
}

/**
 * @brief   Adds every non-zero matrix entry of `source` into this system's matrix \f$A\f$.
 * @param[in] source  System whose assembled matrix entries are added.
 */
void MnaSystem::AddMatrixFrom(const MnaSystem &source) {
    source.ForEachNonZero([this](const int row, const int col, const double value) { AddA(row, col, value); });
}

/**
 * @brief   Adds `value` to right-hand side entry \f$\text{rhs}(\text{row})\f$; silently ignores negative indices
 *          (ground).
 * @param[in] row    Zero-based row index, or negative for ground.
 * @param[in] value  Contribution to accumulate.
 */
void MnaSystem::AddRhs(const int row, const double value) {
    if (row < 0) {
        return;
    }
    m_Rhs[static_cast<std::size_t>(row)] += value;
}

/**
 * @brief   Adds a small conductance \f$G_{\min}\f$ from every node unknown to ground to regularize floating/cutset
 *          nodes.
 * @param[in] conductance  Conductance to ground in Siemens.
 */
void MnaSystem::AddGminToNodes(const double conductance) {
    const int size = GetSize();
    for (int index = 0; index < size; ++index) {
        if (IsNodeUnknown(index)) {
            AddA(index, index, conductance);
        }
    }
}

/**
 * @brief   Registers a frequency-dependent matrix delta callback invoked per \f$\omega\f$.
 * @param[in] stamp  Callback and context for the distributed/dispersive element.
 */
void MnaSystem::AddFrequencyStamp(const FrequencyStamp stamp) {
    if (stamp.Stamp != nullptr) {
        m_FrequencyStamps.push_back(stamp);
    }
}

/**
 * @brief   Returns the frequency-dependent matrix delta callbacks registered with this reactive system.
 * @return  Registered frequency stamp callbacks.
 */
const std::vector<FrequencyStamp> &MnaSystem::FrequencyStamps() const {
    return m_FrequencyStamps;
}

/**
 * @brief   Zeros both \f$A\f$ and \f$\text{rhs}\f$ before stamping a new Newton iteration.
 */
void MnaSystem::Clear() {
    m_Storage->Matrix.Reset();
    std::ranges::fill(m_Rhs, 0.0);
    m_FrequencyStamps.clear();
}

/**
 * @brief   Zeros only \f$\text{rhs}\f$ while preserving the linearized matrix \f$A\f$ (used for small-signal `.tf`
 *          solves).
 */
void MnaSystem::ClearRhs() {
    std::ranges::fill(m_Rhs, 0.0);
}

/**
 * @brief   Solves \f$A \cdot x = \text{rhs}\f$ via LU decomposition with partial pivoting.
 * @param[out] x  Solution vector.
 * @return  False if the matrix is singular.
 */
bool MnaSystem::Solve(std::vector<double> &x) const {
    return SolveRealSystem(m_Storage->Matrix, m_Rhs, x);
}

/**
 * @brief   Solves the transposed (adjoint) system \f$A^T \cdot x = \text{rhs}\f$ via LU decomposition with partial
 *          pivoting.
 * @param[in]  rhs  Right-hand side vector.
 * @param[out] x    Adjoint solution vector.
 * @return  False if the matrix is singular or `rhs` size does not match.
 */
bool MnaSystem::SolveTranspose(const std::vector<double> &rhs, std::vector<double> &x) const {
    const DenseMatrix &matrix = m_Storage->Matrix;
    const std::size_t n = matrix.Rows();
    if (rhs.size() != n) {
        return false;
    }
    DenseMatrix a_transpose(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            a_transpose.At(r, c) = matrix.At(c, r);
        }
    }
    return SolveRealSystem(a_transpose, rhs, x);
}

/**
 * @brief   Solves the small-signal complex AC system \f$(A_{\text{DC}} + j \omega C_{\text{AC}}) \cdot x_{\text{AC}} =
 *          b_{\text{AC}}\f$.
 * @param[in]  reactive  System whose matrix holds the reactive coefficients \f$C_{\text{AC}}\f$ and whose RHS holds
 *                       the small-signal AC source excitation \f$b_{\text{AC}}\f$.
 * @param[in]  omega     Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[out] x_ac      Complex small-signal solution vector (node phasors and branch current phasors).
 * @return  False if the complex matrix at frequency \f$\omega\f$ is singular.
 */
bool MnaSystem::SolveAc(const MnaSystem &reactive, const double omega, std::vector<std::complex<double>> &x_ac) const {
    const std::size_t n = m_Storage->Matrix.Rows();
    if (reactive.m_Storage->Matrix.Rows() != n) {
        return false;
    }
    std::vector<std::complex<double>> b_orig(n);
    for (std::size_t r = 0; r < n; ++r) {
        b_orig[r] = std::complex<double>(reactive.m_Rhs[r], 0.0);
    }
    return SolveAcFor(reactive, omega, b_orig, x_ac);
}

/**
 * @brief   Solves the small-signal complex AC system \f$(A_{\text{DC}} + j \omega C_{\text{AC}}) \cdot x_{\text{AC}} =
 *          \text{rhs}\f$ for an explicit complex right-hand side.
 * @param[in]  reactive  System holding the reactive coefficients \f$C_{\text{AC}}\f$.
 * @param[in]  omega     Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[in]  rhs       Complex right-hand side excitation vector.
 * @param[out] x_ac      Complex small-signal solution vector.
 * @return  False if the complex matrix at frequency \f$\omega\f$ is singular or sizes differ.
 */
bool MnaSystem::SolveAcFor(const MnaSystem &reactive,
                           const double omega,
                           const std::vector<std::complex<double>> &rhs,
                           std::vector<std::complex<double>> &x_ac) const {
    const DenseMatrix &dc_matrix = m_Storage->Matrix;
    const DenseMatrix &ac_matrix = reactive.m_Storage->Matrix;
    const std::size_t n = dc_matrix.Rows();
    if (ac_matrix.Rows() != n || rhs.size() != n) {
        return false;
    }
    return SolveComplexSystem(AssembleAcMatrix(dc_matrix, ac_matrix, omega, false, reactive.m_FrequencyStamps),
                              rhs,
                              x_ac);
}

/**
 * @brief   Solves the small-signal complex AC system for multiple right-hand sides, reusing the LU factorization.
 * @param[in]  reactive   Reactive system holding \f$C_{\text{AC}}\f$.
 * @param[in]  omega      Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[in]  rhs_list   List of complex right-hand side excitation vectors.
 * @param[out] solutions  Output list of complex solution vectors.
 * @return  False if the complex matrix at frequency \f$\omega\f$ is singular.
 */
bool MnaSystem::SolveAcMultiple(const MnaSystem &reactive,
                                const double omega,
                                const std::vector<std::vector<std::complex<double>>> &rhs_list,
                                std::vector<std::vector<std::complex<double>>> &solutions) const {
    const DenseMatrix &dc_matrix = m_Storage->Matrix;
    const DenseMatrix &ac_matrix = reactive.m_Storage->Matrix;
    const std::size_t n = dc_matrix.Rows();
    if (ac_matrix.Rows() != n) {
        return false;
    }
    return SolveComplexSystemMultiple(AssembleAcMatrix(dc_matrix, ac_matrix, omega, false, reactive.m_FrequencyStamps),
                                      rhs_list,
                                      solutions);
}

/**
 * @brief   Solves the transposed (adjoint) small-signal AC system \f$(A_{\text{DC}} + j \omega C_{\text{AC}})^T \cdot
 *          \lambda = e_{\text{out}}\f$ for a unit excitation at `output_unknown`, used by small-signal noise analysis
 *          (`.noise`).
 * @param[in]  reactive        Reactive system holding \f$C_{\text{AC}}\f$.
 * @param[in]  omega           Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[in]  output_unknown  Zero-based unknown index receiving unit excitation.
 * @param[out] adjoint         Complex adjoint solution vector.
 * @return  False if the matrix is singular, sizes differ, or `output_unknown` is out of range.
 */
bool MnaSystem::SolveAcTranspose(const MnaSystem &reactive,
                                 const double omega,
                                 const int output_unknown,
                                 std::vector<std::complex<double>> &adjoint) const {
    const std::size_t n = m_Storage->Matrix.Rows();
    if (reactive.m_Storage->Matrix.Rows() != n || output_unknown < 0 || static_cast<std::size_t>(output_unknown) >= n) {
        return false;
    }

    std::vector<std::complex<double>> b_orig(n, std::complex<double>(0.0, 0.0));
    b_orig[static_cast<std::size_t>(output_unknown)] = std::complex<double>(1.0, 0.0);
    return SolveAcTransposeFor(reactive, omega, b_orig, adjoint);
}

/**
 * @brief   Solves the transposed small-signal system \f$Y(j\omega)^T \cdot \lambda = \text{rhs}\f$ for any complex
 *          right-hand side, which AC sensitivity uses for a differential output.
 * @param[in]  reactive  Reactive system holding \f$C_{\text{AC}}\f$.
 * @param[in]  omega     Angular frequency \f$\omega = 2\pi f\f$ in rad/s.
 * @param[in]  rhs       Complex right-hand side vector.
 * @param[out] adjoint   Complex adjoint solution vector.
 * @return  False if the matrix is singular or the sizes differ.
 */
bool MnaSystem::SolveAcTransposeFor(const MnaSystem &reactive,
                                    const double omega,
                                    const std::vector<std::complex<double>> &rhs,
                                    std::vector<std::complex<double>> &adjoint) const {
    const DenseMatrix &dc_matrix = m_Storage->Matrix;
    const DenseMatrix &ac_matrix = reactive.m_Storage->Matrix;
    const std::size_t n = dc_matrix.Rows();
    if (ac_matrix.Rows() != n || rhs.size() != n) {
        return false;
    }
    return SolveComplexSystem(AssembleAcMatrix(dc_matrix, ac_matrix, omega, true, reactive.m_FrequencyStamps),
                              rhs,
                              adjoint);
}

/**
 * @brief   Solves \f$A^T \cdot x = \text{rhs}\f$ for a complex right-hand side, where \f$A\f$ is the real matrix of
 *          the system, as the DC adjoint of a small-signal quantity needs.
 * @param[in]  rhs  Complex right-hand side vector.
 * @param[out] x    Complex adjoint solution vector.
 * @return  False if the matrix is singular or the sizes differ.
 */
bool MnaSystem::SolveTransposeComplex(const std::vector<std::complex<double>> &rhs,
                                      std::vector<std::complex<double>> &x) const {
    const DenseMatrix &matrix = m_Storage->Matrix;
    const std::size_t n = matrix.Rows();
    if (rhs.size() != n) {
        return false;
    }
    ComplexDenseMatrix a_transpose(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            a_transpose.At(r, c) = std::complex<double>(matrix.At(c, r), 0.0);
        }
    }
    return SolveComplexSystem(a_transpose, rhs, x);
}

/**
 * @brief   Verifies that the equation residual \f$|A \cdot x - \text{rhs}|\f$ satisfies KCL and branch tolerances.
 * @param[in] x               Candidate solution vector.
 * @param[in] voltage_abstol  Absolute tolerance for branch voltage equations in Volts.
 * @param[in] current_abstol  Absolute tolerance for node KCL equations in Amperes.
 * @param[in] reltol          Relative tolerance applied to the largest term or RHS entry per row.
 * @return  True when every equation row satisfies its residual tolerance.
 */
bool MnaSystem::CheckResidual(const std::vector<double> &x,
                              const double voltage_abstol,
                              const double current_abstol,
                              const double reltol) const {
    const DenseMatrix &matrix = m_Storage->Matrix;
    const int size = GetSize();
    for (int row = 0; row < size; ++row) {
        double sum = 0.0;
        double sum_abs = 0.0;
        for (int col = 0; col < size; ++col) {
            const double val = matrix.At(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
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
