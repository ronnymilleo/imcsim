/**
 * @file    root_refinement.cpp
 * @brief   Refines the generalized eigenvalues of a real pencil (A, B) so each is accurate to its own size.
 */

#include "linear/eigen/root_refinement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Nodalys {

namespace {

/// Two QZ roots farther apart than this share of their size were different roots to start with.
constexpr double DISTINCT_START_SHARE = 1e-3;
/// Iterations of the shifted inverse iteration: two or three from a root QZ got right, more from one it got far wrong.
constexpr int MAX_ITERATIONS = 40;
/// Pencils larger than this keep the QZ values: every root costs a few complex factorizations of the pencil.
constexpr std::size_t MAX_REFINED_SIZE = 150;
/// How far a root may move as a share of its own size.
constexpr double MAX_RELATIVE_MOVE = 1.0;
/// How far a root may move, in units of the rounding of the largest root. QZ promises one rounding, but a root of a
/// strongly non-normal pencil can be off by a million times that.
constexpr double NOISE_ROUNDINGS = 1e7;
/// A root whose eigenvector leaves A singular to this many roundings is at the origin.
constexpr double ORIGIN_ROUNDINGS = 50.0;
/// Residual, in roundings of the pencil times its size, under which a refined root is believed.
constexpr double RESIDUAL_ROUNDINGS = 1e3;
/// Two refined roots this close, as a share of their size, are the same root.
constexpr double SAME_ROOT_SHARE = 1e-4;
/// A step smaller than this share of the root, or of the noise floor, ends the iteration.
constexpr double STEP_TOLERANCE = 1e-14;
/// Rounds of the largest root under which a root that was not refined is taken for noise at the origin.
constexpr double UNREFINED_ORIGIN_ROUNDINGS = 10.0;

using Complex = std::complex<double>;

/**
 * @struct  Refined
 * @brief   A refined root with the size of the pencil's residual at it and at the origin.
 */
struct Refined {
    Complex Value;
    bool IsAccepted = false;
    bool IsAtOrigin = false;
};

double OneNorm(const DenseMatrix &matrix) {
    double largest = 0.0;
    for (std::size_t c = 0; c < matrix.Cols(); ++c) {
        double sum = 0.0;
        for (std::size_t r = 0; r < matrix.Rows(); ++r) {
            sum += std::abs(matrix.At(r, c));
        }
        largest = std::max(largest, sum);
    }
    return largest;
}

double Norm(const std::vector<Complex> &vector) {
    double sum = 0.0;
    for (const Complex &entry : vector) {
        sum += std::norm(entry);
    }
    return std::sqrt(sum);
}

/// The product (A - shift B) x, or (B x) when use_a is false.
std::vector<Complex>
Apply(const DenseMatrix &a, const DenseMatrix &b, const Complex shift, const std::vector<Complex> &x) {
    const std::size_t n = a.Rows();
    std::vector<Complex> result(n);
    for (std::size_t r = 0; r < n; ++r) {
        Complex sum{0.0, 0.0};
        for (std::size_t c = 0; c < n; ++c) {
            sum += (a.At(r, c) - shift * b.At(r, c)) * x[c];
        }
        result[r] = sum;
    }
    return result;
}

/// How far x is from satisfying A x = shift B x, relative to the size of the pencil and of x.
double Residual(const DenseMatrix &a,
                const DenseMatrix &b,
                const Complex shift,
                const std::vector<Complex> &x,
                const double a_norm,
                const double b_norm) {
    const double size = Norm(x) * (a_norm + std::abs(shift) * b_norm);
    return size > 0.0 ? Norm(Apply(a, b, shift, x)) / size : 0.0;
}

/**
 * @brief   One step of inverse iteration: solves (A - shift B) z = B x and reads the root from x^H z / x^H x.
 * @details Since (A - mu B)^-1 B x = x / (lambda - mu) when x is the eigenvector, the quotient is 1 / (lambda - mu).
 *          B may be singular: only A - mu B has to be regular, which it is for a shift that is not a root.
 * @return  False when the shifted matrix is singular, which means the shift already is a root.
 */
bool InverseIterationStep(const DenseMatrix &a, const DenseMatrix &b, Complex &shift, std::vector<Complex> &x) {
    const std::size_t n = a.Rows();
    ComplexDenseMatrix shifted(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            shifted.At(r, c) = a.At(r, c) - shift * b.At(r, c);
        }
    }
    std::vector<Complex> rhs(n, Complex{0.0, 0.0});
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            rhs[r] += b.At(r, c) * x[c];
        }
    }
    std::vector<Complex> z;
    if (!shifted.FactorizeLU() || !shifted.Solve(rhs, z)) {
        return false;
    }
    Complex numerator{0.0, 0.0};
    double denominator = 0.0;
    for (std::size_t index = 0; index < n; ++index) {
        numerator += std::conj(x[index]) * z[index];
        denominator += std::norm(x[index]);
    }
    const double z_norm = Norm(z);
    if (std::abs(numerator) == 0.0 || z_norm == 0.0) {
        return false;
    }
    const Complex previous = shift;
    shift = previous + denominator / numerator;
    for (std::size_t index = 0; index < n; ++index) {
        x[index] = z[index] / z_norm;
    }
    return std::abs(shift - previous) > STEP_TOLERANCE * std::abs(shift);
}

/// A start vector that no eigenvector is orthogonal to by construction of the pencils here: all entries different.
std::vector<Complex> StartVector(const std::size_t n) {
    std::vector<Complex> x(n);
    for (std::size_t index = 0; index < n; ++index) {
        x[index] = Complex{1.0 + 0.37 * static_cast<double>(index % 7) + 0.011 * static_cast<double>(index), 0.0};
    }
    return x;
}

/**
 * @brief   Refines one root by inverse iteration from its QZ value.
 * @param[in] start         Root from QZ.
 * @param[in] allowed_move  The farthest the refined root may be from the start.
 */
Refined RefineOne(const DenseMatrix &a, const DenseMatrix &b, const Complex start, const double allowed_move) {
    const double a_norm = OneNorm(a);
    const double b_norm = OneNorm(b);
    const double epsilon = std::numeric_limits<double>::epsilon();
    const double size = static_cast<double>(a.Rows());
    Complex shift = start;
    std::vector<Complex> x = StartVector(a.Rows());
    bool is_moving = true;
    for (int iteration = 0; iteration < MAX_ITERATIONS && is_moving; ++iteration) {
        is_moving = InverseIterationStep(a, b, shift, x);
    }
    Refined refined{.Value = shift};
    if (Norm(x) == 0.0 || std::abs(shift - start) > allowed_move) {
        return refined;
    }
    const double scale = Norm(x);
    for (Complex &entry : x) {
        entry /= scale;
    }
    const double refined_residual = Residual(a, b, shift, x, a_norm, b_norm);
    const double start_residual = Residual(a, b, start, x, a_norm, b_norm);
    refined.IsAccepted = refined_residual <= RESIDUAL_ROUNDINGS * size * epsilon && refined_residual <= start_residual;
    refined.IsAtOrigin =
        refined.IsAccepted && Residual(a, b, Complex{0.0, 0.0}, x, a_norm, b_norm) <= ORIGIN_ROUNDINGS * size * epsilon;
    return refined;
}

/**
 * @brief   Undoes the refinement of a root that landed on the place of another one it was not close to before.
 * @details Two QZ roots that were apart and now are the same cannot both be right, so neither is trusted over QZ.
 */
void RevertCollisions(const std::vector<Complex> &roots, std::vector<Complex> &result, std::vector<bool> &is_done) {
    for (std::size_t first = 0; first < roots.size(); ++first) {
        if (!is_done[first]) {
            continue;
        }
        for (std::size_t second = 0; second < roots.size(); ++second) {
            const double size = std::max(std::abs(result[first]), std::abs(result[second]));
            const bool is_same = std::abs(result[first] - result[second]) <= SAME_ROOT_SHARE * size;
            const bool is_apart = std::abs(roots[first] - roots[second]) > DISTINCT_START_SHARE * size;
            if (second != first && is_same && is_apart) {
                result[first] = roots[first];
                is_done[first] = false;
                break;
            }
        }
    }
}

/// Refines the roots with a non-negative imaginary part; returns which of them were accepted.
std::vector<bool> RefineUpperHalf(const DenseMatrix &a,
                                  const DenseMatrix &b,
                                  const std::vector<Complex> &roots,
                                  std::vector<Complex> &result) {
    const double epsilon = std::numeric_limits<double>::epsilon();
    double largest = 0.0;
    for (const Complex &root : roots) {
        largest = std::max(largest, std::abs(root));
    }
    std::vector<bool> is_done(roots.size(), false);
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const Complex root = roots[index];
        if (root.imag() < 0.0) {
            continue;
        }
        const double allowed_move = std::max(MAX_RELATIVE_MOVE * std::abs(root), NOISE_ROUNDINGS * epsilon * largest);
        const Refined refined = RefineOne(a, b, root, allowed_move);
        if (!refined.IsAccepted) {
            continue;
        }
        const Complex value = root.imag() == 0.0 ? Complex{refined.Value.real(), 0.0} : refined.Value;
        result[index] = refined.IsAtOrigin ? Complex{0.0, 0.0} : value;
        is_done[index] = true;
    }
    RevertCollisions(roots, result, is_done);
    return is_done;
}

/// The index of the root with a positive imaginary part nearest to the conjugate of roots[index], or none.
std::optional<std::size_t> FindUpperPartner(const std::vector<Complex> &roots, const std::size_t index) {
    const Complex mirrored = std::conj(roots[index]);
    std::optional<std::size_t> partner;
    double closest = std::numeric_limits<double>::infinity();
    for (std::size_t other = 0; other < roots.size(); ++other) {
        if (roots[other].imag() > 0.0 && std::abs(roots[other] - mirrored) < closest) {
            closest = std::abs(roots[other] - mirrored);
            partner = other;
        }
    }
    return partner;
}

} // namespace

/**
 * @brief   Refines the roots of the pencil (A, B) by inverse iteration on the pencil, in complex arithmetic.
 * @details A root is kept as refined only when it converged, its residual is a few roundings of the pencil and lower
 *          than the QZ root's, and it moved by no more than its own size or ten million roundings of the largest
 *          root; a root that lands on another one it was not close to is put back, so refinement never merges two
 *          roots. Real roots stay real and the members of a conjugate pair stay conjugate. A refined root whose
 *          eigenvector leaves A singular to a few roundings is exactly 0; a root that was not refined is 0 when it is
 *          under ten roundings of the largest root, which is all that QZ can say of it.
 * @param[in] mat_a  Matrix A of the pencil the roots belong to, as QZ received it.
 * @param[in] mat_b  Matrix B of the same pencil.
 * @param[in] roots  Finite eigenvalues from QZ.
 * @return  The refined roots, in the order of the input.
 */
std::vector<Complex>
RefineRoots(const DenseMatrix &mat_a, const DenseMatrix &mat_b, const std::vector<Complex> &roots) {
    std::vector<Complex> result = roots;
    std::vector<bool> is_done(roots.size(), false);
    if (mat_a.Rows() <= MAX_REFINED_SIZE) {
        is_done = RefineUpperHalf(mat_a, mat_b, roots, result);
    }
    // The lower member of a pair is the conjugate of the refined upper one
    std::vector<bool> is_mirrored(roots.size(), false);
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (roots[index].imag() >= 0.0) {
            continue;
        }
        const std::optional<std::size_t> partner = FindUpperPartner(roots, index);
        if (partner && is_done[*partner]) {
            result[index] = std::conj(result[*partner]);
            is_mirrored[index] = true;
        }
    }
    double largest = 0.0;
    for (const Complex &root : roots) {
        largest = std::max(largest, std::abs(root));
    }
    const double noise = UNREFINED_ORIGIN_ROUNDINGS * std::numeric_limits<double>::epsilon() * largest;
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (!is_done[index] && !is_mirrored[index] && std::abs(roots[index]) <= noise) {
            result[index] = Complex{0.0, 0.0};
        }
    }
    return result;
}

} // namespace Nodalys
