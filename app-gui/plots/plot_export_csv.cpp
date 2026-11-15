/**
 * @file    plot_export_csv.cpp
 * @brief   Writes the plots of the export as CSV tables, cartesian figures and Smith charts alike.
 */

#include "plots/plot_export.h"

#include <cmath>
#include <format>

namespace GUI {

namespace {

/**
 * @brief   Quotes a field that holds a separator, a quote or a line break, doubling its quotes.
 */
std::string QuoteCsvField(const std::string &text) {
    if (text.find_first_of(",\"\n") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (const char character : text) {
        quoted += character;
        if (character == '"') {
            quoted += '"';
        }
    }
    return quoted + "\"";
}

std::string CsvHeader(const std::string &name, const std::string &unit) {
    return QuoteCsvField(unit.empty() ? name : std::format("{} ({})", name, unit));
}

/**
 * @brief   Shortest text that reads back as the same number; missing samples stay empty.
 */
std::string FormatCsvValue(const double value) {
    return std::isfinite(value) ? std::format("{}", value) : "";
}

/**
 * @brief   The table of a Smith chart: a row per frequency, with the real and imaginary parts of every trace.
 */
std::string RenderSmithCsv(const ExportSmith &smith) {
    std::string text = CsvHeader("Frequency", "Hz");
    for (const ExportSmithTrace &trace : smith.Traces) {
        text += "," + CsvHeader(trace.Name + " real", "") + "," + CsvHeader(trace.Name + " imaginary", "");
    }
    text += "\n";
    const std::size_t rows = smith.Traces.empty() ? 0 : smith.Traces.front().Frequencies.size();
    for (std::size_t row = 0; row < rows; ++row) {
        text += FormatCsvValue(smith.Traces.front().Frequencies[row]);
        for (const ExportSmithTrace &trace : smith.Traces) {
            const bool has_value = row < trace.Values.size();
            text += "," + (has_value ? FormatCsvValue(trace.Values[row].real()) : "") + "," +
                    (has_value ? FormatCsvValue(trace.Values[row].imag()) : "");
        }
        text += "\n";
    }
    return text;
}

} // namespace

/**
 * @brief   Writes the samples of a figure as a CSV table.
 * @param[in] figure    Figure whose X samples and series become the columns; its X range is ignored. A Smith chart
 *                      lists the frequency, then the real and imaginary parts of each trace.
 * @return  A header row, then one row per X sample: the X value, then each series of each panel in order.
 * @note    Headers carry the unit of their axis, and the panel title when the figure has more than one panel,
 *          since the panels of a Bode plot list the same traces. Missing samples are left empty.
 */
std::string RenderCsv(const ExportFigure &figure) {
    if (figure.Smith) {
        return RenderSmithCsv(*figure.Smith);
    }
    std::vector<const ExportSeries *> columns;
    std::string text = CsvHeader(figure.XAxis.Name, figure.XAxis.Unit);
    for (const ExportPanel &panel : figure.Panels) {
        for (const ExportSeries &series : panel.Series) {
            if (!series.InTable) {
                continue;
            }
            const std::string name = figure.Panels.size() > 1 && !panel.Title.empty()
                                         ? std::format("{} {}", panel.Title, series.Name)
                                         : series.Name;
            const std::string unit =
                series.TableUnit.value_or(series.Axis < panel.YAxes.size() ? panel.YAxes[series.Axis].Unit : "");
            text += "," + CsvHeader(name, unit);
            columns.push_back(&series);
        }
    }
    text += "\n";
    for (std::size_t row = 0; row < figure.Xs.size(); ++row) {
        text += FormatCsvValue(figure.Xs[row]);
        for (const ExportSeries *series : columns) {
            text += ",";
            const std::vector<double> &samples = series->Samples.empty() ? series->Values : series->Samples;
            if (row < samples.size()) {
                text += FormatCsvValue(samples[row]);
            }
        }
        text += "\n";
    }
    return text;
}

} // namespace GUI
