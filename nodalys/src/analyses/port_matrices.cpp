/**
 * @file    port_matrices.cpp
 * @brief   Impedance and admittance matrices of the ports, worked out from the scattering matrix.
 */

#include "analyses/port_matrices.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>

namespace Nodalys {

namespace {

/// A pivot below this share of the larger of 1 and the biggest entry makes a matrix singular for our purposes: the
/// impedance it would give is above about a billion times the reference, an open circuit in practice, and the solver's
/// own 1e-12 siemens leak to ground keeps a true open from reading as an exact infinity.
constexpr double SINGULAR_PIVOT_RATIO = 1e-9;

using Complex = std::complex<double>;

std::size_t IndexOf(const std::size_t size, const std::size_t row, const std::size_t column) {
    return row * size + column;
}

/// Computes I + sign * S, with sign +1 or -1.
PortMatrix AddIdentity(const PortMatrix &scattering, const std::size_t size, const double sign) {
    PortMatrix result(scattering.size());
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            const Complex identity = row == column ? Complex(1.0, 0.0) : Complex(0.0, 0.0);
            result[IndexOf(size, row, column)] = identity + sign * scattering[IndexOf(size, row, column)];
        }
    }
    return result;
}

PortMatrix Multiply(const PortMatrix &left, const PortMatrix &right, const std::size_t size) {
    PortMatrix product(left.size(), Complex(0.0, 0.0));
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            Complex sum(0.0, 0.0);
            for (std::size_t inner = 0; inner < size; ++inner) {
                sum += left[IndexOf(size, row, inner)] * right[IndexOf(size, inner, column)];
            }
            product[IndexOf(size, row, column)] = sum;
        }
    }
    return product;
}

/**
 * @brief   Inverts a matrix by Gauss-Jordan elimination with partial pivoting.
 * @return  The inverse; none when a pivot is too small, which is a matrix with no usable inverse.
 */
std::optional<PortMatrix> Invert(PortMatrix matrix, const std::size_t size) {
    double largest_entry = 1.0;
    for (const Complex &entry : matrix) {
        largest_entry = std::max(largest_entry, std::abs(entry));
    }
    PortMatrix inverse(matrix.size(), Complex(0.0, 0.0));
    for (std::size_t index = 0; index < size; ++index) {
        inverse[IndexOf(size, index, index)] = 1.0;
    }
    for (std::size_t column = 0; column < size; ++column) {
        std::size_t pivot_row = column;
        for (std::size_t row = column + 1; row < size; ++row) {
            if (std::abs(matrix[IndexOf(size, row, column)]) > std::abs(matrix[IndexOf(size, pivot_row, column)])) {
                pivot_row = row;
            }
        }
        const Complex pivot = matrix[IndexOf(size, pivot_row, column)];
        if (std::abs(pivot) < SINGULAR_PIVOT_RATIO * largest_entry) {
            return std::nullopt;
        }
        for (std::size_t other = 0; other < size; ++other) {
            std::swap(matrix[IndexOf(size, column, other)], matrix[IndexOf(size, pivot_row, other)]);
            std::swap(inverse[IndexOf(size, column, other)], inverse[IndexOf(size, pivot_row, other)]);
        }
        for (std::size_t other = 0; other < size; ++other) {
            matrix[IndexOf(size, column, other)] /= pivot;
            inverse[IndexOf(size, column, other)] /= pivot;
        }
        for (std::size_t row = 0; row < size; ++row) {
            if (row == column) {
                continue;
            }
            const Complex factor = matrix[IndexOf(size, row, column)];
            for (std::size_t other = 0; other < size; ++other) {
                matrix[IndexOf(size, row, other)] -= factor * matrix[IndexOf(size, column, other)];
                inverse[IndexOf(size, row, other)] -= factor * inverse[IndexOf(size, column, other)];
            }
        }
    }
    return inverse;
}

/// Scales entry (row, column) by the square roots of the references of its port row and column, raised to power.
void ScaleByReferences(PortMatrix &matrix, const std::vector<double> &reference_ohms, const double power) {
    const std::size_t size = reference_ohms.size();
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            matrix[IndexOf(size, row, column)] *= std::pow(reference_ohms[row] * reference_ohms[column], power / 2.0);
        }
    }
}

bool IsFinite(const PortMatrix &matrix) {
    return std::ranges::all_of(matrix, [](const Complex &entry) {
        return std::isfinite(entry.real()) && std::isfinite(entry.imag());
    });
}

} // namespace

/**
 * @brief   Works out the impedance matrix of the ports from their scattering matrix.
 * @param[in] scattering     S matrix at one frequency, row by row.
 * @param[in] reference_ohms Reference impedance of each port; the matrix has that many rows.
 * @return  Z = D (I + S) (I - S)^-1 D with D = diag(sqrt(Z0)), in ohms; none when I - S has no usable inverse, which
 *          is a circuit with no finite impedance matrix, such as two ports joined by a series element and nothing else.
 */
std::optional<PortMatrix> ComputeImpedanceMatrix(const PortMatrix &scattering,
                                                 const std::vector<double> &reference_ohms) {
    const std::size_t size = reference_ohms.size();
    const std::optional<PortMatrix> inverse = Invert(AddIdentity(scattering, size, -1.0), size);
    if (!inverse) {
        return std::nullopt;
    }
    PortMatrix impedance = Multiply(AddIdentity(scattering, size, 1.0), *inverse, size);
    ScaleByReferences(impedance, reference_ohms, 1.0);
    if (!IsFinite(impedance)) {
        return std::nullopt;
    }
    return impedance;
}

/**
 * @brief   Works out the admittance matrix of the ports from their scattering matrix.
 * @param[in] scattering      S matrix at one frequency, row by row.
 * @param[in] reference_ohms  Reference impedance of each port; the matrix has that many rows.
 * @return  Y = D^-1 (I - S) (I + S)^-1 D^-1 with D = diag(sqrt(Z0)), in siemens; none when I + S has no usable
 *          inverse, such as a port shorted to ground.
 */
std::optional<PortMatrix> ComputeAdmittanceMatrix(const PortMatrix &scattering,
                                                  const std::vector<double> &reference_ohms) {
    const std::size_t size = reference_ohms.size();
    const std::optional<PortMatrix> inverse = Invert(AddIdentity(scattering, size, 1.0), size);
    if (!inverse) {
        return std::nullopt;
    }
    PortMatrix admittance = Multiply(AddIdentity(scattering, size, -1.0), *inverse, size);
    ScaleByReferences(admittance, reference_ohms, -1.0);
    if (!IsFinite(admittance)) {
        return std::nullopt;
    }
    return admittance;
}

} // namespace Nodalys
