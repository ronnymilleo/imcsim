/**
 * @file    exporters.cpp
 * @brief   Unified simulation result dispatchers and file writer helpers.
 */

#include "nodalys/exporters.h"

#include <format>
#include <fstream>
#include <sstream>

namespace nodalys {

std::string ExportRaw(const SimulationResult &result, std::string_view title) {
    return std::visit(
        [&](const auto &res) -> std::string {
            using T = std::decay_t<decltype(res)>;
            if constexpr (std::is_same_v<T, OperatingPointResult> || std::is_same_v<T, TransientResult> ||
                          std::is_same_v<T, DcSweepResult> || std::is_same_v<T, AcSweepResult> ||
                          std::is_same_v<T, SParameterResult> || std::is_same_v<T, PoleZeroResult>) {
                return ExportRaw(res, title);
            } else {
                // SPICE raw files do not standardly represent .TF or .NOISE direct summaries without vectors
                return ExportCsv(res);
            }
        },
        result);
}

std::string ExportCsv(const SimulationResult &result) {
    return std::visit([](const auto &res) { return ExportCsv(res); }, result);
}

std::string ExportJson(const SimulationResult &result) {
    return std::visit([](const auto &res) { return ExportJson(res); }, result);
}

std::string ExportTouchstone(const SParameterResult &result) {
    const int ports = result.GetPortCount();
    if (ports <= 0) {
        return "";
    }
    const double z0 = result.ReferenceOhms.empty() ? 50.0 : result.ReferenceOhms.front();
    std::ostringstream ss;
    ss << "! Nodalys S-Parameter Simulation\n";
    ss << "# HZ S RI R " << std::format("{:.8g}", z0) << "\n";

    constexpr int PORTS_IN_ONE_LINE = 2;
    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        ss << std::format("{:.10g}", result.Frequencies[pt]);
        if (ports == PORTS_IN_ONE_LINE) {
            // S11, S21, S12, S22
            for (const int col : {1, 2}) {
                for (const int row : {1, 2}) {
                    const auto s = result.Get(pt, row, col);
                    ss << std::format(" {:.10g} {:.10g}", s.real(), s.imag());
                }
            }
            ss << "\n";
        } else {
            for (int row = 1; row <= ports; ++row) {
                if (row > 1) {
                    ss << " ";
                }
                int pairs_in_line = 0;
                for (int col = 1; col <= ports; ++col) {
                    if (pairs_in_line == 4) {
                        ss << "\n ";
                        pairs_in_line = 0;
                    }
                    const auto s = result.Get(pt, row, col);
                    ss << std::format(" {:.10g} {:.10g}", s.real(), s.imag());
                    ++pairs_in_line;
                }
                ss << "\n";
            }
        }
    }
    return ss.str();
}

bool SaveStringToFile(std::string_view filepath, std::string_view content) {
    std::ofstream file{std::string(filepath)};
    if (!file.is_open()) {
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

} // namespace nodalys
