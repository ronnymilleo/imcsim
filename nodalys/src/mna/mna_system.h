/**
 * @file    mna_system.h
 * @brief   The Modified Nodal Analysis (MNA) linear system assembled and solved at each Newton-Raphson iteration.
 * @details
 * ### How Modified Nodal Analysis (MNA) Works
 * A circuit with \f$N\f$ non-ground nodes and \f$M\f$ voltage-defined branches (independent voltage sources, inductors,
 * VCVS, CCVS, current probes) produces a linear system of size \f$(N + M) \times (N + M)\f$:
 *
 * ```text
 *   [ G   B ] [ v ] = [ i_rhs ]   <-- Node rows: KCL (sum of currents leaving node = 0), in Amperes
 *   [ C   D ] [ j ]   [ e_rhs ]   <-- Branch rows: branch constitutive equations (e.g. v_a - v_b = E), in Volts
 * ```
 *
 * - **Ground (node 0)** is the reference potential (\f$0\text{ V}\f$) and has index `-1`. Passing `-1` to `AddA()` or
 *   `AddRhs()` is a no-op, so device stamps never need special `if (node != ground)` branches.
 * - **Unknown indices (`0 .. GetSize() - 1`)**:
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

#include "nodalys/types.h"
#include <complex>
#include <memory>
#include <vector>

namespace Nodalys {

template <typename Scalar>
class BasicDenseMatrix;
/// Double-precision dense matrix alias.
using DenseMatrix = BasicDenseMatrix<double>;

/**
 * @struct  ComplexStamper
 * @brief   Sink receiving complex MNA matrix entries `(row, col, delta)` at angular frequency \f$\omega\f$, ignoring
 *          negative indices (ground) like `MnaSystem::AddA()`.
 */
struct ComplexStamper {
    /// Callback receiving a complex matrix entry `(row, col, value)` and opaque `context`.
    using Callback = void (*)(int row, int col, std::complex<double> value, void *context);

    Callback Apply = nullptr;
    void *Context = nullptr;

    /**
     * @brief   Forwards a complex matrix entry to `Apply`, ignoring negative (ground) indices or a null callback.
     * @param[in] row    Zero-based MNA row index, or negative for ground (ignored).
     * @param[in] col    Zero-based MNA column index, or negative for ground (ignored).
     * @param[in] value  Complex admittance or stamp contribution added at `(row, col)`.
     */
    void Add(const int row, const int col, const std::complex<double> value) const {
        if (row < 0 || col < 0 || Apply == nullptr) {
            return;
        }
        Apply(row, col, value, Context);
    }
};

/**
 * @struct  FrequencyStamp
 * @brief   Type-erased frequency-dependent matrix delta callback registered on a reactive `MnaSystem`.
 */
struct FrequencyStamp {
    /// Callback stamping frequency-dependent matrix entries into `stamper` at angular frequency `omega`.
    using Callback = void (*)(const void *context, const ComplexStamper &stamper, double omega);

    Callback Stamp = nullptr;
    const void *Context = nullptr;

    /**
     * @brief   Invokes the frequency-dependent stamp at angular frequency `omega` when `Stamp` is non-null.
     * @param[in] stamper  Sink receiving complex MNA matrix entries.
     * @param[in] omega    Angular frequency \f$\omega = 2\pi f\f$, in radians per second.
     */
    void Apply(const ComplexStamper &stamper, const double omega) const {
        if (Stamp != nullptr) {
            Stamp(Context, stamper, omega);
        }
    }
};

/**
 * @struct  TimeContext
 * @brief   Describes the analysis point at which devices are being stamped.
 */
struct TimeContext {
    /// False during DC operating point or DC sweep (capacitors are open circuits; inductors are short circuits).
    bool IsTransient = false;
    /// True at t = 0 when starting a transient from user-specified initial conditions (`uic`).
    bool UseInitialConditions = false;
    /// True at t = 0 of a transient solve (both with and without `uic`); `.ic` node voltages clamp only the solve that
    /// is not `uic`, the operating point that starts the transient.
    bool IsTransientStart = false;
    /// False for the solves that continue a sweep, which start from the point before them and not from the nodesets.
    bool AppliesNodesets = true;
    /// True on the first integration step of a transient and on the step that leaves a source corner: uses Backward
    /// Euler (1st-order, needs no past derivatives) whatever `Method` says.
    bool BackwardEuler = false;
    /// Second-order formula of the steps that are not Backward Euler.
    IntegrationMethod Method = IntegrationMethod::TRAPEZOIDAL;
    /// Length of the step before this one, in seconds; Gear-2 needs it for its variable-step coefficients.
    double PreviousTimeStep = 0.0;
    /// True while evaluating periodic steady-state waveforms over one Harmonic Balance cycle.
    bool IsPeriodicSteadyState = false;
    double Time = 0.0; ///< Current simulation time \f$t\f$, in seconds.
    /// Integration step size \f$h = \Delta t\f$, in seconds (used only when `IsTransient` is true).
    double TimeStep = 0.0;
};

/**
 * @class   MnaSystem
 * @brief   Holds the linearized matrix \f$A\f$ and right-hand side \f$\text{rhs}\f$ for one Newton-Raphson step.
 */
class MnaSystem {
public:
    MnaSystem(int node_count, int branch_count);
    explicit MnaSystem(std::vector<bool> is_node_unknown);
    MnaSystem(const MnaSystem &other);
    MnaSystem &operator=(const MnaSystem &other);
    MnaSystem(MnaSystem &&other) noexcept;
    MnaSystem &operator=(MnaSystem &&other) noexcept;
    ~MnaSystem();

    int GetSize() const;
    int NodeIndex(int node) const;
    int BranchIndex(int ordinal) const;
    int GetNodeUnknownCount() const;
    [[nodiscard]] double Entry(int row, int col) const;

    /**
     * @brief   Visits every non-zero entry `(row, col, value)` of the assembled matrix \f$A\f$.
     * @details Storage-agnostic iteration hook so analyses never depend on dense vs. sparse matrix layout.
     * @param[in] visitor  Callable invoked as `visitor(row, col, value)` for each non-zero entry.
     */
    template <typename Visitor>
    void ForEachNonZero(const Visitor &visitor) const {
        VisitNonZeros(
            [](const int row, const int col, const double value, const void *context) {
                (*static_cast<const Visitor *>(context))(row, col, value);
            },
            &visitor);
    }

    [[nodiscard]] DenseMatrix ToDenseMatrix() const;
    const std::vector<double> &Rhs() const;
    bool IsNodeUnknown(int index) const;
    void AddA(int row, int col, double value);
    void AddMatrixFrom(const MnaSystem &source);
    void AddRhs(int row, double value);
    void AddGminToNodes(double conductance);
    void AddFrequencyStamp(FrequencyStamp stamp);
    [[nodiscard]] const std::vector<FrequencyStamp> &FrequencyStamps() const;
    void Clear();
    void ClearRhs();
    bool Solve(std::vector<double> &x) const;
    bool SolveTranspose(const std::vector<double> &rhs, std::vector<double> &x) const;
    bool SolveAc(const MnaSystem &reactive, double omega, std::vector<std::complex<double>> &x_ac) const;
    [[nodiscard]] bool SolveAcFor(const MnaSystem &reactive,
                                  double omega,
                                  const std::vector<std::complex<double>> &rhs,
                                  std::vector<std::complex<double>> &x_ac) const;
    bool SolveAcMultiple(const MnaSystem &reactive,
                         double omega,
                         const std::vector<std::vector<std::complex<double>>> &rhs_list,
                         std::vector<std::vector<std::complex<double>>> &solutions) const;
    bool SolveAcTranspose(const MnaSystem &reactive,
                          double omega,
                          int output_unknown,
                          std::vector<std::complex<double>> &adjoint) const;
    bool SolveAcTransposeFor(const MnaSystem &reactive,
                             double omega,
                             const std::vector<std::complex<double>> &rhs,
                             std::vector<std::complex<double>> &adjoint) const;
    bool SolveTransposeComplex(const std::vector<std::complex<double>> &rhs,
                               std::vector<std::complex<double>> &x) const;
    bool CheckResidual(const std::vector<double> &x, double voltage_abstol, double current_abstol, double reltol) const;

private:
    struct Storage;

    std::unique_ptr<Storage> m_Storage;
    std::vector<double> m_Rhs;
    std::vector<bool> m_IsNodeUnknown;
    std::vector<FrequencyStamp> m_FrequencyStamps;
    int m_NodeUnknowns = 0;

    void VisitNonZeros(void (*callback)(int, int, double, const void *), const void *context) const;
};

} // namespace Nodalys

#endif // NODALYS_MNA_SYSTEM_H
