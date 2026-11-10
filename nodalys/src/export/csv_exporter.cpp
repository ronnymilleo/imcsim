#include "nodalys/exporters.h"

#include <format>
#include <sstream>

namespace nodalys {

std::string ExportCsv(const OperatingPointResult &result) {
    std::ostringstream ss;
    ss << "Variable,Type,Value,Unit\n";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << "V(" << node << "),Voltage," << std::format("{:.8g}", result.NodeVoltages[node]) << ",V\n";
    }
    for (const auto &cur : result.Currents) {
        ss << "I(" << cur.Name << "),Current," << std::format("{:.8g}", cur.Current) << ",A\n";
    }
    return ss.str();
}

std::string ExportCsv(const TransientResult &result) {
    std::ostringstream ss;
    ss << "Time";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << ",V(" << node << ")";
    }
    for (const auto &cur : result.Currents) {
        ss << ",I(" << cur.Name << ")";
    }
    ss << "\n";

    const std::size_t num_points = result.Times.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Times[pt]);
        for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
            const double v = (pt < result.NodeVoltages[node].size()) ? result.NodeVoltages[node][pt] : 0.0;
            ss << "," << std::format("{:.8g}", v);
        }
        for (const auto &cur : result.Currents) {
            const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
            ss << "," << std::format("{:.8g}", i);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const DcSweepResult &result) {
    std::ostringstream ss;
    if (result.Curves.empty() || result.SweptValues.empty()) {
        return "Empty\n";
    }
    const auto &first_curve = result.Curves.front();
    ss << result.SweptSource;
    if (!result.SteppedSource.empty()) {
        ss << "," << result.SteppedSource;
    }
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        ss << ",V(" << node << ")";
    }
    for (const auto &cur : first_curve.Currents) {
        ss << ",I(" << cur.Name << ")";
    }
    ss << "\n";

    for (const auto &curve : result.Curves) {
        for (std::size_t pt = 0; pt < result.SweptValues.size(); ++pt) {
            ss << std::format("{:.8g}", result.SweptValues[pt]);
            if (!result.SteppedSource.empty()) {
                ss << "," << std::format("{:.8g}", curve.StepValue);
            }
            for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
                const double v = (pt < curve.NodeVoltages[node].size()) ? curve.NodeVoltages[node][pt] : 0.0;
                ss << "," << std::format("{:.8g}", v);
            }
            for (const auto &cur : curve.Currents) {
                const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
                ss << "," << std::format("{:.8g}", i);
            }
            ss << "\n";
        }
    }
    return ss.str();
}

std::string ExportCsv(const AcSweepResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz";
    for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
        ss << ",V(" << node << ")_dB,V(" << node << ")_Phase_deg";
    }
    for (const auto &cur : result.CurrentMagnitudesDecibels) {
        ss << ",I(" << cur.Name << ")_dB,I(" << cur.Name << ")_Phase_deg";
    }
    ss << "\n";

    const std::size_t num_points = result.Frequencies.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
            const double db = (pt < result.NodeMagnitudesDecibels[node].size()) ? result.NodeMagnitudesDecibels[node][pt] : 0.0;
            const double deg = (pt < result.NodePhasesDegrees[node].size()) ? result.NodePhasesDegrees[node][pt] : 0.0;
            ss << "," << std::format("{:.8g},{:.8g}", db, deg);
        }
        for (std::size_t c = 0; c < result.CurrentMagnitudesDecibels.size(); ++c) {
            const double db = (pt < result.CurrentMagnitudesDecibels[c].Values.size()) ? result.CurrentMagnitudesDecibels[c].Values[pt] : 0.0;
            const double deg = (pt < result.CurrentPhasesDegrees[c].Values.size()) ? result.CurrentPhasesDegrees[c].Values[pt] : 0.0;
            ss << "," << std::format("{:.8g},{:.8g}", db, deg);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const TransferFunctionResult &result) {
    std::ostringstream ss;
    ss << "Metric,Value,Unit\n";
    ss << "Gain," << std::format("{:.8g}", result.Gain) << "," << result.GainUnit << "\n";
    ss << "InputImpedance," << std::format("{:.8g}", result.InputImpedance) << ",Ohm\n";
    ss << "OutputImpedance," << std::format("{:.8g}", result.OutputImpedance) << ",Ohm\n";
    return ss.str();
}

std::string ExportCsv(const NoiseResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz,OutputDensity_V_rtHz,InputDensity";
    for (const auto &c : result.Contributions) {
        ss << "," << c.Name << "_Density";
    }
    ss << "\n";

    const std::size_t num_points = result.Frequencies.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        ss << "," << std::format("{:.8g}", result.OutputDensities[pt]);
        ss << "," << std::format("{:.8g}", result.InputDensities[pt]);
        for (const auto &c : result.Contributions) {
            const double dens = (pt < c.OutputDensities.size()) ? c.OutputDensities[pt] : 0.0;
            ss << "," << std::format("{:.8g}", dens);
        }
        ss << "\n";
    }
    return ss.str();
}

} // namespace nodalys
