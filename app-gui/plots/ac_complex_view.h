/**
 * @file    ac_complex_view.h
 * @brief   Complex-plane curves, readings and export figures for the Nyquist and Polar views of an AC sweep.
 */

#ifndef IMCSIM_AC_COMPLEX_VIEW_H
#define IMCSIM_AC_COMPLEX_VIEW_H

#include "imgui.h"
#include "plots/complex_plane.h"
#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    ACSweepView
 * @brief   Which representation the AC sweep tab plots: Bode magnitude/phase, Nyquist or Polar.
 */
enum class ACSweepView {
    BODE,
    NYQUIST,
    POLAR
};

/**
 * @struct  ComplexLocus
 * @brief   One complex frequency trace ready to draw in ImPlot or export to SVG/CSV.
 */
struct ComplexLocus {
    std::string Name;
    std::string LegendLabel;
    std::string Unit;
    ImU32 Color = 0;
    bool IsDashed = false;
    bool IsFaint = false;
    std::vector<std::complex<double>> Values;
};

// Curves and readings
std::vector<ComplexLocus> BuildACSweepComplexLoci(const Core::ACSweep &sweep,
                                                  const ACMathTraces &math,
                                                  const ResultStore &results,
                                                  Schematic &schematic);
std::string DescribeACSweepComplexReading(double frequency_hertz, const ComplexLocus &locus, std::size_t sample);

// Export
ExportFigure BuildACSweepComplexFigure(const Core::ACSweep &sweep,
                                       const ACMathTraces &math,
                                       const ResultStore &results,
                                       Schematic &schematic,
                                       ComplexPlaneKind kind,
                                       bool mirror_negative_frequencies);

} // namespace GUI

#endif // IMCSIM_AC_COMPLEX_VIEW_H
