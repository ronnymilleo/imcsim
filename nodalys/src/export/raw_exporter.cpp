/**
 * @file    raw_exporter.cpp
 * @brief   Exports simulation results in Nutmeg SPICE rawfile format.
 */

#include "nodalys/exporters.h"

#include "export/raw_plot.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>

namespace Nodalys {

namespace {

/**
 * @brief   Reads a series at a point, or zero where the series stops short of it.
 */
template <typename T>
T SampleAt(const std::vector<T> &series, const std::size_t point) {
    return point < series.size() ? series[point] : T{};
}

/**
 * @brief   Adds the voltage of every node but ground, which is the first of the series.
 */
template <typename Series>
void AddNodeVariables(RawPlot &plot, const Series &nodes) {
    for (std::size_t node = 1; node < nodes.size(); ++node) {
        plot.AddVariable(std::format("v({})", node), "voltage");
    }
}

/**
 * @brief   Adds the current of every component, named after it.
 */
template <typename Components>
void AddCurrentVariables(RawPlot &plot, const Components &components) {
    for (const auto &component : components) {
        plot.AddVariable(std::format("i({})", component.Name), "current");
    }
}

/**
 * @brief   Adds the real values of every node but ground and of every component at one point.
 */
void AddRealSamples(RawPlot &plot,
                    const std::vector<std::vector<double>> &nodes,
                    const std::vector<ComponentTrace> &components,
                    const std::size_t point) {
    for (std::size_t node = 1; node < nodes.size(); ++node) {
        plot.AddReal(SampleAt(nodes[node], point));
    }
    for (const ComponentTrace &component : components) {
        plot.AddReal(SampleAt(component.Values, point));
    }
}

/**
 * @brief   Converts a magnitude in decibels and a phase in degrees to the complex value they stand for.
 */
std::complex<double> PhasorFromDecibels(const double magnitude_decibels, const double phase_degrees) {
    const double magnitude = std::pow(10.0, magnitude_decibels / 20.0);
    const double phase_radians = phase_degrees * (std::numbers::pi / 180.0);
    return {magnitude * std::cos(phase_radians), magnitude * std::sin(phase_radians)};
}

/**
 * @brief   Builds a plot of voltages and currents against time, as the transient and the waveform of a harmonic
 *          balance both write it.
 */
RawPlot MakeTimePlot(std::string plotname,
                     const std::vector<double> &times,
                     const std::vector<std::vector<double>> &nodes,
                     const std::vector<ComponentTrace> &components) {
    RawPlot plot(std::move(plotname), false);
    plot.AddVariable("time", "time");
    AddNodeVariables(plot, nodes);
    AddCurrentVariables(plot, components);
    for (std::size_t pt = 0; pt < times.size(); ++pt) {
        plot.BeginPoint();
        plot.AddReal(times[pt]);
        AddRealSamples(plot, nodes, components, pt);
    }
    return plot;
}

} // namespace

/**
 * @brief   Writes an operating point as one point of node voltages and component currents.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const OperatingPointResult &result, std::string_view title) {
    RawPlot plot("Operating Point", false);
    AddNodeVariables(plot, result.NodeVoltages);
    AddCurrentVariables(plot, result.Currents);

    plot.BeginPoint();
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        plot.AddReal(result.NodeVoltages[node]);
    }
    for (const ComponentCurrent &component : result.Currents) {
        plot.AddReal(component.Current);
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes a transient as time, node voltages and component currents at each time point.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const TransientResult &result, std::string_view title) {
    return MakeTimePlot("Transient Analysis", result.Times, result.NodeVoltages, result.Currents).ToText(title);
}

/**
 * @brief   Writes a DC sweep as the swept value, node voltages and component currents, one curve after the other.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const DcSweepResult &result, std::string_view title) {
    RawPlot plot("DC transfer characteristic", false);
    if (result.Curves.empty() || result.SweptValues.empty()) {
        return plot.ToText(title);
    }

    const DcSweepCurve &first_curve = result.Curves.front();
    plot.AddVariable(result.SweptSource, result.SweptUnit.empty() ? "voltage" : result.SweptUnit);
    AddNodeVariables(plot, first_curve.NodeVoltages);
    AddCurrentVariables(plot, first_curve.Currents);

    for (const DcSweepCurve &curve : result.Curves) {
        for (std::size_t pt = 0; pt < result.SweptValues.size(); ++pt) {
            plot.BeginPoint();
            plot.AddReal(result.SweptValues[pt]);
            AddRealSamples(plot, curve.NodeVoltages, curve.Currents, pt);
        }
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes an AC sweep as complex values, turning the magnitudes and phases back into real and imaginary parts.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const AcSweepResult &result, std::string_view title) {
    RawPlot plot("AC Analysis", true);
    plot.AddVariable("frequency", "frequency");
    AddNodeVariables(plot, result.NodeMagnitudesDecibels);
    AddCurrentVariables(plot, result.CurrentMagnitudesDecibels);

    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        plot.BeginPoint();
        plot.AddComplex({result.Frequencies[pt], 0.0});
        for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
            plot.AddComplex(
                PhasorFromDecibels(result.NodeMagnitudesDecibels[node][pt], result.NodePhasesDegrees[node][pt]));
        }
        for (std::size_t c = 0; c < result.CurrentMagnitudesDecibels.size(); ++c) {
            plot.AddComplex(PhasorFromDecibels(result.CurrentMagnitudesDecibels[c].Values[pt],
                                               result.CurrentPhasesDegrees[c].Values[pt]));
        }
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes S-parameters as one complex variable for each pair of ports.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const SParameterResult &result, std::string_view title) {
    RawPlot plot("S-Parameter Analysis", true);
    const int ports = result.GetPortCount();
    plot.AddVariable("frequency", "frequency");
    for (int i = 1; i <= ports; ++i) {
        for (int j = 1; j <= ports; ++j) {
            plot.AddVariable(std::format("s_{}_{}", i, j), "S-parameter");
        }
    }

    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        plot.BeginPoint();
        plot.AddComplex({result.Frequencies[pt], 0.0});
        for (int i = 1; i <= ports; ++i) {
            for (int j = 1; j <= ports; ++j) {
                plot.AddComplex(result.Get(pt, i, j));
            }
        }
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes the poles and the zeros as two complex variables, the shorter list padded with zeros.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const PoleZeroResult &result, std::string_view title) {
    constexpr std::string_view PADDING_CELL = "0.0,0.0";

    RawPlot plot("Pole-Zero Analysis", true);
    plot.AddVariable("pole", "complex");
    plot.AddVariable("zero", "complex");

    const std::size_t num_points = std::max(result.Poles.size(), result.Zeros.size());
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        plot.BeginPoint();
        if (pt < result.Poles.size()) {
            plot.AddComplex(result.Poles[pt]);
        } else {
            plot.AddText(PADDING_CELL);
        }
        if (pt < result.Zeros.size()) {
            plot.AddComplex(result.Zeros[pt]);
        } else {
            plot.AddText(PADDING_CELL);
        }
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes a DC sensitivity as one point with one variable for each sensitivity vector.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const SensitivityResult &result, std::string_view title) {
    RawPlot plot("DC Sensitivity Analysis", false);
    for (const auto &[name, value] : result.Vectors) {
        (void)value;
        plot.AddVariable(name, "voltage");
    }

    plot.BeginPoint();
    for (const auto &[name, value] : result.Vectors) {
        (void)name;
        plot.AddReal(value);
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes an AC sensitivity as the output and the derivative of each element at every frequency.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile.
 */
std::string ExportRaw(const AcSensitivityResult &result, std::string_view title) {
    RawPlot plot("AC Sensitivity Analysis", true);

    // The output is named as the user writes it in the directive, v(out) or v(out,ref)
    const std::string output_name = result.OutputNegativeNode == 0
                                      ? std::format("v({})", result.OutputPositiveNode)
                                      : std::format("v({},{})", result.OutputPositiveNode, result.OutputNegativeNode);
    plot.AddVariable("frequency", "frequency");
    plot.AddVariable(output_name, "voltage");
    for (const AcElementSensitivity &element : result.Elements) {
        plot.AddVariable(element.Name, "voltage");
    }

    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        plot.BeginPoint();
        plot.AddComplex({result.Frequencies[pt], 0.0});
        plot.AddComplex(result.Output[pt]);
        for (const AcElementSensitivity &element : result.Elements) {
            plot.AddComplex(element.Derivatives[pt]);
        }
    }
    return plot.ToText(title);
}

/**
 * @brief   Writes a harmonic balance as two plots: the spectrum, and the waveform when one was reconstructed.
 * @param[in] result  Result to write.
 * @param[in] title   Title of the simulation, written in the header.
 * @return  The text of the rawfile, with the plot of the waveform after the plot of the spectrum.
 */
std::string ExportRaw(const HarmonicBalanceResult &result, std::string_view title) {
    // Plot 1: Complex harmonic spectrum
    RawPlot spectrum("Harmonic Balance Spectrum", true);
    spectrum.AddVariable("frequency", "frequency");
    AddNodeVariables(spectrum, result.NodePhasors);
    AddCurrentVariables(spectrum, result.CurrentPhasors);

    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        spectrum.BeginPoint();
        spectrum.AddComplex({result.Frequencies[pt], 0.0});
        for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
            spectrum.AddComplex(SampleAt(result.NodePhasors[node], pt));
        }
        for (const ComponentPhasors &component : result.CurrentPhasors) {
            spectrum.AddComplex(SampleAt(component.Phasors, pt));
        }
    }
    std::string text = spectrum.ToText(title);

    // Plot 2: Reconstructed periodic waveform
    if (!result.Times.empty()) {
        text += MakeTimePlot("Harmonic Balance Waveform", result.Times, result.NodeWaveforms, result.CurrentWaveforms)
                    .ToText(title);
    }
    return text;
}

} // namespace Nodalys
