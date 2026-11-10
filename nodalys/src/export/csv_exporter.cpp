/**
 * @file    csv_exporter.cpp
 * @brief   Exports simulation results in CSV tabular format.
 */

#include "nodalys/exporters.h"

#include <format>
#include <numbers>
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

std::string ExportCsv(const SParameterResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz";
    const int ports = result.GetPortCount();
    for (int i = 1; i <= ports; ++i) {
        for (int j = 1; j <= ports; ++j) {
            ss << std::format(",S{0}{1}_dB,S{0}{1}_deg,S{0}{1}_re,S{0}{1}_im", i, j);
        }
    }
    ss << "\n";

    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;
    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        for (int i = 1; i <= ports; ++i) {
            for (int j = 1; j <= ports; ++j) {
                const auto s = result.Get(pt, i, j);
                const double mag = std::abs(s);
                const double mag_db = 20.0 * std::log10(std::max(mag, 1e-15));
                const double phase_deg = std::arg(s) * RAD_TO_DEG;
                ss << std::format(",{:.8g},{:.8g},{:.8g},{:.8g}", mag_db, phase_deg, s.real(), s.imag());
            }
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const PoleZeroResult &result) {
    std::ostringstream ss;
    constexpr double TWO_PI = 2.0 * std::numbers::pi;
    ss << "Type,Index,Real (rad/s),Imag (rad/s),Frequency (Hz)\n";
    for (std::size_t i = 0; i < result.Poles.size(); ++i) {
        const double f_hz = std::abs(result.Poles[i]) / TWO_PI;
        ss << "Pole," << (i + 1) << ","
           << std::format("{:.8g}", result.Poles[i].real()) << ","
           << std::format("{:.8g}", result.Poles[i].imag()) << ","
           << std::format("{:.8g}", f_hz) << "\n";
    }
    for (std::size_t i = 0; i < result.Zeros.size(); ++i) {
        const double f_hz = std::abs(result.Zeros[i]) / TWO_PI;
        ss << "Zero," << (i + 1) << ","
           << std::format("{:.8g}", result.Zeros[i].real()) << ","
           << std::format("{:.8g}", result.Zeros[i].imag()) << ","
           << std::format("{:.8g}", f_hz) << "\n";
    }
    return ss.str();
}

} // namespace nodalys
