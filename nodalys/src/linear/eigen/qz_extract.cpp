/**
 * @file    qz_extract.cpp
 * @brief   Phase 3 (Qzval) of Moler-Stewart QZ: extracts finite eigenvalues from quasi-triangular (A, B).
 */

#include "linear/eigen/qz_extract.h"

#include "linear/eigen/qz_householder.h"
#include <cmath>
#include <cstddef>

namespace Nodalys {

namespace {

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

} // namespace

std::vector<std::complex<double>>
ExtractQzEigenvalues(DenseMatrix &a, DenseMatrix &b, const double epsb, const double infinite_beta) {
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

} // namespace Nodalys
