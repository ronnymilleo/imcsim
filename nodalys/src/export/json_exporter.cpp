/**
 * @file    json_exporter.cpp
 * @brief   Exports simulation results in JSON structured format.
 */

#include "nodalys/exporters.h"

#include <format>
#include <sstream>

namespace nodalys {

namespace {

void WriteDoubleArray(std::ostringstream &ss, const std::vector<double> &arr) {
    ss << "[";
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (i > 0) ss << ", ";
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
        if (!first) ss << ", ";
        first = false;
        ss << "\"" << node << "\": " << std::format("{:.8g}", result.NodeVoltages[node]);
    }
    ss << "},\n  \"currents\": {";
    first = true;
    for (const auto &cur : result.Currents) {
        if (!first) ss << ", ";
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
        if (node > 1) ss << ",\n";
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodeVoltages[node]);
    }
    ss << "\n  },\n  \"currents\": {\n";
    for (std::size_t c = 0; c < result.Currents.size(); ++c) {
        if (c > 0) ss << ",\n";
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
        if (idx > 0) ss << ",\n";
        const auto &curve = result.Curves[idx];
        ss << "    {\n      \"step_value\": " << std::format("{:.8g}", curve.StepValue) << ",\n";
        ss << "      \"node_voltages\": {\n";
        for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
            if (node > 1) ss << ",\n";
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
        if (node > 1) ss << ",\n";
        ss << "    \"" << node << "\": ";
        WriteDoubleArray(ss, result.NodeMagnitudesDecibels[node]);
    }
    ss << "\n  },\n  \"node_phases_deg\": {\n";
    for (std::size_t node = 1; node < result.NodePhasesDegrees.size(); ++node) {
        if (node > 1) ss << ",\n";
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

} // namespace nodalys
