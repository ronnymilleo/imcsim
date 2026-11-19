/**
 * @file    json_exporter.cpp
 * @brief   Exports simulation results in JSON structured format.
 */

#include "nodalys/exporters.h"

#include <complex>
#include <format>
#include <numbers>
#include <optional>
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

/**
 * @brief   Writes the matrices of ports of every frequency as an array, with null where the circuit has no finite one.
 */
void WritePortMatrices(std::ostringstream &ss, const std::vector<std::optional<PortMatrix>> &matrices) {
    ss << "[\n";
    for (std::size_t pt = 0; pt < matrices.size(); ++pt) {
        ss << "    ";
        const std::optional<PortMatrix> &matrix = matrices[pt];
        if (matrix) {
            ss << "[";
            for (std::size_t index = 0; index < matrix->size(); ++index) {
                const std::complex<double> value = (*matrix)[index];
                ss << (index > 0 ? ", " : "") << "{\"re\": " << std::format("{:.8g}", value.real())
                   << ", \"im\": " << std::format("{:.8g}", value.imag()) << "}";
            }
            ss << "]";
        } else {
            ss << "null";
        }
        ss << (pt + 1 < matrices.size() ? ",\n" : "\n");
    }
    ss << "  ]";
}

/**
 * @brief   Writes a text as a JSON string, escaping what JSON does not allow bare.
 */
void WriteQuoted(std::ostringstream &ss, std::string_view text) {
    ss << '"';
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            ss << '\\';
        }
        ss << ch;
    }
    ss << '"';
}

/**
 * @brief   Writes numbers that may be missing, null where the figure is none.
 */
void WriteOptionalArray(std::ostringstream &ss, const std::vector<std::optional<double>> &values) {
    ss << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        ss << (i > 0 ? ", " : "");
        if (const std::optional<double> &value = values[i]; value.has_value()) {
            ss << std::format("{:.8g}", *value);
        } else {
            ss << "null";
        }
    }
    ss << "]";
}

/**
 * @brief   Writes the real or the imaginary parts of complex values as an array.
 */
void WriteComplexPart(std::ostringstream &ss, const std::vector<std::complex<double>> &values, const bool is_real) {
    ss << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        ss << (i > 0 ? ", " : "") << std::format("{:.8g}", is_real ? values[i].real() : values[i].imag());
    }
    ss << "]";
}

/**
 * @brief   Opens a document: the brace and the name of the analysis, which every result starts with.
 */
void BeginDocument(std::ostringstream &ss, const std::string_view analysis) {
    ss << "{\n  \"analysis\": \"" << analysis << "\",\n";
}

/**
 * @brief   Writes the members of an object that hold a series for each node but ground, one per line.
 * @param[in,out] ss       Stream to write to.
 * @param[in] indent       Spaces in front of each member.
 * @param[in] nodes        Series indexed by node, whose first entry is ground and is left out.
 * @param[in] write_array  Writes one series to the stream.
 */
template <typename Series, typename WriteArray>
void WriteNodeSeries(std::ostringstream &ss,
                     const std::string_view indent,
                     const Series &nodes,
                     const WriteArray &write_array) {
    for (std::size_t node = 1; node < nodes.size(); ++node) {
        if (node > 1) {
            ss << ",\n";
        }
        ss << indent << "\"" << node << "\": ";
        write_array(ss, nodes[node]);
    }
}

/**
 * @brief   Writes the real and imaginary parts, the magnitude and the phase of each phasor of a series as an array.
 */
void WritePhasorArray(std::ostringstream &ss, const std::vector<std::complex<double>> &phasors) {
    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;
    ss << "[";
    for (std::size_t k = 0; k < phasors.size(); ++k) {
        if (k > 0) {
            ss << ", ";
        }
        const std::complex<double> &p = phasors[k];
        const double mag = std::abs(p);
        const double phase_deg = std::arg(p) * RAD_TO_DEG;
        ss << "{\"re\": " << std::format("{:.8g}", p.real()) << ", \"im\": " << std::format("{:.8g}", p.imag())
           << ", \"mag\": " << std::format("{:.8g}", mag) << ", \"phase_deg\": " << std::format("{:.8g}", phase_deg)
           << "}";
    }
    ss << "]";
}

/**
 * @brief   Writes the roots of a polynomial, poles or zeros, as an array with the frequency of each in hertz.
 */
void WriteRootList(std::ostringstream &ss, const std::vector<std::complex<double>> &roots) {
    constexpr double TWO_PI = 2.0 * std::numbers::pi;
    ss << "[";
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        const double f_hz = std::abs(roots[i]) / TWO_PI;
        ss << "{\"real\": " << std::format("{:.8g}", roots[i].real())
           << ", \"imag\": " << std::format("{:.8g}", roots[i].imag())
           << ", \"frequency_hz\": " << std::format("{:.8g}", f_hz) << "}";
    }
    ss << "]";
}

const char *DescribeParameter(const SensitivityParameter parameter) {
    switch (parameter) {
    case SensitivityParameter::RESISTANCE:
        return "resistance";
    case SensitivityParameter::CAPACITANCE:
        return "capacitance";
    case SensitivityParameter::INDUCTANCE:
        return "inductance";
    case SensitivityParameter::VOLTAGE:
        return "voltage";
    case SensitivityParameter::CURRENT:
        return "current";
    case SensitivityParameter::GAIN:
        return "gain";
    default:
        return "unknown";
    }
}

} // namespace

std::string ExportJson(const OperatingPointResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "operating_point");
    ss << "  \"node_voltages\": {";
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
    BeginDocument(ss, "transient");
    ss << "  \"points\": " << result.Times.size() << ",\n";
    ss << "  \"times\": ";
    WriteDoubleArray(ss, result.Times);
    ss << ",\n  \"node_voltages\": {\n";
    WriteNodeSeries(ss, "    ", result.NodeVoltages, WriteDoubleArray);
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
    BeginDocument(ss, "dc_sweep");
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
        WriteNodeSeries(ss, "        ", curve.NodeVoltages, WriteDoubleArray);
        ss << "\n      }\n    }";
    }
    ss << "\n  ]\n}\n";
    return ss.str();
}

std::string ExportJson(const AcSweepResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "ac_sweep");
    ss << "  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);
    ss << ",\n  \"node_magnitudes_db\": {\n";
    WriteNodeSeries(ss, "    ", result.NodeMagnitudesDecibels, WriteDoubleArray);
    ss << "\n  },\n  \"node_phases_deg\": {\n";
    WriteNodeSeries(ss, "    ", result.NodePhasesDegrees, WriteDoubleArray);
    ss << "\n  }\n}\n";
    return ss.str();
}

std::string ExportJson(const TransferFunctionResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "transfer_function");
    ss << "  \"input_source\": \"" << result.InputSource << "\",\n";
    ss << "  \"gain\": " << std::format("{:.8g}", result.Gain) << ",\n";
    ss << "  \"gain_unit\": \"" << result.GainUnit << "\",\n";
    ss << "  \"input_impedance_ohm\": " << std::format("{:.8g}", result.InputImpedance) << ",\n";
    ss << "  \"output_impedance_ohm\": " << std::format("{:.8g}", result.OutputImpedance) << "\n}\n";
    return ss.str();
}

std::string ExportJson(const NoiseResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "noise");
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
    BeginDocument(ss, "s_parameters");
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
    ss << "  ]";
    if (result.ImpedanceMatrices.size() == result.Frequencies.size()
        && result.AdmittanceMatrices.size() == result.Frequencies.size()) {
        ss << ",\n  \"impedance\": ";
        WritePortMatrices(ss, result.ImpedanceMatrices);
        ss << ",\n  \"admittance\": ";
        WritePortMatrices(ss, result.AdmittanceMatrices);
    }
    ss << "\n}\n";
    return ss.str();
}

std::string ExportJson(const PoleZeroResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "pole_zero");
    ss << "  \"poles\": ";
    WriteRootList(ss, result.Poles);
    ss << ",\n  \"zeros\": ";
    WriteRootList(ss, result.Zeros);
    ss << "\n}\n";
    return ss.str();
}

std::string ExportJson(const SensitivityResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "sensitivity");
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

std::string ExportJson(const AcSensitivityResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "ac_sensitivity");
    ss << "  \"output_positive_node\": " << result.OutputPositiveNode << ",\n";
    ss << "  \"output_negative_node\": " << result.OutputNegativeNode << ",\n";
    ss << "  \"input_source\": ";
    WriteQuoted(ss, result.InputSource);
    ss << ",\n  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);
    ss << ",\n  \"output_re\": ";
    WriteComplexPart(ss, result.Output, true);
    ss << ",\n  \"output_im\": ";
    WriteComplexPart(ss, result.Output, false);
    ss << ",\n  \"warnings\": [";
    for (std::size_t i = 0; i < result.Warnings.size(); ++i) {
        ss << (i > 0 ? ", " : "");
        WriteQuoted(ss, result.Warnings[i]);
    }
    ss << "],\n  \"elements\": [\n";
    for (std::size_t index = 0; index < result.Elements.size(); ++index) {
        const AcElementSensitivity &element = result.Elements[index];
        ss << "    {\"name\": ";
        WriteQuoted(ss, element.Name);
        ss << ", \"parameter\": \"" << DescribeParameter(element.Parameter)
           << "\", \"value\": " << std::format("{:.8g}", element.Value) << ",\n     \"derivative_re\": ";
        WriteComplexPart(ss, element.Derivatives, true);
        ss << ",\n     \"derivative_im\": ";
        WriteComplexPart(ss, element.Derivatives, false);
        ss << ",\n     \"magnitude_db_per_percent\": ";
        WriteOptionalArray(ss, element.MagnitudeDecibelsPerPercent);
        ss << ",\n     \"phase_deg_per_percent\": ";
        WriteOptionalArray(ss, element.PhaseDegreesPerPercent);
        ss << "}" << (index + 1 < result.Elements.size() ? ",\n" : "\n");
    }
    ss << "  ]\n}\n";
    return ss.str();
}

std::string ExportJson(const HarmonicBalanceResult &result) {
    std::ostringstream ss;
    BeginDocument(ss, "harmonic_balance");
    ss << "  \"iterations\": " << result.Iterations << ",\n";
    ss << "  \"strategy\": ";
    WriteQuoted(ss, result.Strategy);
    ss << ",\n  \"warnings\": [";
    for (std::size_t i = 0; i < result.Warnings.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        WriteQuoted(ss, result.Warnings[i]);
    }
    ss << "],\n  \"frequencies\": ";
    WriteDoubleArray(ss, result.Frequencies);

    ss << ",\n  \"node_phasors\": {\n";
    WriteNodeSeries(ss, "    ", result.NodePhasors, WritePhasorArray);

    ss << "\n  },\n  \"current_phasors\": {\n";
    for (std::size_t c = 0; c < result.CurrentPhasors.size(); ++c) {
        if (c > 0) {
            ss << ",\n";
        }
        ss << "    ";
        WriteQuoted(ss, result.CurrentPhasors[c].Name);
        ss << ": ";
        WritePhasorArray(ss, result.CurrentPhasors[c].Phasors);
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
    WriteNodeSeries(ss, "    ", result.NodeWaveforms, WriteDoubleArray);

    ss << "\n  },\n  \"current_waveforms\": {\n";
    for (std::size_t c = 0; c < result.CurrentWaveforms.size(); ++c) {
        if (c > 0) {
            ss << ",\n";
        }
        ss << "    ";
        WriteQuoted(ss, result.CurrentWaveforms[c].Name);
        ss << ": ";
        WriteDoubleArray(ss, result.CurrentWaveforms[c].Values);
    }

    ss << "\n  }\n}\n";
    return ss.str();
}

} // namespace Nodalys
