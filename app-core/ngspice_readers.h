/**
 * @file    ngspice_readers.h
 * @brief   Reads the results of each analysis from the current plot of ngspice.
 */

#ifndef IMCSIM_NGSPICE_READERS_H
#define IMCSIM_NGSPICE_READERS_H

#include "analysis_settings.h"
#include "circuit.h"
#include "simulation_results.h"
#include <cstddef>
#include <expected>
#include <optional>
#include <string>

namespace Core {

std::expected<OperatingPoint, std::string> ReadOperatingPoint(const Circuit &circuit);
std::expected<Transient, std::string> ReadTransient(const Circuit &circuit);
std::expected<Transient, std::string> ReadTransientPoints(const Circuit &circuit, std::size_t first_point);
std::optional<double> ReadTransientTime();
std::expected<ACSweep, std::string> ReadACSweep(const Circuit &circuit);
std::expected<DCSweep, std::string> ReadDCSweep(const Circuit &circuit, const DCSweepSettings &settings);
std::expected<Noise, std::string> ReadNoise(const Circuit &circuit, const NoiseSettings &settings);
std::expected<TransferFunction, std::string> ReadTransferFunction(const Circuit &circuit,
                                                                  const TransferFunctionSettings &settings);

} // namespace Core

#endif // IMCSIM_NGSPICE_READERS_H
