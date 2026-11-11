/**
 * @file    exporters.h
 * @brief   Exports simulation results in Nutmeg SPICE rawfile, CSV, and JSON formats.
 */

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
std::string ExportRaw(const SParameterResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const PoleZeroResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const SensitivityResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const HarmonicBalanceResult &result, std::string_view title = "Nodalys Simulation");
std::string ExportRaw(const SimulationResult &result, std::string_view title = "Nodalys Simulation");

// CSV (Comma-Separated Values with headers)
std::string ExportCsv(const OperatingPointResult &result);
std::string ExportCsv(const TransientResult &result);
std::string ExportCsv(const DcSweepResult &result);
std::string ExportCsv(const AcSweepResult &result);
std::string ExportCsv(const TransferFunctionResult &result);
std::string ExportCsv(const NoiseResult &result);
std::string ExportCsv(const SParameterResult &result);
std::string ExportCsv(const PoleZeroResult &result);
std::string ExportCsv(const SensitivityResult &result);
std::string ExportCsv(const HarmonicBalanceResult &result);
std::string ExportCsv(const SimulationResult &result);

// JSON (Structured data format with metadata, variables, and series)
std::string ExportJson(const OperatingPointResult &result);
std::string ExportJson(const TransientResult &result);
std::string ExportJson(const DcSweepResult &result);
std::string ExportJson(const AcSweepResult &result);
std::string ExportJson(const TransferFunctionResult &result);
std::string ExportJson(const NoiseResult &result);
std::string ExportJson(const SParameterResult &result);
std::string ExportJson(const PoleZeroResult &result);
std::string ExportJson(const SensitivityResult &result);
std::string ExportJson(const HarmonicBalanceResult &result);
std::string ExportJson(const SimulationResult &result);

// Touchstone (Standard RF .sNp Touchstone version 1 format)
std::string ExportTouchstone(const SParameterResult &result);

// File helper functions
[[nodiscard]] bool SaveStringToFile(std::string_view filepath, std::string_view content);

} // namespace nodalys

#endif // NODALYS_EXPORTERS_H
