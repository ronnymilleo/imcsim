/**
 * @file    harmonic_balance_tab.h
 * @brief   The Harmonic balance tab of the Output window: harmonic spectra, reconstructed periodic waveforms and THD.
 */

#ifndef IMCSIM_HARMONIC_BALANCE_TAB_H
#define IMCSIM_HARMONIC_BALANCE_TAB_H

#include "analysis/harmonic_balance_view.h"
#include "document/schematic.h"
#include "plots/plot_export.h"
#include "plots/plot_exporter.h"
#include "plots/plot_types.h"
#include "plots/trace_fit_state.h"
#include "preferences.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace GUI {

/**
 * @class   HarmonicBalanceTab
 * @brief   Draws the harmonic balance spectrum, reconstructed periodic waveforms, DC/THD summaries and harmonic table.
 */
class HarmonicBalanceTab {
public:
    HarmonicBalanceTab(Schematic &schematic, Preferences &preferences);

    void Draw(PlotExporter &exporter);

    // Export
    bool CanExport() const;
    ExportFigure BuildFigure() const;

private:
    Schematic &m_Schematic;
    Preferences &m_Preferences;

    TraceFitState m_SpectrumFit;
    TraceFitState m_WaveformFit;
    PlotSpan m_WaveformView;

    HarmonicScale m_Scale = HarmonicScale::LINEAR;
    std::optional<HarmonicScale> m_DrawnScale;
    std::optional<HarmonicAmplitudeUnit> m_DrawnAmplitudeUnit;
    int m_Cycles = 1;
    std::optional<int> m_DrawnCycles;
    std::size_t m_SelectedTableTrace = 0;

    // Controls and views
    void DrawControls(const Core::HarmonicBalance &result, bool has_measured, PlotExporter &exporter);
    void DrawSpectrumPlot(const Core::HarmonicBalance &result, bool has_voltages, bool has_currents, float height);
    void DrawWaveformPlot(const Core::HarmonicBalance &result, bool has_voltages, bool has_currents, float height);
    void DrawStatistics(const Core::HarmonicBalance &result);
};

} // namespace GUI

#endif // IMCSIM_HARMONIC_BALANCE_TAB_H
