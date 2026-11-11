/**
 * @file    mna_system.h
 * @brief   The Modified Nodal Analysis (MNA) linear system assembled and solved at each Newton-Raphson iteration.
 * @details
 * ### How Modified Nodal Analysis (MNA) Works
 * A circuit with $N$ non-ground nodes and $M$ voltage-defined branches (independent voltage sources, inductors,
 * VCVS, CCVS, current probes) produces a linear system of size $(N + M) \times (N + M)$:
 *
 * ```text
 *   [ G   B ] [ v ] = [ i_rhs ]   <-- Node rows: KCL (sum of currents leaving node = 0), in Amperes
 *   [ C   D ] [ j ]   [ e_rhs ]   <-- Branch rows: branch constitutive equations (e.g. v_a - v_b = E), in Volts
 * ```
 *
 * - **Ground (node 0)** is the reference potential ($0\text{ V}$) and has index `-1`. Passing `-1` to `AddA()` or
 *   `AddRhs()` is a no-op, so device stamps never need special `if (node != ground)` branches.
 * - **Unknown indices (`0 .. Size() - 1`)**:
 *   - Top-level circuit nodes `1 .. CircuitNodeCount - 1` map directly to unknown indices `0 .. CircuitNodeCount - 2`
 *     via `NodeIndex(node) = node - 1`.
 *   - Internal nodes (created for subcircuits, series resistances, or macromodels) and branch current unknowns are
 *     allocated dynamically by the compiler in a single pass. `IsNodeUnknown(index)` tells whether unknown `index`
 *     is a node voltage (`true`) or a branch current (`false`).
 *
 * References:
 * - Vlach & Singhal, *Computer Methods for Circuit Analysis and Design*
 * - Chua & Lin, *Computer-Aided Analysis of Electronic Circuits*
 */

#ifndef NODALYS_MNA_SYSTEM_H
#define NODALYS_MNA_SYSTEM_H

#include "linear/dense_matrix.h"
#include <vector>

namespace nodalys {

/**
 * @struct  TimeContext
 * @brief   Describes the analysis point at which devices are being stamped.
 */
struct TimeContext {
    /// False during DC operating point or DC sweep (capacitors are open circuits; inductors are short circuits).
    bool IsTransient = false;
    /// True at t = 0 when starting a transient from user-specified initial conditions (`uic`).
    bool UseInitialConditions = false;
    /// True on the first integration step of a transient: uses Backward Euler (1st-order, needs no past derivatives)
    /// to bootstrap the state before switching to the Trapezoidal rule (2nd-order) on subsequent steps.
    bool BackwardEuler = false;
    /// Current simulation time $t$, in seconds.
    double Time = 0.0;
    /// Integration step size $h = \Delta t$, in seconds (used only when `IsTransient` is true).
    double TimeStep = 0.0;
};

/**
 * @class   MnaSystem
 * @brief   Holds the linearized matrix $A$ and right-hand side $\text{rhs}$ for one Newton-Raphson step.
 */
class MnaSystem {
public:
    MnaSystem(int node_count, int branch_count);
    explicit MnaSystem(std::vector<bool> is_node_unknown);

    /// Total number of unknowns (node voltages + branch currents).
    int Size() const;

    /// Maps a 1-based circuit node number to its 0-based unknown index, or -1 for ground (node 0).
    int NodeIndex(int node) const;

    /// Maps a 0-based branch ordinal to its unknown index (used when all nodes precede all branches).
    int BranchIndex(int ordinal) const;

    /// Number of node-voltage unknowns in the system.
    int NodeUnknowns() const;

    /// The assembled linearized dense matrix A.
    const DenseMatrix &Matrix() const;

    /// The assembled right-hand side vector.
    const std::vector<double> &Rhs() const;

    /// True when unknown `index` is a node voltage (KCL row); false when it is a branch current (branch voltage row).
    bool IsNodeUnknown(int index) const;

    /// Adds `value` to matrix entry $A(\text{row}, \text{col})$. Silently ignores negative indices (ground).
    void AddA(int row, int col, double value);

    /// Adds `value` to right-hand side entry $\text{rhs}(\text{row})$. Silently ignores negative indices (ground).
    void AddRhs(int row, double value);

    /// Adds a small conductance $G_{\min}$ from every node unknown to ground to regularize floating/cutset nodes.
    void AddGminToNodes(double conductance);

    /// Zeros both $A$ and $\text{rhs}$ before stamping a new Newton iteration.
    void Clear();

    /// Zeros only $\text{rhs}$ while preserving the linearized matrix $A$ (used for small-signal `.tf` solves).
    void ClearRhs();

    /// Solves $A \cdot x = \text{rhs}$ via LU decomposition with partial pivoting; returns false if singular.
    bool Solve(std::vector<double> &x) const;

    /// Solves the transposed (adjoint) system $A^T \cdot x = \text{rhs}$ via LU decomposition with partial pivoting.
    bool SolveTranspose(const std::vector<double> &rhs, std::vector<double> &x) const;

    /**
     * @brief   Solves the small-signal complex AC system $(A_{\text{DC}} + j \omega C_{\text{AC}}) \cdot x_{\text{AC}}
     * = b_{\text{AC}}$.
     * @param[in]  reactive  System whose matrix holds the reactive coefficients $C_{\text{AC}}$ and whose RHS holds the
     *                       small-signal AC source excitation $b_{\text{AC}}$.
     * @param[in]  omega     Angular frequency $\omega = 2\pi f$ in rad/s.
     * @param[out] x_ac      Complex small-signal solution vector (node phasors and branch current phasors).
     * @return  False if the complex matrix at frequency $\omega$ is singular.
     */
    bool SolveAc(const MnaSystem &reactive, double omega, std::vector<std::complex<double>> &x_ac) const;

    /**
     * @brief   Solves the small-signal complex AC system for multiple right-hand sides, reusing the LU factorization.
     * @param[in]  reactive  Reactive system (C_ac).
     * @param[in]  omega     Angular frequency in rad/s.
     * @param[in]  rhs_list  List of complex right-hand side excitation vectors.
     * @param[out] solutions Output list of complex solution vectors.
     * @return  False if the complex matrix at frequency $\omega$ is singular.
     */
    bool SolveAcMultiple(const MnaSystem &reactive, double omega,
                         const std::vector<std::vector<std::complex<double>>> &rhs_list,
                         std::vector<std::vector<std::complex<double>>> &solutions) const;

    /**
     * @brief   Solves the transposed (adjoint) small-signal AC system $(A_{\text{DC}} + j \omega C_{\text{AC}})^T \cdot
     * \lambda = e_{\text{out}}$ for a unit excitation at `output_unknown`, used by small-signal noise analysis
     * (`.noise`).
     */
    bool SolveAcTranspose(const MnaSystem &reactive, double omega, int output_unknown,
                          std::vector<std::complex<double>> &adjoint) const;

    /// Verifies that the equation residual $|A \cdot x - \text{rhs}|$ satisfies KCL and branch tolerances.
    bool CheckResidual(const std::vector<double> &x, double voltage_abstol, double current_abstol, double reltol) const;

private:
    DenseMatrix m_A;
    std::vector<double> m_Rhs;
    std::vector<bool> m_IsNodeUnknown;
    int m_NodeUnknowns = 0;
};

} // namespace nodalys

#endif // NODALYS_MNA_SYSTEM_H
