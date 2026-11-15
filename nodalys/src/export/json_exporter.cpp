/**
 * @file    json_exporter.cpp
 * @brief   Exports simulation results in JSON structured format.
 */

#include "nodalys/exporters.h"

#include <format>
#include <numbers>
#include <sstream>

namespace Nodalys {

namespace {

void WriteDoubleArray(std::ostringstream &ss, const std::vector<double> &arr) {
    ss << "[";
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << std::format("{:.8g}", arr[i]);
    }
    ss << "]";
}

} // namespace

std::string ExportJson(const OperatingPointResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"operating_point\",\n  \"node_voltages\": {";
    bool first = true;
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        if (!first) {
            ss << ", ";
        }
        first = false;
        ss << "\"" << node << "\": " << std::format("{:.8g}", result.NodeVoltages[node]);
    }
    ss << "},\n  \"currents\": {";
    first = true;
    for (const auto &cur : result.Currents) {
        if (!first) {
            ss << ", ";
        }
        first = false;
        ss << "\"" << cur.Name << "\": " << std::format("{:.8g}", cur.Current);
    }
    ss << "}\n}\n";
    return ss.str();
}

std::string ExportJson(const TransientResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"transient\",\n";
    ss << "  \"points\": " << result.Times.size() << ",\n";
    ss << "  \"times\": ";
    WriteDoubleArray(ss, result.Times);
    ss << ",\n  \"node_voltages\": {\n";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodeVoltages[node]);
    }
    ss << "\n  },\n  \"currents\": {\n";
    for (std::size_t c = 0; c < result.Currents.size(); ++c) {
        if (c > 0) {
            ss << ",\n";
        }
        ss << "    \"" << result.Currents[c].Name << "\": ";
        WriteDoubleArray(ss, result.Currents[c].Values);
    }
    ss << "\n  }\n}\n";
    return ss.str();
}

std::string ExportJson(const DcSweepResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"dc_sweep\",\n";
    ss << "  \"swept_source\": \"" << result.SweptSource << "\",\n";
    ss << "  \"swept_values\": ";
    WriteDoubleArray(ss, result.SweptValues);
    ss << ",\n  \"curves\": [\n";
    for (std::size_t idx = 0; idx < result.Curves.size(); ++idx) {
        if (idx > 0) {
            ss << ",\n";
        }
        const auto &curve = result.Curves[idx];
        ss << "    {\n      \"step_value\": " << std::format("{:.8g}", curve.StepValue) << ",\n";
        ss << "      \"node_voltages\": {\n";
        for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
            if (node > 1) {
                ss << ",\n";
            }
            ss << "        \"" << node << "\": ";
            WriteDoubleArray(ss, curve.NodeVoltages[node]);
        }
        ss << "\n      }\n    }";
    }
    ss << "\n  ]\n}\n";
    return ss.str();
}

std::string ExportJson(const AcSweepResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"ac_sweep\",\n";
    ss << "  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);
    ss << ",\n  \"node_magnitudes_db\": {\n";
    for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodeMagnitudesDecibels[node]);
    }
    ss << "\n  },\n  \"node_phases_deg\": {\n";
    for (std::size_t node = 1; node < result.NodePhasesDegrees.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodePhasesDegrees[node]);
    }
    ss << "\n  }\n}\n";
    return ss.str();
}

std::string ExportJson(const TransferFunctionResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"transfer_function\",\n";
    ss << "  \"input_source\": \"" << result.InputSource << "\",\n";
    ss << "  \"gain\": " << std::format("{:.8g}", result.Gain) << ",\n";
    ss << "  \"gain_unit\": \"" << result.GainUnit << "\",\n";
    ss << "  \"input_impedance_ohm\": " << std::format("{:.8g}", result.InputImpedance) << ",\n";
    ss << "  \"output_impedance_ohm\": " << std::format("{:.8g}", result.OutputImpedance) << "\n}\n";
    return ss.str();
}

std::string ExportJson(const NoiseResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"noise\",\n";
    ss << "  \"input_source\": \"" << result.InputSource << "\",\n";
    ss << "  \"output_total\": " << std::format("{:.8g}", result.OutputTotal) << ",\n";
    ss << "  \"input_total\": " << std::format("{:.8g}", result.InputTotal) << ",\n";
    ss << "  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);
    ss << ",\n  \"output_densities\": ";
    WriteDoubleArray(ss, result.OutputDensities);
    ss << ",\n  \"input_densities\": ";
    WriteDoubleArray(ss, result.InputDensities);
    ss << "\n}\n";
    return ss.str();
}

std::string ExportJson(const SParameterResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"s_parameters\",\n";
    ss << "  \"ports\": " << result.GetPortCount() << ",\n";
    ss << "  \"reference_ohms\": ";
    WriteDoubleArray(ss, result.ReferenceOhms);
    ss << ",\n  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);
    ss << ",\n  \"values\": [\n";
    const int ports = result.GetPortCount();
    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        ss << "    [";
        for (int i = 1; i <= ports; ++i) {
            for (int j = 1; j <= ports; ++j) {
                const auto s = result.Get(pt, i, j);
                if (i > 1 || j > 1) {
                    ss << ", ";
                }
                ss << "{\"re\": " << std::format("{:.8g}", s.real()) << ", \"im\": " << std::format("{:.8g}", s.imag())
                   << "}";
            }
        }
        ss << (pt + 1 < result.Frequencies.size() ? "],\n" : "]\n");
    }
    ss << "  ]\n}\n";
    return ss.str();
}

std::string ExportJson(const PoleZeroResult &result) {
    std::ostringstream ss;
    constexpr double TWO_PI = 2.0 * std::numbers::pi;
    ss << "{\n  \"analysis\": \"pole_zero\",\n";
    ss << "  \"poles\": [";
    for (std::size_t i = 0; i < result.Poles.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        const double f_hz = std::abs(result.Poles[i]) / TWO_PI;
        ss << "{\"real\": " << std::format("{:.8g}", result.Poles[i].real())
           << ", \"imag\": " << std::format("{:.8g}", result.Poles[i].imag())
           << ", \"frequency_hz\": " << std::format("{:.8g}", f_hz) << "}";
    }
    ss << "],\n  \"zeros\": [";
    for (std::size_t i = 0; i < result.Zeros.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        const double f_hz = std::abs(result.Zeros[i]) / TWO_PI;
        ss << "{\"real\": " << std::format("{:.8g}", result.Zeros[i].real())
           << ", \"imag\": " << std::format("{:.8g}", result.Zeros[i].imag())
           << ", \"frequency_hz\": " << std::format("{:.8g}", f_hz) << "}";
    }
    ss << "]\n}\n";
    return ss.str();
}

std::string ExportJson(const SensitivityResult &result) {
    std::ostringstream ss;
    ss << "{\n  \"analysis\": \"sensitivity\",\n";
    ss << "  \"output_positive_node\": " << result.OutputPositiveNode << ",\n";
    ss << "  \"output_negative_node\": " << result.OutputNegativeNode << ",\n";
    ss << "  \"vectors\": {\n";
    bool first = true;
    for (const auto &[name, val] : result.Vectors) {
        if (!first) {
            ss << ",\n";
        }
        first = false;
        ss << "    \"" << name << "\": " << std::format("{:.8g}", val);
    }
    ss << "\n  }\n}\n";
    return ss.str();
}

std::string ExportJson(const HarmonicBalanceResult &result) {
    std::ostringstream ss;
    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;

    auto write_escaped = [&](std::string_view text) {
        ss << '"';
        for (const char ch : text) {
            if (ch == '"' || ch == '\\') {
                ss << '\\';
            }
            ss << ch;
        }
        ss << '"';
    };

    auto write_phasor_array = [&](const std::vector<std::complex<double>> &phasors) {
        ss << "[";
        for (std::size_t k = 0; k < phasors.size(); ++k) {
            if (k > 0) {
                ss << ", ";
            }
            const auto &p = phasors[k];
            const double mag = std::abs(p);
            const double phase_deg = std::arg(p) * RAD_TO_DEG;
            ss << "{\"re\": " << std::format("{:.8g}", p.real()) << ", \"im\": " << std::format("{:.8g}", p.imag())
               << ", \"mag\": " << std::format("{:.8g}", mag) << ", \"phase_deg\": " << std::format("{:.8g}", phase_deg)
               << "}";
        }
        ss << "]";
    };

    ss << "{\n  \"analysis\": \"harmonic_balance\",\n";
    ss << "  \"iterations\": " << result.Iterations << ",\n";
    ss << "  \"strategy\": ";
    write_escaped(result.Strategy);
    ss << ",\n  \"warnings\": [";
    for (std::size_t i = 0; i < result.Warnings.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        write_escaped(result.Warnings[i]);
    }
    ss << "],\n  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);

    ss << ",\n  \"node_phasors\": {\n";
    for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << "    \"" << node << "\": ";
        write_phasor_array(result.NodePhasors[node]);
    }

    ss << "\n  },\n  \"current_phasors\": {\n";
    for (std::size_t c = 0; c < result.CurrentPhasors.size(); ++c) {
        if (c > 0) {
            ss << ",\n";
        }
        ss << "    ";
        write_escaped(result.CurrentPhasors[c].Name);
        ss << ": ";
        write_phasor_array(result.CurrentPhasors[c].Phasors);
    }

    ss << "\n  },\n  \"thd_percent\": {\n";
    bool first_thd = true;
    for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
        if (!first_thd) {
            ss << ",\n";
        }
        first_thd = false;
        ss << "    \"v(" << node << ")\": " << std::format("{:.6g}", ComputeThd(result.NodePhasors[node]) * 100.0);
    }
    for (const auto &cur : result.CurrentPhasors) {
        if (!first_thd) {
            ss << ",\n";
        }
        first_thd = false;
        ss << "    \"i(" << cur.Name << ")\": " << std::format("{:.6g}", ComputeThd(cur.Phasors) * 100.0);
    }

    ss << "\n  },\n  \"times\": ";
    WriteDoubleArray(ss, result.Times);

    ss << ",\n  \"node_waveforms\": {\n";
    for (std::size_t node = 1; node < result.NodeWaveforms.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodeWaveforms[node]);
    }

    ss << "\n  },\n  \"current_waveforms\": {\n";
    for (std::size_t c = 0; c < result.CurrentWaveforms.size(); ++c) {
        if (c > 0) {
            ss << ",\n";
        }
        ss << "    ";
        write_escaped(result.CurrentWaveforms[c].Name);
        ss << ": ";
        WriteDoubleArray(ss, result.CurrentWaveforms[c].Values);
    }

    ss << "\n  }\n}\n";
    return ss.str();
}

} // namespace Nodalys
