/**
 * @file    qz.cpp
 * @brief   Generalized eigenvalue solver for real matrix pencils (A, B) via the QZ algorithm (Moler-Stewart).
 * @details Implements the Moler-Stewart generalized Schur decomposition (SIAM J. Numer. Anal. 1973):
 *          1. Qzhes: reduction of (A, B) to upper Hessenberg-triangular form via Householder reflections.
 *          2. Qzit: iterative QZ step with implicit Francis double/single shifts and deflation.
 *          3. Qzval: extraction of finite eigenvalues from quasi-triangular and triangular diagonal blocks.
 */

#include "linear/qz.h"

#include "linear/pencil_reduction.h"
#include "linear/root_refinement.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Nodalys {

namespace {

/// Size of B, relative to its largest entry, at or below which an eigenvalue counts as infinite.
constexpr double INFINITE_EIGENVALUE_FRACTION = 1e-13;

/// Unitary 2x2 complex Givens rotation used when splitting a complex-conjugate 2x2 block.
struct ComplexRotation {
    double C = 0.0;
    double Sr = 1.0;
    double Si = 0.0;
};

/// Generalized eigenvalue numerators (Alfr + i*Alfi) and denominators (Beta) for all n diagonal slots.
struct EigenvalueArrays {
    std::vector<double> Alfr;
    std::vector<double> Alfi;
    std::vector<double> Beta;
};

/// Two-element Householder reflector parameterized as I + [v1, v2]^T * [1, u2].
struct Householder2 {
    double V1 = 0.0;
    double V2 = 0.0;
    double U2 = 0.0;
    bool Active = false;
};

/// Three-element Householder reflector parameterized as I + [v1, v2, v3]^T * [1, u2, u3].
struct Householder3 {
    double V1 = 0.0;
    double V2 = 0.0;
    double V3 = 0.0;
    double U2 = 0.0;
    double U3 = 0.0;
    bool Active = false;
};

/// Implicit single- or double-shift starter computed for one QZ bulge-chasing sweep.
struct QzShift {
    int Order = 2;
    std::size_t Start = 0;
    double A1 = 0.0;
    double A2 = 0.0;
    double A3 = 0.0;
};

/// Normalized entries of a 2x2 quasi-triangular block (A, B) for eigenvalue extraction.
struct ScaledBlock2x2 {
    double An = 0.0;
    double Bn = 0.0;
    double A11 = 0.0;
    double A12 = 0.0;
    double A21 = 0.0;
    double A22 = 0.0;
    double B11 = 0.0;
    double B12 = 0.0;
    double B22 = 0.0;
};

// Householder and rotation primitives

Householder2 MakeHouseholder2(const double x1, const double x2) {
    const double s = std::abs(x1) + std::abs(x2);
    if (s == 0.0) {
        return {};
    }
    const double u1 = x1 / s;
    const double u2 = x2 / s;
    const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
    const double v1 = -(u1 + r) / r;
    const double v2 = -u2 / r;
    return {.V1 = v1, .V2 = v2, .U2 = v2 / v1, .Active = true};
}

Householder3 MakeHouseholder3(const double x1, const double x2, const double x3) {
    const double s = std::abs(x1) + std::abs(x2) + std::abs(x3);
    if (s == 0.0) {
        return {};
    }
    const double u1 = x1 / s;
    const double u2 = x2 / s;
    const double u3 = x3 / s;
    const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2 + u3 * u3), u1);
    const double v1 = -(u1 + r) / r;
    const double v2 = -u2 / r;
    const double v3 = -u3 / r;
    return {.V1 = v1, .V2 = v2, .V3 = v3, .U2 = v2 / v1, .U3 = v3 / v1, .Active = true};
}

void ApplyLeftHouseholder2(DenseMatrix &mat,
                           const std::size_t r0,
                           const std::size_t r1,
                           const std::size_t col_start,
                           const std::size_t col_end,
                           const Householder2 &h) {
    for (std::size_t j = col_start; j < col_end; ++j) {
        const double t = mat.At(r0, j) + h.U2 * mat.At(r1, j);
        mat.At(r0, j) += t * h.V1;
        mat.At(r1, j) += t * h.V2;
    }
}

void ApplyRightHouseholder2(DenseMatrix &mat,
                            const std::size_t c0,
                            const std::size_t c1,
                            const std::size_t row_start,
                            const std::size_t row_end,
                            const Householder2 &h) {
    for (std::size_t i = row_start; i < row_end; ++i) {
        const double t = mat.At(i, c0) + h.U2 * mat.At(i, c1);
        mat.At(i, c0) += t * h.V1;
        mat.At(i, c1) += t * h.V2;
    }
}

void ApplyLeftHouseholder3(DenseMatrix &mat,
                           const std::size_t r0,
                           const std::size_t r1,
                           const std::size_t r2,
                           const std::size_t col_start,
                           const std::size_t col_end,
                           const Householder3 &h) {
    for (std::size_t j = col_start; j < col_end; ++j) {
        const double t = mat.At(r0, j) + h.U2 * mat.At(r1, j) + h.U3 * mat.At(r2, j);
        mat.At(r0, j) += t * h.V1;
        mat.At(r1, j) += t * h.V2;
        mat.At(r2, j) += t * h.V3;
    }
}

void ApplyRightHouseholder3(DenseMatrix &mat,
                            const std::size_t c0,
                            const std::size_t c1,
                            const std::size_t c2,
                            const std::size_t row_start,
                            const std::size_t row_end,
                            const Householder3 &h) {
    for (std::size_t i = row_start; i < row_end; ++i) {
        const double t = mat.At(i, c0) + h.U2 * mat.At(i, c1) + h.U3 * mat.At(i, c2);
        mat.At(i, c0) += t * h.V1;
        mat.At(i, c1) += t * h.V2;
        mat.At(i, c2) += t * h.V3;
    }
}

ComplexRotation MakeComplexRotation(const double a1, const double a1i, const double a2, const double a2i) {
    const double c = std::sqrt(a1 * a1 + a1i * a1i);
    if (c == 0.0) {
        return {.C = 0.0, .Sr = 1.0, .Si = 0.0};
    }
    const double sr = (a1 * a2 + a1i * a2i) / c;
    const double si = (a1 * a2i - a1i * a2) / c;
    const double r = std::sqrt(c * c + sr * sr + si * si);
    return {.C = c / r, .Sr = sr / r, .Si = si / r};
}

// Phase 1: Hessenberg-triangular reduction (Qzhes)

void ApplyColumnReflector(DenseMatrix &target,
                          const DenseMatrix &v_source,
                          const std::size_t l,
                          const std::size_t col_start,
                          const double rho) {
    const std::size_t n = target.Rows();
    for (std::size_t j = col_start; j < n; ++j) {
        double t = 0.0;
        for (std::size_t i = l; i < n; ++i) {
            t += v_source.At(i, l) * target.At(i, j);
        }
        t = -t / rho;
        for (std::size_t i = l; i < n; ++i) {
            target.At(i, j) += t * v_source.At(i, l);
        }
    }
}

void TriangularizeB(DenseMatrix &a, DenseMatrix &b) {
    const std::size_t n = a.Rows();
    for (std::size_t l = 0; l < n - 1; ++l) {
        const std::size_t l1 = l + 1;
        double s = 0.0;
        for (std::size_t i = l1; i < n; ++i) {
            s += std::abs(b.At(i, l));
        }
        if (s == 0.0) {
            continue;
        }
        s += std::abs(b.At(l, l));
        double r = 0.0;
        for (std::size_t i = l; i < n; ++i) {
            b.At(i, l) /= s;
            r += b.At(i, l) * b.At(i, l);
        }
        r = std::copysign(std::sqrt(r), b.At(l, l));
        b.At(l, l) += r;
        const double rho = r * b.At(l, l);

        ApplyColumnReflector(b, b, l, l1, rho);
        ApplyColumnReflector(a, b, l, 0, rho);
        b.At(l, l) = -s * r;
        for (std::size_t i = l1; i < n; ++i) {
            b.At(i, l) = 0.0;
        }
    }
}

void ReduceAWithUpperB(DenseMatrix &a, DenseMatrix &b) {
    const std::size_t n = a.Rows();
    for (std::size_t k = 0; k < n - 2; ++k) {
        for (std::size_t lb = 1; lb <= n - 1 - (k + 1); ++lb) {
            const std::size_t l = (n - 1) - lb;
            const std::size_t l1 = l + 1;

            // Annihilate A(l1, k) from the left
            if (const Householder2 h = MakeHouseholder2(a.At(l, k), a.At(l1, k)); h.Active) {
                ApplyLeftHouseholder2(a, l, l1, k, n, h);
                a.At(l1, k) = 0.0;
                ApplyLeftHouseholder2(b, l, l1, l, n, h);
            }

            // Restore upper triangular B by annihilating B(l1, l) from the right
            if (const Householder2 h = MakeHouseholder2(b.At(l1, l1), b.At(l1, l)); h.Active) {
                ApplyRightHouseholder2(b, l1, l, 0, l1 + 1, h);
                b.At(l1, l) = 0.0;
                ApplyRightHouseholder2(a, l1, l, 0, n, h);
            }
        }
    }
}

/**
 * @brief   Reduces matrix pair (A, B) to upper Hessenberg-triangular form in place.
 * @param[in,out] a  Matrix A reduced to upper Hessenberg form.
 * @param[in,out] b  Matrix B reduced to upper triangular form.
 */
void ReduceToHessenbergTriangular(DenseMatrix &a, DenseMatrix &b) {
    const std::size_t n = a.Rows();
    if (n <= 1) {
        return;
    }
    TriangularizeB(a, b);
    if (n > 2) {
        ReduceAWithUpperB(a, b);
    }
}

// Phase 2: Iterative QZ steps (Qzit)

std::pair<double, double> ComputePencilTolerances(const DenseMatrix &a, const DenseMatrix &b) {
    const std::size_t n = a.Rows();
    double anorm = 0.0;
    double bnorm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double ani = (i > 0) ? std::abs(a.At(i, i - 1)) : 0.0;
        double bni = 0.0;
        for (std::size_t j = i; j < n; ++j) {
            ani += std::abs(a.At(i, j));
            bni += std::abs(b.At(i, j));
        }
        anorm = std::max(anorm, ani);
        bnorm = std::max(bnorm, bni);
    }
    const double ep = std::numeric_limits<double>::epsilon();
    return {ep * (anorm == 0.0 ? 1.0 : anorm), ep * (bnorm == 0.0 ? 1.0 : bnorm)};
}

std::pair<std::size_t, bool> FindSubdiagonalSplit(DenseMatrix &a, const std::size_t en, const double epsa) {
    for (std::size_t ll = 0; ll <= en; ++ll) {
        const std::size_t l = en - ll;
        if (l == 0) {
            return {0, false};
        }
        if (std::abs(a.At(l, l - 1)) <= epsa) {
            a.At(l, l - 1) = 0.0;
            return {l, true};
        }
    }
    return {0, false};
}

bool DeflateZeroBDiagonal(DenseMatrix &a, DenseMatrix &b, std::size_t &l, const std::size_t en, const double epsb) {
    const std::size_t na = en - 1;
    while (std::abs(b.At(l, l)) <= epsb) {
        const std::size_t l1 = l + 1;
        b.At(l, l) = 0.0;
        if (const Householder2 h = MakeHouseholder2(a.At(l, l), a.At(l1, l)); h.Active) {
            ApplyLeftHouseholder2(a, l, l1, l, en + 1, h);
            ApplyLeftHouseholder2(b, l, l1, l, en + 1, h);
        }
        if (l > 0) {
            a.At(l, l - 1) = -a.At(l, l - 1);
        }
        l = l1;
        a.At(l, l - 1) = 0.0;
        if (l >= na) {
            return true;
        }
    }
    return false;
}

std::size_t FindSingleShiftStart(const DenseMatrix &a,
                                 const DenseMatrix &b,
                                 const std::size_t ld,
                                 const std::size_t enm2,
                                 const double sh,
                                 const double epsa,
                                 const double epsb) {
    for (std::size_t ll = ld; ll <= enm2; ++ll) {
        const std::size_t l = enm2 + ld - ll;
        if (l == ld) {
            return ld;
        }
        double t_val = a.At(l, l);
        if (std::abs(b.At(l, l)) > epsb) {
            t_val -= sh * b.At(l, l);
        }
        if (std::abs(a.At(l, l - 1)) <= std::abs(t_val / a.At(l + 1, l)) * epsa) {
            return l;
        }
    }
    return ld;
}

QzShift ComputeQzShift(DenseMatrix &a,
                       const DenseMatrix &b,
                       const std::size_t ld,
                       const std::size_t en,
                       const std::size_t its,
                       const double epsa,
                       const double epsb) {
    if (its == 10) {
        return {.Order = 2, .Start = ld, .A1 = 0.0, .A2 = 1.0, .A3 = 1.1605};
    }

    const std::size_t l1 = ld + 1;
    const std::size_t na = en - 1;
    const double a11 = a.At(ld, ld) / b.At(ld, ld);
    const double a21 = a.At(l1, ld) / b.At(ld, ld);
    const double b22 = std::abs(b.At(l1, l1)) < epsb ? epsb : b.At(l1, l1);
    const double b33 = std::abs(b.At(na, na)) < epsb ? epsb : b.At(na, na);
    const double b44 = std::abs(b.At(en, en)) < epsb ? epsb : b.At(en, en);

    const double a33 = a.At(na, na) / b33;
    const double a34 = a.At(na, en) / b44;
    const double a43 = a.At(en, na) / b33;
    const double a44 = a.At(en, en) / b44;
    const double b34 = b.At(na, en) / b44;

    const double t = 0.5 * (a43 * b34 - a33 - a44);
    const double r = t * t + a34 * a43 - a33 * a44;
    if (r >= 0.0) {
        const double sqrt_r = std::sqrt(r);
        double sh = -t + sqrt_r;
        const double s = -t - sqrt_r;
        if (std::abs(s - a44) < std::abs(sh - a44)) {
            sh = s;
        }
        const std::size_t start = FindSingleShiftStart(a, b, ld, na - 1, sh, epsa, epsb);
        if (start != ld) {
            a.At(start, start - 1) = -a.At(start, start - 1);
        }
        return {.Order = 1, .Start = start, .A1 = a11 - sh, .A2 = a21, .A3 = 0.0};
    }

    const double a12 = a.At(ld, l1) / b22;
    const double a22 = a.At(l1, l1) / b22;
    const double b12 = b.At(ld, l1) / b22;
    return {
        .Order = 2,
        .Start = ld,
        .A1 = ((a33 - a11) * (a44 - a11) - a34 * a43 + a43 * b34 * a11) / a21 + a12 - a11 * b12,
        .A2 = (a22 - a11) - a21 * b12 - (a33 - a11) - (a44 - a11) + a43 * b34,
        .A3 = a.At(l1 + 1, l1) / b22,
    };
}

void ChaseSingleShiftStep(DenseMatrix &a,
                          DenseMatrix &b,
                          const std::size_t k,
                          const std::size_t l,
                          const std::size_t ld,
                          const std::size_t en,
                          const std::size_t ll_bound,
                          const double init_a1,
                          const double init_a2) {
    const std::size_t k1 = k + 1;
    const std::size_t km1 = std::max(k > 0 ? k - 1 : 0, l);
    const double a1 = (k == l) ? init_a1 : a.At(k, km1);
    const double a2 = (k == l) ? init_a2 : a.At(k1, km1);

    const Householder2 left = MakeHouseholder2(a1, a2);
    if (!left.Active) {
        return;
    }
    ApplyLeftHouseholder2(a, k, k1, km1, en + 1, left);
    ApplyLeftHouseholder2(b, k, k1, km1, en + 1, left);
    if (k != l) {
        a.At(k1, km1) = 0.0;
    }

    if (const Householder2 right = MakeHouseholder2(b.At(k1, k1), b.At(k1, k)); right.Active) {
        ApplyRightHouseholder2(a, k1, k, ld, ll_bound + 1, right);
        ApplyRightHouseholder2(b, k1, k, ld, ll_bound + 1, right);
        b.At(k1, k) = 0.0;
    }
}

void ChaseDoubleShiftStep(DenseMatrix &a,
                          DenseMatrix &b,
                          const std::size_t k,
                          const std::size_t l,
                          const std::size_t ld,
                          const std::size_t en,
                          const std::size_t ll_bound,
                          const double init_a1,
                          const double init_a2,
                          const double init_a3) {
    const std::size_t k1 = k + 1;
    const std::size_t k2 = k + 2;
    const std::size_t km1 = std::max(k > 0 ? k - 1 : 0, l);
    const double a1 = (k == l) ? init_a1 : a.At(k, km1);
    const double a2 = (k == l) ? init_a2 : a.At(k1, km1);
    const double a3 = (k == l) ? init_a3 : a.At(k2, km1);

    const Householder3 left = MakeHouseholder3(a1, a2, a3);
    if (!left.Active) {
        return;
    }
    ApplyLeftHouseholder3(a, k, k1, k2, km1, en + 1, left);
    ApplyLeftHouseholder3(b, k, k1, k2, km1, en + 1, left);
    if (k != l) {
        a.At(k1, km1) = 0.0;
        a.At(k2, km1) = 0.0;
    }

    if (const Householder3 right3 = MakeHouseholder3(b.At(k2, k2), b.At(k2, k1), b.At(k2, k)); right3.Active) {
        ApplyRightHouseholder3(a, k2, k1, k, ld, ll_bound + 1, right3);
        ApplyRightHouseholder3(b, k2, k1, k, ld, ll_bound + 1, right3);
        b.At(k2, k) = 0.0;
        b.At(k2, k1) = 0.0;
    }
    if (const Householder2 right2 = MakeHouseholder2(b.At(k1, k1), b.At(k1, k)); right2.Active) {
        ApplyRightHouseholder2(a, k1, k, ld, ll_bound + 1, right2);
        ApplyRightHouseholder2(b, k1, k, ld, ll_bound + 1, right2);
        b.At(k1, k) = 0.0;
    }
}

void ChaseQzBulge(DenseMatrix &a, DenseMatrix &b, const QzShift &shift, const std::size_t ld, const std::size_t en) {
    const std::size_t na = en - 1;
    for (std::size_t k = shift.Start; k <= na; ++k) {
        const std::size_t ll_bound = std::min(en, k + 1 + static_cast<std::size_t>(shift.Order));
        if (k != na && shift.Order == 2) {
            ChaseDoubleShiftStep(a, b, k, shift.Start, ld, en, ll_bound, shift.A1, shift.A2, shift.A3);
        } else {
            ChaseSingleShiftStep(a, b, k, shift.Start, ld, en, ll_bound, shift.A1, shift.A2);
        }
    }
}

/**
 * @brief   Performs iterative QZ steps to reduce upper Hessenberg A to quasi-triangular form.
 * @param[in,out] a         Upper Hessenberg matrix.
 * @param[in,out] b         Upper triangular matrix.
 * @param[out]    epsb_out  Tolerance on B elements.
 */
void IterateQz(DenseMatrix &a, DenseMatrix &b, double &epsb_out) {
    const std::size_t n = a.Rows();
    if (n <= 1) {
        epsb_out = 0.0;
        return;
    }

    const auto [epsa, epsb] = ComputePencilTolerances(a, b);
    epsb_out = epsb;

    std::size_t en = n - 1;
    std::size_t itn = 30 * n;
    std::size_t its = 0;

    while (en >= 2) {
        const std::size_t na = en - 1;
        auto [l, split] = FindSubdiagonalSplit(a, en, epsa);
        if (split && l >= na) {
            en = l - 1;
            its = 0;
            continue;
        }
        if (DeflateZeroBDiagonal(a, b, l, en, epsb)) {
            en = l - 1;
            its = 0;
            continue;
        }
        if (itn == 0) {
            return;
        }

        const QzShift shift = ComputeQzShift(a, b, l, en, its, epsa, epsb);
        ++its;
        --itn;
        ChaseQzBulge(a, b, shift, l, en);
    }
}

// Phase 3: Eigenvalue extraction (Qzval)

void RecordRealEigenvalue(const DenseMatrix &a, const DenseMatrix &b, const std::size_t idx, EigenvalueArrays &out) {
    out.Alfr[idx] = (b.At(idx, idx) < 0.0) ? -a.At(idx, idx) : a.At(idx, idx);
    out.Beta[idx] = std::abs(b.At(idx, idx));
    out.Alfi[idx] = 0.0;
}

void RecordRealBlock2x2(DenseMatrix &a,
                        DenseMatrix &b,
                        const std::size_t na,
                        const std::size_t en,
                        EigenvalueArrays &out) {
    a.At(en, na) = 0.0;
    b.At(en, na) = 0.0;
    RecordRealEigenvalue(a, b, na, out);
    RecordRealEigenvalue(a, b, en, out);
}

void DeflateSingularBlock2x2(DenseMatrix &a,
                             DenseMatrix &b,
                             const std::size_t na,
                             const std::size_t en,
                             const bool upper_singular,
                             EigenvalueArrays &out) {
    if (upper_singular) {
        if (const Householder2 h = MakeHouseholder2(a.At(na, na), a.At(en, na)); h.Active) {
            ApplyLeftHouseholder2(a, na, en, na, a.Rows(), h);
            ApplyLeftHouseholder2(b, na, en, na, a.Rows(), h);
        }
    } else if (const Householder2 h = MakeHouseholder2(a.At(en, en), a.At(en, na)); h.Active) {
        ApplyRightHouseholder2(a, en, na, 0, en + 1, h);
        ApplyRightHouseholder2(b, en, na, 0, en + 1, h);
    }
    RecordRealBlock2x2(a, b, na, en, out);
}

ScaledBlock2x2 ScaleBlock2x2(const DenseMatrix &a, const DenseMatrix &b, const std::size_t na, const std::size_t en) {
    const double an = std::abs(a.At(na, na)) + std::abs(a.At(na, en)) + std::abs(a.At(en, na)) + std::abs(a.At(en, en));
    const double bn = std::abs(b.At(na, na)) + std::abs(b.At(na, en)) + std::abs(b.At(en, en));
    return {
        .An = an,
        .Bn = bn,
        .A11 = a.At(na, na) / an,
        .A12 = a.At(na, en) / an,
        .A21 = a.At(en, na) / an,
        .A22 = a.At(en, en) / an,
        .B11 = b.At(na, na) / bn,
        .B12 = b.At(na, en) / bn,
        .B22 = b.At(en, en) / bn,
    };
}

void ExtractRealBlock2x2(DenseMatrix &a,
                         DenseMatrix &b,
                         const std::size_t na,
                         const std::size_t en,
                         const ScaledBlock2x2 &blk,
                         const double shift_e,
                         EigenvalueArrays &out) {
    const double a11 = blk.A11 - shift_e * blk.B11;
    const double a12 = blk.A12 - shift_e * blk.B12;
    const double a22 = blk.A22 - shift_e * blk.B22;
    const bool use_lower = (std::abs(a11) + std::abs(a12) < std::abs(blk.A21) + std::abs(a22));
    const double r1 = use_lower ? a22 : a12;
    const double r2 = use_lower ? blk.A21 : a11;

    if (const Householder2 right = MakeHouseholder2(r1, r2); right.Active) {
        ApplyRightHouseholder2(a, en, na, 0, en + 1, right);
        ApplyRightHouseholder2(b, en, na, 0, en + 1, right);
    }

    const bool use_a = (blk.An < std::abs(shift_e) * blk.Bn);
    const double l1 = use_a ? a.At(na, na) : b.At(na, na);
    const double l2 = use_a ? a.At(en, na) : b.At(en, na);
    if (const Householder2 left = MakeHouseholder2(l1, l2); left.Active) {
        ApplyLeftHouseholder2(a, na, en, na, a.Rows(), left);
        ApplyLeftHouseholder2(b, na, en, na, a.Rows(), left);
    }
    RecordRealBlock2x2(a, b, na, en, out);
}

void RecordComplexRoot(const double tr,
                       const double ti,
                       const double dr,
                       const double di,
                       const double an,
                       const double bn,
                       const std::size_t na,
                       const std::size_t en,
                       EigenvalueArrays &out) {
    const double t_val = ti * dr - tr * di;
    const std::size_t j_idx = (t_val < 0.0) ? en : na;
    const double r = std::sqrt(dr * dr + di * di);
    out.Beta[j_idx] = bn * r;
    out.Alfr[j_idx] = an * (tr * dr + ti * di) / r;
    out.Alfi[j_idx] = an * t_val / r;
}

void ExtractComplexBlock2x2(const std::size_t na,
                            const std::size_t en,
                            const ScaledBlock2x2 &blk,
                            const double e_real,
                            const double e_imag,
                            EigenvalueArrays &out) {
    const double a11r = blk.A11 - e_real * blk.B11;
    const double a11i = e_imag * blk.B11;
    const double a12r = blk.A12 - e_real * blk.B12;
    const double a12i = e_imag * blk.B12;
    const double a22r = blk.A22 - e_real * blk.B22;
    const double a22i = e_imag * blk.B22;

    const bool use_lower = (std::abs(a11r) + std::abs(a11i) + std::abs(a12r) + std::abs(a12i)
                            < std::abs(blk.A21) + std::abs(a22r) + std::abs(a22i));
    const ComplexRotation z =
        use_lower ? MakeComplexRotation(a22r, a22i, -blk.A21, 0.0) : MakeComplexRotation(a12r, a12i, -a11r, -a11i);

    const bool use_a = (blk.An < (std::abs(e_real) + e_imag) * blk.Bn);
    const ComplexRotation q =
        use_a ? MakeComplexRotation(z.C * blk.A11 + z.Sr * blk.A12,
                                    z.Si * blk.A12,
                                    z.C * blk.A21 + z.Sr * blk.A22,
                                    z.Si * blk.A22)
              : MakeComplexRotation(z.C * blk.B11 + z.Sr * blk.B12, z.Si * blk.B12, z.Sr * blk.B22, z.Si * blk.B22);

    const double ssr = q.Sr * z.Sr + q.Si * z.Si;
    const double ssi = q.Sr * z.Si - q.Si * z.Sr;

    // First complex root
    RecordComplexRoot(q.C * z.C * blk.A11 + q.C * z.Sr * blk.A12 + q.Sr * z.C * blk.A21 + ssr * blk.A22,
                      q.C * z.Si * blk.A12 - q.Si * z.C * blk.A21 + ssi * blk.A22,
                      q.C * z.C * blk.B11 + q.C * z.Sr * blk.B12 + ssr * blk.B22,
                      q.C * z.Si * blk.B12 + ssi * blk.B22,
                      blk.An,
                      blk.Bn,
                      na,
                      en,
                      out);

    // Second complex root
    RecordComplexRoot(ssr * blk.A11 - q.Sr * z.C * blk.A12 - q.C * z.Sr * blk.A21 + q.C * z.C * blk.A22,
                      -ssi * blk.A11 - q.Si * z.C * blk.A12 + q.C * z.Si * blk.A21,
                      ssr * blk.B11 - q.Sr * z.C * blk.B12 + q.C * z.C * blk.B22,
                      -ssi * blk.B11 - q.Si * z.C * blk.B12,
                      blk.An,
                      blk.Bn,
                      na,
                      en,
                      out);
}

void ExtractBlock2x2(DenseMatrix &a,
                     DenseMatrix &b,
                     const std::size_t na,
                     const std::size_t en,
                     const double epsb,
                     EigenvalueArrays &out) {
    if (std::abs(b.At(na, na)) <= epsb) {
        DeflateSingularBlock2x2(a, b, na, en, true, out);
        return;
    }
    if (std::abs(b.At(en, en)) <= epsb) {
        DeflateSingularBlock2x2(a, b, na, en, false, out);
        return;
    }

    const ScaledBlock2x2 blk = ScaleBlock2x2(a, b, na, en);
    double e = blk.A11 / blk.B11;
    const double ei_val = blk.A22 / blk.B22;
    const double s = blk.A21 / (blk.B11 * blk.B22);
    double t = (blk.A22 - e * blk.B22) / blk.B22;
    if (std::abs(e) > std::abs(ei_val)) {
        e = ei_val;
        t = (blk.A11 - e * blk.B11) / blk.B11;
    }
    const double c = 0.5 * (t - s * blk.B12);
    const double d = c * c + s * (blk.A12 - e * blk.B12);

    if (d >= 0.0) {
        ExtractRealBlock2x2(a, b, na, en, blk, e + c + std::copysign(std::sqrt(d), c), out);
    } else {
        ExtractComplexBlock2x2(na, en, blk, e + c, std::sqrt(-d), out);
    }
}

/**
 * @brief   Extracts finite eigenvalues from quasi-triangular A and triangular B.
 * @param[in,out] a              Quasi-triangular matrix.
 * @param[in,out] b              Upper triangular matrix.
 * @param[in]     epsb           Singularity floor for B diagonal.
 * @param[in]     infinite_beta  Largest beta that still counts as an infinite eigenvalue.
 * @return  Vector of finite complex eigenvalues.
 */
std::vector<std::complex<double>>
ExtractEigenvalues(DenseMatrix &a, DenseMatrix &b, const double epsb, const double infinite_beta) {
    const std::size_t n = a.Rows();
    EigenvalueArrays out{
        .Alfr = std::vector<double>(n, 0.0),
        .Alfi = std::vector<double>(n, 0.0),
        .Beta = std::vector<double>(n, 0.0),
    };
    bool skip_next = false;

    for (std::size_t nn = 0; nn < n; ++nn) {
        if (skip_next) {
            skip_next = false;
            continue;
        }
        const std::size_t en = n - 1 - nn;
        const std::size_t na = (en > 0) ? en - 1 : 0;
        if (en == 0 || a.At(en, na) == 0.0) {
            RecordRealEigenvalue(a, b, en, out);
        } else {
            ExtractBlock2x2(a, b, na, en, epsb, out);
            skip_next = true;
        }
    }

    std::vector<std::complex<double>> eigenvalues;
    for (std::size_t i = 0; i < n; ++i) {
        if (out.Beta[i] > infinite_beta) {
            eigenvalues.emplace_back(out.Alfr[i] / out.Beta[i], out.Alfi[i] / out.Beta[i]);
        }
    }
    return eigenvalues;
}

} // namespace

/**
 * @brief   Computes the finite generalized eigenvalues lambda of the real matrix pencil (mat_a, mat_b).
 * @param[in] mat_a  Real square matrix of size N x N.
 * @param[in] mat_b  Real square matrix of size N x N.
 * @return  Vector of finite complex generalized eigenvalues in rad/s.
 */
std::vector<std::complex<double>> ComputeGeneralizedEigenvalues(const DenseMatrix &mat_a, const DenseMatrix &mat_b) {
    if (mat_a.Rows() == 0 || mat_a.Cols() != mat_a.Rows() || mat_b.Rows() != mat_a.Rows()
        || mat_b.Cols() != mat_a.Rows()) {
        return {};
    }

    DenseMatrix a = mat_a;
    DenseMatrix b = mat_b;
    const double frequency_scale = ScaleFrequency(a, b);
    BalancePencil(a, b);
    // Measured before the algebraic part is eliminated and after the scaling, so what counts as small is the same for
    // every pencil, and a 1 pF capacitor beside a 1 S conductance is not taken for the zero of an infinite eigenvalue
    const double infinite_beta = INFINITE_EIGENVALUE_FRACTION * MaxAbsEntry(b);
    DeflateAlgebraicRowsAndColumns(a, b);

    const std::size_t n = a.Rows();
    if (n == 0) {
        return {};
    }

    // QZ works on copies: refinement goes back to the pencil as it was before the reduction
    const DenseMatrix deflated_a = a;
    const DenseMatrix deflated_b = b;
    std::vector<std::complex<double>> roots;
    if (n == 1) {
        if (std::abs(b.At(0, 0)) > infinite_beta) {
            roots.emplace_back(a.At(0, 0) / b.At(0, 0), 0.0);
        }
    } else {
        ReduceToHessenbergTriangular(a, b);
        double epsb = 0.0;
        IterateQz(a, b, epsb);
        roots = ExtractEigenvalues(a, b, epsb, infinite_beta);
    }
    roots = RefineRoots(deflated_a, deflated_b, roots);
    for (std::complex<double> &root : roots) {
        root *= frequency_scale;
    }
    return roots;
}

} // namespace Nodalys
