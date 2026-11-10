/**
 * @file    raw_exporter.cpp
 * @brief   Exports simulation results in Nutmeg SPICE rawfile format.
 */

#include "nodalys/exporters.h"

#include <cmath>
#include <format>
#include <numbers>
#include <sstream>

namespace nodalys {

namespace {

std::string CurrentDateString() {
    return "Mon Nov  9 22:00:00 2026";
}

} // namespace

std::string ExportRaw(const OperatingPointResult &result, std::string_view title) {
    std::ostringstream ss;
    ss << "Title: " << title << "\n";
    ss << "Date: " << CurrentDateString() << "\n";
    ss << "Plotname: Operating Point\n";
    ss << "Flags: real\n";

    const std::size_t num_nodes = result.NodeVoltages.size() > 1 ? result.NodeVoltages.size() - 1 : 0;
    const std::size_t num_currents = result.Currents.size();
    const std::size_t num_vars = num_nodes + num_currents;

    ss << "No. Variables: " << num_vars << "\n";
    ss << "No. Points: 1\n";
    ss << "Variables:\n";

    std::size_t var_idx = 0;
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << "\t" << var_idx++ << "\tv(" << node << ")\tvoltage\n";
    }
    for (const auto &cur : result.Currents) {
        ss << "\t" << var_idx++ << "\ti(" << cur.Name << ")\tcurrent\n";
    }

    ss << "Values:\n";
    bool first = true;
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        if (first) {
            ss << "0\t" << std::format("{:.15e}\n", result.NodeVoltages[node]);
            first = false;
        } else {
            ss << "\t" << std::format("{:.15e}\n", result.NodeVoltages[node]);
        }
    }
    for (const auto &cur : result.Currents) {
        if (first) {
            ss << "0\t" << std::format("{:.15e}\n", cur.Current);
            first = false;
        } else {
            ss << "\t" << std::format("{:.15e}\n", cur.Current);
        }
    }
    return ss.str();
}

std::string ExportRaw(const TransientResult &result, std::string_view title) {
    std::ostringstream ss;
    ss << "Title: " << title << "\n";
    ss << "Date: " << CurrentDateString() << "\n";
    ss << "Plotname: Transient Analysis\n";
    ss << "Flags: real\n";

    const std::size_t num_nodes = result.NodeVoltages.size() > 1 ? result.NodeVoltages.size() - 1 : 0;
    const std::size_t num_currents = result.Currents.size();
    const std::size_t num_vars = 1 + num_nodes + num_currents;
    const std::size_t num_points = result.Times.size();

    ss << "No. Variables: " << num_vars << "\n";
    ss << "No. Points: " << num_points << "\n";
    ss << "Variables:\n";

    std::size_t var_idx = 0;
    ss << "\t" << var_idx++ << "\ttime\ttime\n";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << "\t" << var_idx++ << "\tv(" << node << ")\tvoltage\n";
    }
    for (const auto &cur : result.Currents) {
        ss << "\t" << var_idx++ << "\ti(" << cur.Name << ")\tcurrent\n";
    }

    ss << "Values:\n";
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << pt << "\t" << std::format("{:.15e}\n", result.Times[pt]);
        for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
            const double v = (pt < result.NodeVoltages[node].size()) ? result.NodeVoltages[node][pt] : 0.0;
            ss << "\t" << std::format("{:.15e}\n", v);
        }
        for (const auto &cur : result.Currents) {
            const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
            ss << "\t" << std::format("{:.15e}\n", i);
        }
    }
    return ss.str();
}

std::string ExportRaw(const DcSweepResult &result, std::string_view title) {
    std::ostringstream ss;
    ss << "Title: " << title << "\n";
    ss << "Date: " << CurrentDateString() << "\n";
    ss << "Plotname: DC transfer characteristic\n";
    ss << "Flags: real\n";

    if (result.Curves.empty() || result.SweptValues.empty()) {
        ss << "No. Variables: 0\nNo. Points: 0\nVariables:\nValues:\n";
        return ss.str();
    }

    const auto &first_curve = result.Curves.front();
    const std::size_t num_nodes = first_curve.NodeVoltages.size() > 1 ? first_curve.NodeVoltages.size() - 1 : 0;
    const std::size_t num_currents = first_curve.Currents.size();
    const std::size_t num_vars = 1 + num_nodes + num_currents;
    const std::size_t num_points = result.SweptValues.size() * result.Curves.size();

    ss << "No. Variables: " << num_vars << "\n";
    ss << "No. Points: " << num_points << "\n";
    ss << "Variables:\n";

    std::size_t var_idx = 0;
    ss << "\t" << var_idx++ << "\t" << result.SweptSource << "\t" << (result.SweptUnit.empty() ? "voltage" : result.SweptUnit) << "\n";
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        ss << "\t" << var_idx++ << "\tv(" << node << ")\tvoltage\n";
    }
    for (const auto &cur : first_curve.Currents) {
        ss << "\t" << var_idx++ << "\ti(" << cur.Name << ")\tcurrent\n";
    }

    ss << "Values:\n";
    std::size_t global_idx = 0;
    for (const auto &curve : result.Curves) {
        for (std::size_t pt = 0; pt < result.SweptValues.size(); ++pt) {
            ss << global_idx++ << "\t" << std::format("{:.15e}\n", result.SweptValues[pt]);
            for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
                const double v = (pt < curve.NodeVoltages[node].size()) ? curve.NodeVoltages[node][pt] : 0.0;
                ss << "\t" << std::format("{:.15e}\n", v);
            }
            for (const auto &cur : curve.Currents) {
                const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
                ss << "\t" << std::format("{:.15e}\n", i);
            }
        }
    }
    return ss.str();
}

std::string ExportRaw(const AcSweepResult &result, std::string_view title) {
    std::ostringstream ss;
    ss << "Title: " << title << "\n";
    ss << "Date: " << CurrentDateString() << "\n";
    ss << "Plotname: AC Analysis\n";
    ss << "Flags: complex\n";

    const std::size_t num_nodes = result.NodeMagnitudesDecibels.size() > 1 ? result.NodeMagnitudesDecibels.size() - 1 : 0;
    const std::size_t num_currents = result.CurrentMagnitudesDecibels.size();
    const std::size_t num_vars = 1 + num_nodes + num_currents;
    const std::size_t num_points = result.Frequencies.size();

    ss << "No. Variables: " << num_vars << "\n";
    ss << "No. Points: " << num_points << "\n";
    ss << "Variables:\n";

    std::size_t var_idx = 0;
    ss << "\t" << var_idx++ << "\tfrequency\tfrequency\n";
    for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
        ss << "\t" << var_idx++ << "\tv(" << node << ")\tvoltage\n";
    }
    for (const auto &cur : result.CurrentMagnitudesDecibels) {
        ss << "\t" << var_idx++ << "\ti(" << cur.Name << ")\tcurrent\n";
    }

    ss << "Values:\n";
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        // frequency has 0 imaginary part
        ss << pt << "\t" << std::format("{:.15e},{:.15e}\n", result.Frequencies[pt], 0.0);
        for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
            const double db = result.NodeMagnitudesDecibels[node][pt];
            const double deg = result.NodePhasesDegrees[node][pt];
            const double mag = std::pow(10.0, db / 20.0);
            const double rad = deg * (std::numbers::pi / 180.0);
            const double re = mag * std::cos(rad);
            const double im = mag * std::sin(rad);
            ss << "\t" << std::format("{:.15e},{:.15e}\n", re, im);
        }
        for (std::size_t c = 0; c < result.CurrentMagnitudesDecibels.size(); ++c) {
            const double db = result.CurrentMagnitudesDecibels[c].Values[pt];
            const double deg = result.CurrentPhasesDegrees[c].Values[pt];
            const double mag = std::pow(10.0, db / 20.0);
            const double rad = deg * (std::numbers::pi / 180.0);
            const double re = mag * std::cos(rad);
            const double im = mag * std::sin(rad);
            ss << "\t" << std::format("{:.15e},{:.15e}\n", re, im);
        }
    }
    return ss.str();
}

} // namespace nodalys
