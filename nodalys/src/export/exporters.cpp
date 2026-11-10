#include "nodalys/exporters.h"

#include <fstream>

namespace nodalys {

std::string ExportRaw(const SimulationResult &result, std::string_view title) {
    return std::visit([&](const auto &res) -> std::string {
        using T = std::decay_t<decltype(res)>;
        if constexpr (std::is_same_v<T, OperatingPointResult> ||
                      std::is_same_v<T, TransientResult> ||
                      std::is_same_v<T, DcSweepResult> ||
                      std::is_same_v<T, AcSweepResult>) {
            return ExportRaw(res, title);
        } else {
            // SPICE raw files do not standardly represent .TF or .NOISE direct summaries without vectors
            return ExportCsv(res);
        }
    }, result);
}

std::string ExportCsv(const SimulationResult &result) {
    return std::visit([](const auto &res) {
        return ExportCsv(res);
    }, result);
}

std::string ExportJson(const SimulationResult &result) {
    return std::visit([](const auto &res) {
        return ExportJson(res);
    }, result);
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
