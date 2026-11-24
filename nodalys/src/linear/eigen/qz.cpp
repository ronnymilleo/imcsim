/**
 * @file    qz.cpp
 * @brief   Generalized eigenvalue solver for real matrix pencils (A, B) via the QZ algorithm (Moler-Stewart).
 * @details Implements the Moler-Stewart generalized Schur decomposition (SIAM J. Numer. Anal. 1973):
 *          1. Qzhes: reduction of (A, B) to upper Hessenberg-triangular form via Householder reflections.
 *          2. Qzit: iterative QZ step with implicit Francis double/single shifts and deflation.
 *          3. Qzval: extraction of finite eigenvalues from quasi-triangular and triangular diagonal blocks.
 */

#include "linear/eigen/qz.h"

#include "linear/eigen/pencil_reduction.h"
#include "linear/eigen/qz_extract.h"
#include "linear/eigen/qz_householder.h"
#include "linear/eigen/root_refinement.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Nodalys {

namespace {

/// Size of B, relative to its largest entry, at or below which an eigenvalue counts as infinite.
constexpr double INFINITE_EIGENVALUE_FRACTION = 1e-13;

/// Implicit single- or double-shift starter computed for one QZ bulge-chasing sweep.
struct QzShift {
    int Order = 2;
    std::size_t Start = 0;
    double A1 = 0.0;
    double A2 = 0.0;
    double A3 = 0.0;
};

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

QzShift MakeSingleShift(DenseMatrix &a,
                        const DenseMatrix &b,
                        const std::size_t ld,
                        const std::size_t na,
                        const double a11,
                        const double a21,
                        const double a44,
                        const double t,
                        const double r,
                        const double epsa,
                        const double epsb) {
    const double sqrt_r = std::sqrt(r);
    const double sh1 = -t + sqrt_r;
    const double sh2 = -t - sqrt_r;
    const double sh = (std::abs(sh2 - a44) < std::abs(sh1 - a44)) ? sh2 : sh1;
    const std::size_t start = FindSingleShiftStart(a, b, ld, na - 1, sh, epsa, epsb);
    if (start != ld) {
        a.At(start, start - 1) = -a.At(start, start - 1);
    }
    return {.Order = 1, .Start = start, .A1 = a11 - sh, .A2 = a21, .A3 = 0.0};
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
        return MakeSingleShift(a, b, ld, na, a11, a21, a44, t, r, epsa, epsb);
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

} // namespace

/**
 * @brief   Computes the finite generalized eigenvalues lambda of the real matrix pencil (mat_a, mat_b) such that
 *          det(mat_a - lambda * mat_b) = 0.
 * @param[in] mat_a  Real square matrix of size N x N.
 * @param[in] mat_b  Real square matrix of size N x N.
 * @return  Vector of finite complex generalized eigenvalues in rad/s. Infinite and indeterminate eigenvalues are
 *          deflated.
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
        roots = ExtractQzEigenvalues(a, b, epsb, infinite_beta);
    }
    roots = RefineRoots(deflated_a, deflated_b, roots);
    for (std::complex<double> &root : roots) {
        root *= frequency_scale;
    }
    return roots;
}

} // namespace Nodalys
