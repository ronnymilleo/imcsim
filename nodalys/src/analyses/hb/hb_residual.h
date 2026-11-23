/**
 * @file    hb_residual.h
 * @brief   The harmonic balance residual and its Jacobian, by alternating between frequency and time.
 * @details The linear devices act on the harmonics directly. The nonlinear ones are evaluated at the time samples of
 *          the state, and their currents and charges are taken back to the harmonics by the FFT.
 */

#ifndef NODALYS_HB_RESIDUAL_H
#define NODALYS_HB_RESIDUAL_H

#include "analyses/hb/hb_workspace.h"
#include "linear/dense_matrix.h"
#include "newton/newton.h"
#include <vector>

namespace Nodalys {

/**
 * @struct  HarmonicResidual
 * @brief   The residual of the balance and the size of the terms that made up each of its rows.
 */
struct HarmonicResidual {
    std::vector<double> Values;
    /// Largest term added to each row, which the tolerance of the row scales with.
    std::vector<double> RowScale;
};

// Evaluation
void EvaluateResidual(HbWorkspace &ws,
                      const std::vector<double> &x_hb,
                      double ac_scale,
                      double gmin,
                      HarmonicResidual &residual);
void EvaluateResidualAndJacobian(HbWorkspace &ws,
                                 const std::vector<double> &x_hb,
                                 double ac_scale,
                                 double gmin,
                                 HarmonicResidual &residual,
                                 DenseMatrix &jacobian);

// Judging
[[nodiscard]] bool
CheckHbResidual(const HbWorkspace &ws, const HarmonicResidual &residual, const ConvergenceOptions &options);
double ResidualNormSq(const HbWorkspace &ws, const HarmonicResidual &residual);

} // namespace Nodalys

#endif // NODALYS_HB_RESIDUAL_H
