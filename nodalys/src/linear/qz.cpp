/**
 * @file    qz.cpp
 * @brief   Generalized eigenvalue solver for real matrix pencils (A, B) via the QZ algorithm (Moler-Stewart).
 * @details Implements the Moler-Stewart generalized Schur decomposition (SIAM J. Numer. Anal. 1973):
 *          1. Qzhes: reduction of (A, B) to upper Hessenberg-triangular form via Householder reflections.
 *          2. Qzit: iterative QZ step with implicit Francis double/single shifts and deflation.
 *          3. Qzval: extraction of finite eigenvalues from quasi-triangular and triangular diagonal blocks.
 */

#include "linear/qz.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Nodalys {

namespace {

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

    // 1. Reduce B to upper triangular form via Householder reflections from the left
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

        for (std::size_t j = l1; j < n; ++j) {
            double t = 0.0;
            for (std::size_t i = l; i < n; ++i) {
                t += b.At(i, l) * b.At(i, j);
            }
            t = -t / rho;
            for (std::size_t i = l; i < n; ++i) {
                b.At(i, j) += t * b.At(i, l);
            }
        }
        for (std::size_t j = 0; j < n; ++j) {
            double t = 0.0;
            for (std::size_t i = l; i < n; ++i) {
                t += b.At(i, l) * a.At(i, j);
            }
            t = -t / rho;
            for (std::size_t i = l; i < n; ++i) {
                a.At(i, j) += t * b.At(i, l);
            }
        }
        b.At(l, l) = -s * r;
        for (std::size_t i = l1; i < n; ++i) {
            b.At(i, l) = 0.0;
        }
    }

    if (n == 2) {
        return;
    }

    // 2. Reduce A to upper Hessenberg while keeping B upper triangular
    for (std::size_t k = 0; k < n - 2; ++k) {
        for (std::size_t lb = 1; lb <= n - 1 - (k + 1); ++lb) {
            const std::size_t l = (n - 1) - lb;
            const std::size_t l1 = l + 1;

            // Annihilate A(l1, k)
            double s = std::abs(a.At(l, k)) + std::abs(a.At(l1, k));
            if (s != 0.0) {
                const double u1 = a.At(l, k) / s;
                double u2 = a.At(l1, k) / s;
                const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
                const double v1 = -(u1 + r) / r;
                const double v2 = -u2 / r;
                u2 = v2 / v1;

                for (std::size_t j = k; j < n; ++j) {
                    const double t = a.At(l, j) + u2 * a.At(l1, j);
                    a.At(l, j) += t * v1;
                    a.At(l1, j) += t * v2;
                }
                a.At(l1, k) = 0.0;

                for (std::size_t j = l; j < n; ++j) {
                    const double t = b.At(l, j) + u2 * b.At(l1, j);
                    b.At(l, j) += t * v1;
                    b.At(l1, j) += t * v2;
                }
            }

            // Annihilate B(l1, l) from the right
            s = std::abs(b.At(l1, l1)) + std::abs(b.At(l1, l));
            if (s != 0.0) {
                const double u1 = b.At(l1, l1) / s;
                double u2 = b.At(l1, l) / s;
                const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
                const double v1 = -(u1 + r) / r;
                const double v2 = -u2 / r;
                u2 = v2 / v1;

                for (std::size_t i = 0; i <= l1; ++i) {
                    const double t = b.At(i, l1) + u2 * b.At(i, l);
                    b.At(i, l1) += t * v1;
                    b.At(i, l) += t * v2;
                }
                b.At(l1, l) = 0.0;

                for (std::size_t i = 0; i < n; ++i) {
                    const double t = a.At(i, l1) + u2 * a.At(i, l);
                    a.At(i, l1) += t * v1;
                    a.At(i, l) += t * v2;
                }
            }
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

    double anorm = 0.0;
    double bnorm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double ani = (i > 0) ? std::abs(a.At(i, i - 1)) : 0.0;
        double bni = 0.0;
        for (std::size_t j = i; j < n; ++j) {
            ani += std::abs(a.At(i, j));
            bni += std::abs(b.At(i, j));
        }
        if (ani > anorm) {
            anorm = ani;
        }
        if (bni > bnorm) {
            bnorm = bni;
        }
    }
    if (anorm == 0.0) {
        anorm = 1.0;
    }
    if (bnorm == 0.0) {
        bnorm = 1.0;
    }

    const double ep = std::numeric_limits<double>::epsilon();
    const double epsa = ep * anorm;
    const double epsb = ep * bnorm;
    epsb_out = epsb;

    std::size_t en = n - 1;
    std::size_t itn = 30 * n;
    std::size_t its = 0;

    while (en >= 2) {
        const std::size_t na = en - 1;
        const std::size_t enm2 = na - 1;
        int ish = 2;

        std::size_t l = 0;
        bool split = false;
        for (std::size_t ll = 0; ll <= en; ++ll) {
            l = en - ll;
            if (l == 0) {
                break;
            }
            if (std::abs(a.At(l, l - 1)) <= epsa) {
                a.At(l, l - 1) = 0.0;
                split = true;
                break;
            }
        }

        if (split && l >= na) {
            en = l - 1;
            its = 0;
            continue;
        }

        const std::size_t ld = l;
        bool goto_next_qz = false;
        while (true) {
            const std::size_t l1 = l + 1;
            const double b11 = b.At(l, l);
            if (std::abs(b11) > epsb) {
                break;
            }
            b.At(l, l) = 0.0;
            const double s = std::abs(a.At(l, l)) + std::abs(a.At(l1, l));
            const double u1 = a.At(l, l) / s;
            double u2 = a.At(l1, l) / s;
            const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
            const double v1 = -(u1 + r) / r;
            const double v2 = -u2 / r;
            u2 = v2 / v1;

            for (std::size_t j = l; j <= en; ++j) {
                const double t = a.At(l, j) + u2 * a.At(l1, j);
                a.At(l, j) += t * v1;
                a.At(l1, j) += t * v2;
                const double tb = b.At(l, j) + u2 * b.At(l1, j);
                b.At(l, j) += tb * v1;
                b.At(l1, j) += tb * v2;
            }

            if (l > 0) {
                a.At(l, l - 1) = -a.At(l, l - 1);
            }
            l = l1;
            a.At(l, l - 1) = 0.0;
            if (l >= na) {
                en = l - 1;
                its = 0;
                goto_next_qz = true;
                break;
            }
        }
        if (goto_next_qz) {
            continue;
        }

        if (itn == 0) {
            return;
        }

        const std::size_t l1 = l + 1;
        const double a11 = a.At(l, l) / b.At(l, l);
        const double a21 = a.At(l1, l) / b.At(l, l);

        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;

        if (its == 10) {
            a1 = 0.0;
            a2 = 1.0;
            a3 = 1.1605;
        } else {
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
                ish = 1;
                const double sqrt_r = std::sqrt(r);
                double sh = -t + sqrt_r;
                const double s = -t - sqrt_r;
                if (std::abs(s - a44) < std::abs(sh - a44)) {
                    sh = s;
                }

                for (std::size_t ll = ld; ll <= enm2; ++ll) {
                    l = enm2 + ld - ll;
                    if (l == ld) {
                        break;
                    }
                    double t_val = a.At(l, l);
                    if (std::abs(b.At(l, l)) > epsb) {
                        t_val -= sh * b.At(l, l);
                    }
                    if (std::abs(a.At(l, l - 1)) <= std::abs(t_val / a.At(l + 1, l)) * epsa) {
                        break;
                    }
                }

                a1 = a11 - sh;
                a2 = a21;
                if (l != ld) {
                    a.At(l, l - 1) = -a.At(l, l - 1);
                }
            } else {
                const double a12 = a.At(l, l1) / b22;
                const double a22 = a.At(l1, l1) / b22;
                const double b12 = b.At(l, l1) / b22;

                a1 = ((a33 - a11) * (a44 - a11) - a34 * a43 + a43 * b34 * a11) / a21 + a12 - a11 * b12;
                a2 = (a22 - a11) - a21 * b12 - (a33 - a11) - (a44 - a11) + a43 * b34;
                a3 = a.At(l1 + 1, l1) / b22;
            }
        }

        its++;
        itn--;

        for (std::size_t k = l; k <= na; ++k) {
            const bool notlas = (k != na && ish == 2);
            const std::size_t k1 = k + 1;
            const std::size_t k2 = k + 2;
            const std::size_t km1 = std::max(k > 0 ? k - 1 : 0, l);
            const std::size_t ll_bound = std::min(en, k1 + static_cast<std::size_t>(ish));

            if (!notlas) {
                if (k != l) {
                    a1 = a.At(k, km1);
                    a2 = a.At(k1, km1);
                }
                const double s = std::abs(a1) + std::abs(a2);
                if (s == 0.0) {
                    continue;
                }
                const double u1 = a1 / s;
                double u2 = a2 / s;
                const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
                const double v1 = -(u1 + r) / r;
                const double v2 = -u2 / r;
                u2 = v2 / v1;

                for (std::size_t j = km1; j <= en; ++j) {
                    const double t = a.At(k, j) + u2 * a.At(k1, j);
                    a.At(k, j) += t * v1;
                    a.At(k1, j) += t * v2;
                    const double tb = b.At(k, j) + u2 * b.At(k1, j);
                    b.At(k, j) += tb * v1;
                    b.At(k1, j) += tb * v2;
                }
                if (k != l) {
                    a.At(k1, km1) = 0.0;
                }

                const double sb = std::abs(b.At(k1, k1)) + std::abs(b.At(k1, k));
                if (sb != 0.0) {
                    const double ub1 = b.At(k1, k1) / sb;
                    double ub2 = b.At(k1, k) / sb;
                    const double rb = std::copysign(std::sqrt(ub1 * ub1 + ub2 * ub2), ub1);
                    const double vb1 = -(ub1 + rb) / rb;
                    const double vb2 = -ub2 / rb;
                    ub2 = vb2 / vb1;

                    for (std::size_t i = ld; i <= ll_bound; ++i) {
                        const double t = a.At(i, k1) + ub2 * a.At(i, k);
                        a.At(i, k1) += t * vb1;
                        a.At(i, k) += t * vb2;
                        const double tb = b.At(i, k1) + ub2 * b.At(i, k);
                        b.At(i, k1) += tb * vb1;
                        b.At(i, k) += tb * vb2;
                    }
                    b.At(k1, k) = 0.0;
                }
            } else {
                if (k != l) {
                    a1 = a.At(k, km1);
                    a2 = a.At(k1, km1);
                    a3 = a.At(k2, km1);
                }
                const double s = std::abs(a1) + std::abs(a2) + std::abs(a3);
                if (s == 0.0) {
                    continue;
                }
                const double u1 = a1 / s;
                double u2 = a2 / s;
                double u3 = a3 / s;
                const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2 + u3 * u3), u1);
                const double v1 = -(u1 + r) / r;
                const double v2 = -u2 / r;
                const double v3 = -u3 / r;
                u2 = v2 / v1;
                u3 = v3 / v1;

                for (std::size_t j = km1; j <= en; ++j) {
                    const double t = a.At(k, j) + u2 * a.At(k1, j) + u3 * a.At(k2, j);
                    a.At(k, j) += t * v1;
                    a.At(k1, j) += t * v2;
                    a.At(k2, j) += t * v3;
                    const double tb = b.At(k, j) + u2 * b.At(k1, j) + u3 * b.At(k2, j);
                    b.At(k, j) += tb * v1;
                    b.At(k1, j) += tb * v2;
                    b.At(k2, j) += tb * v3;
                }
                if (k != l) {
                    a.At(k1, km1) = 0.0;
                    a.At(k2, km1) = 0.0;
                }

                const double sb2 = std::abs(b.At(k2, k2)) + std::abs(b.At(k2, k1)) + std::abs(b.At(k2, k));
                if (sb2 != 0.0) {
                    const double ub1 = b.At(k2, k2) / sb2;
                    double ub2 = b.At(k2, k1) / sb2;
                    double ub3 = b.At(k2, k) / sb2;
                    const double rb = std::copysign(std::sqrt(ub1 * ub1 + ub2 * ub2 + ub3 * ub3), ub1);
                    const double vb1 = -(ub1 + rb) / rb;
                    const double vb2 = -ub2 / rb;
                    const double vb3 = -ub3 / rb;
                    ub2 = vb2 / vb1;
                    ub3 = vb3 / vb1;

                    for (std::size_t i = ld; i <= ll_bound; ++i) {
                        const double t = a.At(i, k2) + ub2 * a.At(i, k1) + ub3 * a.At(i, k);
                        a.At(i, k2) += t * vb1;
                        a.At(i, k1) += t * vb2;
                        a.At(i, k) += t * vb3;
                        const double tb = b.At(i, k2) + ub2 * b.At(i, k1) + ub3 * b.At(i, k);
                        b.At(i, k2) += tb * vb1;
                        b.At(i, k1) += tb * vb2;
                        b.At(i, k) += tb * vb3;
                    }
                    b.At(k2, k) = 0.0;
                    b.At(k2, k1) = 0.0;
                }

                const double sb1 = std::abs(b.At(k1, k1)) + std::abs(b.At(k1, k));
                if (sb1 != 0.0) {
                    const double ub1 = b.At(k1, k1) / sb1;
                    double ub2 = b.At(k1, k) / sb1;
                    const double rb = std::copysign(std::sqrt(ub1 * ub1 + ub2 * ub2), ub1);
                    const double vb1 = -(ub1 + rb) / rb;
                    const double vb2 = -ub2 / rb;
                    ub2 = vb2 / vb1;

                    for (std::size_t i = ld; i <= ll_bound; ++i) {
                        const double t = a.At(i, k1) + ub2 * a.At(i, k);
                        a.At(i, k1) += t * vb1;
                        a.At(i, k) += t * vb2;
                        const double tb = b.At(i, k1) + ub2 * b.At(i, k);
                        b.At(i, k1) += tb * vb1;
                        b.At(i, k) += tb * vb2;
                    }
                    b.At(k1, k) = 0.0;
                }
            }
        }
    }
}

/**
 * @brief   Extracts finite eigenvalues from quasi-triangular A and triangular B.
 * @param[in,out] a     Quasi-triangular matrix.
 * @param[in,out] b     Upper triangular matrix.
 * @param[in]     epsb  Singularity floor for B diagonal.
 * @return  Vector of finite complex eigenvalues.
 */
std::vector<std::complex<double>> ExtractEigenvalues(DenseMatrix &a, DenseMatrix &b, const double epsb) {
    const std::size_t n = a.Rows();
    std::vector<double> alfr(n, 0.0);
    std::vector<double> alfi(n, 0.0);
    std::vector<double> beta(n, 0.0);
    int isw = 1;

    for (std::size_t nn = 0; nn < n; ++nn) {
        const std::size_t en = n - 1 - nn;
        const std::size_t na = (en > 0) ? en - 1 : 0;

        if (isw == 2) {
            isw = 1;
            continue;
        }

        if (en == 0 || a.At(en, na) == 0.0) {
            // 1-by-1 block
            alfr[en] = a.At(en, en);
            if (b.At(en, en) < 0.0) {
                alfr[en] = -alfr[en];
            }
            beta[en] = std::abs(b.At(en, en));
            alfi[en] = 0.0;
            continue;
        }

        // 2-by-2 block
        if (std::abs(b.At(na, na)) <= epsb) {
            const double a1 = a.At(na, na);
            const double a2 = a.At(en, na);
            const double s = std::abs(a1) + std::abs(a2);
            if (s != 0.0) {
                const double u1 = a1 / s;
                double u2 = a2 / s;
                const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
                const double v1 = -(u1 + r) / r;
                const double v2 = -u2 / r;
                u2 = v2 / v1;
                for (std::size_t j = na; j < n; ++j) {
                    const double t = a.At(na, j) + u2 * a.At(en, j);
                    a.At(na, j) += t * v1;
                    a.At(en, j) += t * v2;
                    const double tb = b.At(na, j) + u2 * b.At(en, j);
                    b.At(na, j) += tb * v1;
                    b.At(en, j) += tb * v2;
                }
            }
            a.At(en, na) = 0.0;
            b.At(en, na) = 0.0;
            alfr[na] = a.At(na, na);
            alfr[en] = a.At(en, en);
            if (b.At(na, na) < 0.0) {
                alfr[na] = -alfr[na];
            }
            if (b.At(en, en) < 0.0) {
                alfr[en] = -alfr[en];
            }
            beta[na] = std::abs(b.At(na, na));
            beta[en] = std::abs(b.At(en, en));
            alfi[en] = 0.0;
            alfi[na] = 0.0;
            isw = 2;
            continue;
        }

        if (std::abs(b.At(en, en)) <= epsb) {
            const double a1 = a.At(en, en);
            const double a2 = a.At(en, na);
            const double s = std::abs(a1) + std::abs(a2);
            const double u1 = a1 / s;
            double u2 = a2 / s;
            const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
            const double v1 = -(u1 + r) / r;
            const double v2 = -u2 / r;
            u2 = v2 / v1;
            for (std::size_t i = 0; i <= en; ++i) {
                const double t = a.At(i, en) + u2 * a.At(i, na);
                a.At(i, en) += t * v1;
                a.At(i, na) += t * v2;
                const double tb = b.At(i, en) + u2 * b.At(i, na);
                b.At(i, en) += tb * v1;
                b.At(i, na) += tb * v2;
            }
            a.At(en, na) = 0.0;
            b.At(en, na) = 0.0;
            alfr[na] = a.At(na, na);
            alfr[en] = a.At(en, en);
            if (b.At(na, na) < 0.0) {
                alfr[na] = -alfr[na];
            }
            if (b.At(en, en) < 0.0) {
                alfr[en] = -alfr[en];
            }
            beta[na] = std::abs(b.At(na, na));
            beta[en] = std::abs(b.At(en, en));
            alfi[en] = 0.0;
            alfi[na] = 0.0;
            isw = 2;
            continue;
        }

        const double an =
            std::abs(a.At(na, na)) + std::abs(a.At(na, en)) + std::abs(a.At(en, na)) + std::abs(a.At(en, en));
        const double bn = std::abs(b.At(na, na)) + std::abs(b.At(na, en)) + std::abs(b.At(en, en));
        double a11 = a.At(na, na) / an;
        double a12 = a.At(na, en) / an;
        const double a21 = a.At(en, na) / an;
        double a22 = a.At(en, en) / an;
        const double b11 = b.At(na, na) / bn;
        const double b12 = b.At(na, en) / bn;
        const double b22 = b.At(en, en) / bn;

        double e = a11 / b11;
        const double ei_val = a22 / b22;
        const double s = a21 / (b11 * b22);
        double t = (a22 - e * b22) / b22;
        if (std::abs(e) > std::abs(ei_val)) {
            e = ei_val;
            t = (a11 - e * b11) / b11;
        }
        const double c = 0.5 * (t - s * b12);
        const double d = c * c + s * (a12 - e * b12);

        if (d >= 0.0) {
            e += (c + std::copysign(std::sqrt(d), c));
            a11 -= e * b11;
            a12 -= e * b12;
            a22 -= e * b22;
            double a1 = 0.0;
            double a2 = 0.0;
            if (std::abs(a11) + std::abs(a12) < std::abs(a21) + std::abs(a22)) {
                a1 = a22;
                a2 = a21;
            } else {
                a1 = a12;
                a2 = a11;
            }
            const double s_val = std::abs(a1) + std::abs(a2);
            const double u1 = a1 / s_val;
            double u2 = a2 / s_val;
            const double r = std::copysign(std::sqrt(u1 * u1 + u2 * u2), u1);
            const double v1 = -(u1 + r) / r;
            const double v2 = -u2 / r;
            u2 = v2 / v1;

            for (std::size_t i = 0; i <= en; ++i) {
                const double t_val = a.At(i, en) + u2 * a.At(i, na);
                a.At(i, en) += t_val * v1;
                a.At(i, na) += t_val * v2;
                const double tb_val = b.At(i, en) + u2 * b.At(i, na);
                b.At(i, en) += tb_val * v1;
                b.At(i, na) += tb_val * v2;
            }

            if (an < std::abs(e) * bn) {
                a1 = a.At(na, na);
                a2 = a.At(en, na);
            } else {
                a1 = b.At(na, na);
                a2 = b.At(en, na);
            }
            const double sb = std::abs(a1) + std::abs(a2);
            if (sb != 0.0) {
                const double ub1 = a1 / sb;
                double ub2 = a2 / sb;
                const double rb = std::copysign(std::sqrt(ub1 * ub1 + ub2 * ub2), ub1);
                const double vb1 = -(ub1 + rb) / rb;
                const double vb2 = -ub2 / rb;
                ub2 = vb2 / vb1;
                for (std::size_t j = na; j < n; ++j) {
                    const double t_val = a.At(na, j) + ub2 * a.At(en, j);
                    a.At(na, j) += t_val * vb1;
                    a.At(en, j) += t_val * vb2;
                    const double tb_val = b.At(na, j) + ub2 * b.At(en, j);
                    b.At(na, j) += tb_val * vb1;
                    b.At(en, j) += tb_val * vb2;
                }
            }
            a.At(en, na) = 0.0;
            b.At(en, na) = 0.0;
            alfr[na] = a.At(na, na);
            alfr[en] = a.At(en, en);
            if (b.At(na, na) < 0.0) {
                alfr[na] = -alfr[na];
            }
            if (b.At(en, en) < 0.0) {
                alfr[en] = -alfr[en];
            }
            beta[na] = std::abs(b.At(na, na));
            beta[en] = std::abs(b.At(en, en));
            alfi[en] = 0.0;
            alfi[na] = 0.0;
        } else {
            e += c;
            const double ei = std::sqrt(-d);
            const double a11r = a11 - e * b11;
            const double a11i = ei * b11;
            const double a12r = a12 - e * b12;
            const double a12i = ei * b12;
            const double a22r = a22 - e * b22;
            const double a22i = ei * b22;

            double a1 = 0.0;
            double a1i = 0.0;
            double a2 = 0.0;
            double a2i = 0.0;
            if (std::abs(a11r) + std::abs(a11i) + std::abs(a12r) + std::abs(a12i) <
                std::abs(a21) + std::abs(a22r) + std::abs(a22i)) {
                a1 = a22r;
                a1i = a22i;
                a2 = -a21;
                a2i = 0.0;
            } else {
                a1 = a12r;
                a1i = a12i;
                a2 = -a11r;
                a2i = -a11i;
            }
            double cz = std::sqrt(a1 * a1 + a1i * a1i);
            double szr = 1.0;
            double szi = 0.0;
            if (cz != 0.0) {
                szr = (a1 * a2 + a1i * a2i) / cz;
                szi = (a1 * a2i - a1i * a2) / cz;
                const double r = std::sqrt(cz * cz + szr * szr + szi * szi);
                cz /= r;
                szr /= r;
                szi /= r;
            }

            if (an < (std::abs(e) + ei) * bn) {
                a1 = cz * a11 + szr * a12;
                a1i = szi * a12;
                a2 = cz * a21 + szr * a22;
                a2i = szi * a22;
            } else {
                a1 = cz * b11 + szr * b12;
                a1i = szi * b12;
                a2 = szr * b22;
                a2i = szi * b22;
            }
            double cq = std::sqrt(a1 * a1 + a1i * a1i);
            double sqr = 1.0;
            double sqi = 0.0;
            if (cq != 0.0) {
                sqr = (a1 * a2 + a1i * a2i) / cq;
                sqi = (a1 * a2i - a1i * a2) / cq;
                const double r = std::sqrt(cq * cq + sqr * sqr + sqi * sqi);
                cq /= r;
                sqr /= r;
                sqi /= r;
            }

            const double ssr = sqr * szr + sqi * szi;
            const double ssi = sqr * szi - sqi * szr;

            // First complex root
            double tr = cq * cz * a11 + cq * szr * a12 + sqr * cz * a21 + ssr * a22;
            double ti = cq * szi * a12 - sqi * cz * a21 + ssi * a22;
            double dr = cq * cz * b11 + cq * szr * b12 + ssr * b22;
            double di = cq * szi * b12 + ssi * b22;
            double t_val = ti * dr - tr * di;
            std::size_t j_idx = (t_val < 0.0) ? en : na;
            double r = std::sqrt(dr * dr + di * di);
            beta[j_idx] = bn * r;
            alfr[j_idx] = an * (tr * dr + ti * di) / r;
            alfi[j_idx] = an * t_val / r;

            // Second complex root
            tr = ssr * a11 - sqr * cz * a12 - cq * szr * a21 + cq * cz * a22;
            ti = -ssi * a11 - sqi * cz * a12 + cq * szi * a21;
            dr = ssr * b11 - sqr * cz * b12 + cq * cz * b22;
            di = -ssi * b11 - sqi * cz * b12;
            t_val = ti * dr - tr * di;
            j_idx = (t_val < 0.0) ? en : na;
            r = std::sqrt(dr * dr + di * di);
            beta[j_idx] = bn * r;
            alfr[j_idx] = an * (tr * dr + ti * di) / r;
            alfi[j_idx] = an * t_val / r;
        }

        isw = 2;
    }

    std::vector<std::complex<double>> eigenvalues;
    double b_norm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            b_norm = std::max(b_norm, std::abs(b.At(i, j)));
        }
    }
    const double tol = 1e-12 * b_norm;

    for (std::size_t i = 0; i < n; ++i) {
        if (beta[i] > tol) {
            eigenvalues.emplace_back(alfr[i] / beta[i], alfi[i] / beta[i]);
        }
    }
    return eigenvalues;
}

DenseMatrix RemoveRowAndColumn(const DenseMatrix &matrix, const std::size_t drop_row, const std::size_t drop_col) {
    const std::size_t n = matrix.Rows();
    DenseMatrix reduced(n - 1, n - 1);
    std::size_t dst_r = 0;
    for (std::size_t r = 0; r < n; ++r) {
        if (r == drop_row) {
            continue;
        }
        std::size_t dst_c = 0;
        for (std::size_t c = 0; c < n; ++c) {
            if (c == drop_col) {
                continue;
            }
            reduced.At(dst_r, dst_c) = matrix.At(r, c);
            ++dst_c;
        }
        ++dst_r;
    }
    return reduced;
}

double MaxAbsEntry(const DenseMatrix &matrix) {
    double max_val = 0.0;
    for (std::size_t r = 0; r < matrix.Rows(); ++r) {
        for (std::size_t c = 0; c < matrix.Cols(); ++c) {
            max_val = std::max(max_val, std::abs(matrix.At(r, c)));
        }
    }
    return max_val;
}

struct AlgebraicPivot {
    bool IsRow = false;
    std::size_t Row = 0;
    std::size_t Col = 0;
    double Magnitude = 0.0;
};

void UpdateBestZeroBRowPivot(const DenseMatrix &a, const DenseMatrix &b, const double b_tol, AlgebraicPivot &best) {
    const std::size_t n = a.Rows();
    for (std::size_t r = 0; r < n; ++r) {
        double row_b = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            row_b = std::max(row_b, std::abs(b.At(r, c)));
        }
        if (row_b > b_tol) {
            continue;
        }
        for (std::size_t c = 0; c < n; ++c) {
            const double val = std::abs(a.At(r, c));
            if (val > best.Magnitude) {
                best = {.IsRow = true, .Row = r, .Col = c, .Magnitude = val};
            }
        }
    }
}

void UpdateBestZeroBColPivot(const DenseMatrix &a, const DenseMatrix &b, const double b_tol, AlgebraicPivot &best) {
    const std::size_t n = a.Rows();
    for (std::size_t c = 0; c < n; ++c) {
        double col_b = 0.0;
        for (std::size_t r = 0; r < n; ++r) {
            col_b = std::max(col_b, std::abs(b.At(r, c)));
        }
        if (col_b > b_tol) {
            continue;
        }
        for (std::size_t r = 0; r < n; ++r) {
            const double val = std::abs(a.At(r, c));
            if (val > best.Magnitude) {
                best = {.IsRow = false, .Row = r, .Col = c, .Magnitude = val};
            }
        }
    }
}

void EliminateRowPivot(DenseMatrix &a, DenseMatrix &b, const std::size_t pivot_row, const std::size_t pivot_col) {
    const std::size_t n = a.Rows();
    const double pivot = a.At(pivot_row, pivot_col);
    for (std::size_t j = 0; j < n; ++j) {
        if (j == pivot_col) {
            continue;
        }
        const double factor = a.At(pivot_row, j) / pivot;
        if (factor == 0.0) {
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) {
            a.At(i, j) -= factor * a.At(i, pivot_col);
            b.At(i, j) -= factor * b.At(i, pivot_col);
        }
    }
}

void EliminateColPivot(DenseMatrix &a, DenseMatrix &b, const std::size_t pivot_row, const std::size_t pivot_col) {
    const std::size_t n = a.Rows();
    const double pivot = a.At(pivot_row, pivot_col);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == pivot_row) {
            continue;
        }
        const double factor = a.At(i, pivot_col) / pivot;
        if (factor == 0.0) {
            continue;
        }
        for (std::size_t j = 0; j < n; ++j) {
            a.At(i, j) -= factor * a.At(pivot_row, j);
            b.At(i, j) -= factor * b.At(pivot_row, j);
        }
    }
}

/**
 * @brief   Deflates algebraic rows and columns (zero rows/columns in B) before running QZ, eliminating spurious
 *          infinite-eigenvalue leaks in singular MNA pencils.
 */
void DeflateZeroRowsAndColumns(DenseMatrix &a, DenseMatrix &b) {
    const double a_norm_ref = MaxAbsEntry(a);
    const double b_norm_ref = MaxAbsEntry(b);
    if (b_norm_ref == 0.0) {
        a = DenseMatrix(0, 0);
        b = DenseMatrix(0, 0);
        return;
    }
    const double b_tol = 1e-12 * b_norm_ref;
    const double a_tol = 1e-12 * a_norm_ref;

    while (a.Rows() > 0) {
        if (MaxAbsEntry(b) <= b_tol) {
            a = DenseMatrix(0, 0);
            b = DenseMatrix(0, 0);
            return;
        }

        AlgebraicPivot best;
        UpdateBestZeroBRowPivot(a, b, b_tol, best);
        UpdateBestZeroBColPivot(a, b, b_tol, best);

        if (best.Magnitude <= a_tol) {
            break;
        }

        if (best.IsRow) {
            EliminateRowPivot(a, b, best.Row, best.Col);
        } else {
            EliminateColPivot(a, b, best.Row, best.Col);
        }

        a = RemoveRowAndColumn(a, best.Row, best.Col);
        b = RemoveRowAndColumn(b, best.Row, best.Col);
    }
}

} // namespace

/**
 * @brief   Computes the finite generalized eigenvalues lambda of the real matrix pencil (mat_a, mat_b).
 * @param[in] mat_a  Real square matrix of size N x N.
 * @param[in] mat_b  Real square matrix of size N x N.
 * @return  Vector of finite complex generalized eigenvalues in rad/s.
 */
std::vector<std::complex<double>> ComputeGeneralizedEigenvalues(const DenseMatrix &mat_a, const DenseMatrix &mat_b) {
    if (mat_a.Rows() == 0 || mat_a.Cols() != mat_a.Rows() || mat_b.Rows() != mat_a.Rows() ||
        mat_b.Cols() != mat_a.Rows()) {
        return {};
    }

    DenseMatrix a = mat_a;
    DenseMatrix b = mat_b;
    DeflateZeroRowsAndColumns(a, b);

    const std::size_t n = a.Rows();
    if (n == 0) {
        return {};
    }

    if (n == 1) {
        const double b00 = b.At(0, 0);
        const double b_norm = std::abs(b00);
        if (std::abs(b00) <= 1e-12 * std::max(b_norm, 1.0)) {
            return {};
        }
        return {std::complex<double>(a.At(0, 0) / b00, 0.0)};
    }

    ReduceToHessenbergTriangular(a, b);
    double epsb = 0.0;
    IterateQz(a, b, epsb);
    return ExtractEigenvalues(a, b, epsb);
}

} // namespace Nodalys
