#ifndef NODALYS_EXPORTERS_H
#define NODALYS_EXPORTERS_H

#include "nodalys/results.h"
#include <string>
#include <string_view>

namespace nodalys {

// RAW (SPICE Nutmeg rawfile format - compatible with LTspice, ngspice, gaw, Qucs, PySpice)
std::string ExportRaw(const OperatingPointResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const TransientResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const DcSweepResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const AcSweepResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const SimulationResult &result, std::string_view title = "Nodalys Simulation");

// CSV (Comma-Separated Values with headers)
std::string ExportCsv(const OperatingPointResult &result);
std::string ExportCsv(const TransientResult &result);
std::string ExportCsv(const DcSweepResult &result);
std::string ExportCsv(const AcSweepResult &result);
std::string ExportCsv(const TransferFunctionResult &result);
std::string ExportCsv(const NoiseResult &result);
std::string ExportCsv(const SimulationResult &result);

// JSON (Structured data format with metadata, variables, and series)
std::string ExportJson(const OperatingPointResult &result);
std::string ExportJson(const TransientResult &result);
std::string ExportJson(const DcSweepResult &result);
std::string ExportJson(const AcSweepResult &result);
std::string ExportJson(const TransferFunctionResult &result);
std::string ExportJson(const NoiseResult &result);
std::string ExportJson(const SimulationResult &result);

// File helper functions
bool SaveStringToFile(std::string_view filepath, std::string_view content);

} // namespace nodalys

#endif // NODALYS_EXPORTERS_H
