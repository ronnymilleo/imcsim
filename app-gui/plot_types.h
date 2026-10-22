/**
 * @file    plot_types.h
 * @brief   Spans, cursors and spectra that the plots of the Output window show and export.
 */

#ifndef IMCSIM_PLOT_TYPES_H
#define IMCSIM_PLOT_TYPES_H

#include "implot.h"
#include "spectrum.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  PlotCursors
 * @brief   The two vertical cursors of a plot, A and B, as on an oscilloscope.
 * @details They are placed in the visible range when turned on, and the user drags them from there.
 */
struct PlotCursors {
    bool Shown = false;
    bool Placed = false;
    double A = 0.0;
    double B = 0.0;
};

/**
 * @struct  PlotSpan
 * @brief   A range of the X axis of a plot, such as what it shows.
 */
struct PlotSpan {
    double From = 0.0;
    double To = 0.0;
};

/**
 * @struct  SpectrumTrace
 * @brief   The spectrum of one measured trace of the transient, in decibels, and its harmonics.
 */
struct SpectrumTrace {
    std::string Label;
    // Unit of the trace, such as "V"; the spectrum reads in decibels relative to 1 of it
    std::string Unit;
    ImAxis Axis = ImAxis_Y1;
    ImU32 Color = 0;
    bool Dashed = false;
    // No value when the trace is undefined somewhere in the span, as a math channel dividing by zero
    std::optional<std::vector<double>> Decibels;
    // Of the mean and every frequency, so a negligible mean reads as about zero
    double LargestAmplitude = 0.0;
    HarmonicAnalysis Harmonics;
};

/**
 * @struct  TransientSpectra
 * @brief   The spectra of the measured traces of a transient over one span, and what they were computed from.
 * @details Transforms take long enough to keep for as long as their inputs stay the same.
 */
struct TransientSpectra {
    std::size_t TransientVersion = 0;
    PlotSpan Span;
    SpectrumWindow Window = SpectrumWindow::HANN;
    std::vector<std::string> Labels;
    // From the first frequency above 0 Hz to half the sampling rate, shared by every trace
    std::vector<double> Frequencies;
    std::vector<SpectrumTrace> Traces;
};

} // namespace GUI

#endif // IMCSIM_PLOT_TYPES_H
