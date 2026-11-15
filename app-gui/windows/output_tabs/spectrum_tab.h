/**
 * @file    spectrum_tab.h
 * @brief   The Spectrum tab of the Output window: the measured traces of the transient in frequency, and their
 *          harmonics.
 */

#ifndef IMCSIM_SPECTRUM_TAB_H
#define IMCSIM_SPECTRUM_TAB_H

#include "plots/math_channels.h"
#include "plots/plot_export.h"
#include "plots/plot_types.h"
#include "plots/spectrum.h"
#include "results/result_store.h"
#include "schematic/schematic.h"
#include "windows/output_tabs/frame_results.h"
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   SpectrumTab
 * @brief   Transforms the measured traces of the transient over the span its statistics measure, and reads the
 *          fundamental, the harmonics and the THD of each, as a spectrum analyzer would.
 * @details The spectra are cached until the result, the span, the window of the transform or the traces change. The
 *          tab goes over the whole run, so the math channels join it once the run is over.
 */
class SpectrumTab {
public:
    SpectrumTab(Schematic &schematic, const ResultStore &results);

    void Draw(const FrameResults &frame, PlotSpan span);

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure() const;

private:
    Schematic &m_Schematic;
    const ResultStore &m_Results;
    /// Window the transform of the spectrum applies.
    SpectrumWindow m_Window = SpectrumWindow::HANN;
    /// Whether the frequency scale of the spectrum is logarithmic.
    bool m_IsLogarithmic = false;
    /// Whether the plot shows noise densities on logarithmic axes instead of decibels.
    bool m_IsDensity = false;
    /// Whether every trace of the last spectra was broadband, so the density view opens by itself when they become so.
    bool m_WereAllBroadband = false;
    /// Visible range of the spectrum plot.
    PlotSpan m_View;
    /// The last spectra computed.
    std::optional<TransientSpectra> m_Spectra;
    /// Whether the spectra changed since the plot last fitted its axes to them.
    bool m_HasChanged = false;

    void Update(const Core::Transient &transient, const std::vector<MathTrace> &math, PlotSpan span);
    void DrawPlot();
    void DrawStatistics();
    void DrawControls();
    bool IsDensityShown() const;
};

} // namespace GUI

#endif // IMCSIM_SPECTRUM_TAB_H
