/**
 * @file    qz_householder.cpp
 * @brief   Two- and three-element Householder reflector primitives shared across QZ phases.
 */

#include "linear/eigen/qz_householder.h"

#include <cmath>

namespace Nodalys {

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

} // namespace Nodalys
