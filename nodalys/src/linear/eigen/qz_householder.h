/**
 * @file    qz_householder.h
 * @brief   Two- and three-element Householder reflector primitives shared across QZ phases.
 */

#ifndef NODALYS_LINEAR_EIGEN_QZ_HOUSEHOLDER_H
#define NODALYS_LINEAR_EIGEN_QZ_HOUSEHOLDER_H

#include "linear/dense_matrix.h"
#include <cstddef>

namespace Nodalys {

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

// Reflector construction
[[nodiscard]] Householder2 MakeHouseholder2(double x1, double x2);
[[nodiscard]] Householder3 MakeHouseholder3(double x1, double x2, double x3);

// Left and right application
void ApplyLeftHouseholder2(DenseMatrix &mat,
                           std::size_t r0,
                           std::size_t r1,
                           std::size_t col_start,
                           std::size_t col_end,
                           const Householder2 &h);
void ApplyRightHouseholder2(DenseMatrix &mat,
                            std::size_t c0,
                            std::size_t c1,
                            std::size_t row_start,
                            std::size_t row_end,
                            const Householder2 &h);
void ApplyLeftHouseholder3(DenseMatrix &mat,
                           std::size_t r0,
                           std::size_t r1,
                           std::size_t r2,
                           std::size_t col_start,
                           std::size_t col_end,
                           const Householder3 &h);
void ApplyRightHouseholder3(DenseMatrix &mat,
                            std::size_t c0,
                            std::size_t c1,
                            std::size_t c2,
                            std::size_t row_start,
                            std::size_t row_end,
                            const Householder3 &h);

} // namespace Nodalys

#endif // NODALYS_LINEAR_EIGEN_QZ_HOUSEHOLDER_H
