/**
 * @file    pwl_import.h
 * @brief   Reads piecewise linear (PWL) source points from CSV tables and uncompressed WAV audio files.
 */

#ifndef IMCSIM_PWL_IMPORT_H
#define IMCSIM_PWL_IMPORT_H

#include "circuit/components/source.h"
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/// Largest number of frames accepted from a WAV file, avoiding unbounded memory use on very long recordings.
inline constexpr std::size_t MAX_WAV_SAMPLES = 2'000'000;

// Parsing from memory
[[nodiscard]] std::expected<std::vector<PWLPoint>, std::string> ParsePWLCSV(std::string_view text);
[[nodiscard]] std::expected<std::vector<PWLPoint>, std::string> ParsePWLWAV(std::span<const std::byte> bytes,
                                                                            double peak_amplitude = 1.0);

// Loading from disk
[[nodiscard]] std::expected<std::vector<PWLPoint>, std::string> LoadPWLCSVFile(const std::filesystem::path &path);
[[nodiscard]] std::expected<std::vector<PWLPoint>, std::string> LoadPWLWAVFile(const std::filesystem::path &path,
                                                                               double peak_amplitude = 1.0);
[[nodiscard]] std::expected<std::vector<PWLPoint>, std::string> LoadPWLFile(const std::filesystem::path &path,
                                                                            double wav_peak_amplitude = 1.0);

} // namespace Core

#endif // IMCSIM_PWL_IMPORT_H
