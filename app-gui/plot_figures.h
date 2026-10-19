/**
 * @file    plot_figures.h
 * @brief   The export figures of the Output window plots, and the lists of traces the plots and the figures share.
 */

#ifndef IMCSIM_PLOT_FIGURES_H
#define IMCSIM_PLOT_FIGURES_H

#include "implot.h"
#include "logic_trace.h"
#include "math_channels.h"
#include "plot_export.h"
#include "plot_types.h"
#include "schematic.h"
#include "simulator.h"
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  CurveFamily
 * @brief   The curves of one trace across the steps of a DC sweep, and the axis they are plotted on.
 */
struct CurveFamily {
    ImAxis Axis;
    std::vector<const std::vector<double> *> Curves;
};

// Spread between the curves of a family, against their size, below which they count as one and get no labels
inline constexpr double NegligibleSpread = 1e-6;
// Below a femtovolt or a femtoampere a value is the rounding residue of the solver, such as 1e-170 V for a node at 0 V
inline constexpr double ResidueLevel = 1e-15;
// The input-referred noise takes a color of its own, apart from the node colors the output uses
inline constexpr ImU32 InputNoiseColor = IM_COL32(150, 230, 90, 255);

// Traces shown
bool HasMeasuredCurrent(const std::vector<Core::ComponentTrace> &currents, const Schematic &schematic);
std::vector<int> ListNodes(const std::vector<LogicNode> &lanes);
bool CurvesDiffer(const std::vector<const std::vector<double> *> &curves, std::size_t sample);
std::vector<CurveFamily> ListCurveFamilies(const Core::DCSweep &sweep, const Schematic &schematic, bool show_currents,
                                           const std::vector<std::vector<MathTrace>> &math);
std::string FormatStepLabel(const Core::DCSweep &sweep, const Core::DCSweepCurve &curve);
bool HasInputGain(const Core::Noise &noise);
std::string GetDensityUnit(const std::string &unit);

// Figures
ExportFigure BuildTransientFigure(const Core::Transient &transient, const std::vector<MathTrace> &math,
                                  const std::vector<LogicNode> &lanes, Schematic &schematic, bool bit_values,
                                  const PlotSpan &view);
ExportFigure BuildSpectrumFigure(const TransientSpectra &spectra, bool logarithmic, const PlotSpan &view);
ExportFigure BuildACSweepFigure(const Core::ACSweep &sweep, Schematic &schematic, const PlotSpan &view);
ExportFigure BuildDCSweepFigure(const Core::DCSweep &sweep, const std::vector<std::vector<MathTrace>> &math,
                                Schematic &schematic, const PlotSpan &view);
ExportFigure BuildNoiseFigure(const Core::Noise &noise, Schematic &schematic, const std::set<std::string> &shown,
                              const PlotSpan &view);

} // namespace GUI

#endif // IMCSIM_PLOT_FIGURES_H
