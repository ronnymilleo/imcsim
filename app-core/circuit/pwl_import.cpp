/**
 * @file    pwl_import.cpp
 * @brief   Reads piecewise linear (PWL) source points from CSV tables and uncompressed WAV audio files.
 */

#include "circuit/pwl_import.h"

#include "spice/spice_text.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace Core {

namespace {

constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";
constexpr std::size_t RIFF_HEADER_SIZE = 12;
constexpr std::size_t CHUNK_HEADER_SIZE = 8;
constexpr std::size_t FMT_MIN_SIZE = 16;
constexpr std::size_t FMT_EXTENSIBLE_SIZE = 40;
constexpr std::uint16_t WAVE_FORMAT_PCM = 1;
constexpr std::uint16_t WAVE_FORMAT_IEEE_FLOAT = 3;
constexpr std::uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

/// Common 12-byte tail of Microsoft KSDATAFORMAT_SUBTYPE_PCM and KSDATAFORMAT_SUBTYPE_IEEE_FLOAT GUIDs.
constexpr std::array<std::uint8_t, 12> WAVE_EXTENSIBLE_GUID_TAIL =
    {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

enum class AudioSampleFormat {
    PCM,
    IEEE_FLOAT
};

struct WavFormat {
    AudioSampleFormat Format = AudioSampleFormat::PCM;
    std::uint16_t Channels = 0;
    std::uint32_t SampleRate = 0;
    std::uint16_t BitsPerSample = 0;
};

std::string_view TrimCell(std::string_view text) {
    text = Trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = Trim(text);
    }
    return text;
}

std::optional<char> FindCsvDelimiter(const std::string_view line) {
    for (const char delimiter : {',', ';', '\t'}) {
        if (line.find(delimiter) != std::string_view::npos) {
            return delimiter;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> SplitCsvColumns(const std::string_view line) {
    std::vector<std::string_view> columns;
    const std::optional<char> delimiter = FindCsvDelimiter(line);
    if (!delimiter) {
        std::size_t pos = 0;
        while (pos < line.size()) {
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
                ++pos;
            }
            if (pos >= line.size()) {
                break;
            }
            const std::size_t start = pos;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) == 0) {
                ++pos;
            }
            columns.push_back(TrimCell(line.substr(start, pos - start)));
        }
        return columns;
    }

    std::size_t start = 0;
    bool in_quotes = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"') {
            in_quotes = !in_quotes;
        } else if (!in_quotes && line[index] == *delimiter) {
            columns.push_back(TrimCell(line.substr(start, index - start)));
            start = index + 1;
        }
    }
    columns.push_back(TrimCell(line.substr(start)));
    return columns;
}

std::optional<PWLPoint> ParseCsvRow(const std::string_view line) {
    const std::vector<std::string_view> columns = SplitCsvColumns(line);
    if (columns.size() < 2) {
        return std::nullopt;
    }
    const std::optional<double> time = ParseValue(columns[0], "");
    const std::optional<double> value = ParseValue(columns[1], "");
    if (!time || !value || !std::isfinite(*time) || !std::isfinite(*value)) {
        return std::nullopt;
    }
    return PWLPoint{.Time = *time, .Value = *value};
}

std::uint16_t ReadU16LE(const std::span<const std::byte> data, const std::size_t offset) {
    const auto b0 = std::to_integer<std::uint16_t>(data[offset]);
    const auto b1 = std::to_integer<std::uint16_t>(data[offset + 1]);
    return static_cast<std::uint16_t>(b0 | (b1 << 8U));
}

std::uint32_t ReadU32LE(const std::span<const std::byte> data, const std::size_t offset) {
    const auto b0 = std::to_integer<std::uint32_t>(data[offset]);
    const auto b1 = std::to_integer<std::uint32_t>(data[offset + 1]);
    const auto b2 = std::to_integer<std::uint32_t>(data[offset + 2]);
    const auto b3 = std::to_integer<std::uint32_t>(data[offset + 3]);
    return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
}

std::uint64_t ReadU64LE(const std::span<const std::byte> data, const std::size_t offset) {
    const std::uint64_t lo = ReadU32LE(data, offset);
    const std::uint64_t hi = ReadU32LE(data, offset + 4);
    return lo | (hi << 32U);
}

std::string_view FourCC(const std::span<const std::byte> data, const std::size_t offset) {
    return {reinterpret_cast<const char *>(data.data() + offset), 4};
}

std::expected<WavFormat, std::string> ParseFmtChunk(const std::span<const std::byte> chunk) {
    if (chunk.size() < FMT_MIN_SIZE) {
        return std::unexpected("The WAV fmt chunk is truncated");
    }
    std::uint16_t format_code = ReadU16LE(chunk, 0);
    const std::uint16_t channels = ReadU16LE(chunk, 2);
    const std::uint32_t sample_rate = ReadU32LE(chunk, 4);
    const std::uint16_t bits_per_sample = ReadU16LE(chunk, 14);

    if (format_code == WAVE_FORMAT_EXTENSIBLE) {
        if (chunk.size() < FMT_EXTENSIBLE_SIZE) {
            return std::unexpected("The extensible WAV fmt chunk is truncated");
        }
        for (std::size_t index = 0; index < WAVE_EXTENSIBLE_GUID_TAIL.size(); ++index) {
            if (std::to_integer<std::uint8_t>(chunk[28 + index]) != WAVE_EXTENSIBLE_GUID_TAIL[index]) {
                return std::unexpected("Unsupported WAV extensible subformat");
            }
        }
        const std::uint32_t subformat = ReadU32LE(chunk, 24);
        if (subformat != WAVE_FORMAT_PCM && subformat != WAVE_FORMAT_IEEE_FLOAT) {
            return std::unexpected("Only uncompressed PCM or IEEE float WAV files are supported");
        }
        format_code = static_cast<std::uint16_t>(subformat);
    }

    AudioSampleFormat sample_format = AudioSampleFormat::PCM;
    if (format_code == WAVE_FORMAT_PCM) {
        if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32) {
            return std::unexpected(std::format("Unsupported PCM bit depth: {} bits", bits_per_sample));
        }
    } else if (format_code == WAVE_FORMAT_IEEE_FLOAT) {
        sample_format = AudioSampleFormat::IEEE_FLOAT;
        if (bits_per_sample != 32 && bits_per_sample != 64) {
            return std::unexpected(std::format("Unsupported IEEE float bit depth: {} bits", bits_per_sample));
        }
    } else {
        return std::unexpected("Only uncompressed PCM or IEEE float WAV files are supported");
    }

    if (channels == 0 || sample_rate == 0) {
        return std::unexpected("The WAV file has an invalid channel count or sample rate");
    }

    return WavFormat{
        .Format = sample_format,
        .Channels = channels,
        .SampleRate = sample_rate,
        .BitsPerSample = bits_per_sample,
    };
}

std::expected<double, std::string>
DecodeSample(const std::span<const std::byte> data, const std::size_t offset, const WavFormat &format) {
    if (format.Format == AudioSampleFormat::PCM) {
        switch (format.BitsPerSample) {
        case 8: {
            const auto sample = std::to_integer<std::uint8_t>(data[offset]);
            return (static_cast<double>(sample) - 128.0) / 128.0;
        }
        case 16: {
            const auto sample = static_cast<std::int16_t>(ReadU16LE(data, offset));
            return static_cast<double>(sample) / 32768.0;
        }
        case 24: {
            const auto b0 = std::to_integer<std::uint32_t>(data[offset]);
            const auto b1 = std::to_integer<std::uint32_t>(data[offset + 1]);
            const auto b2 = std::to_integer<std::uint32_t>(data[offset + 2]);
            std::uint32_t raw = b0 | (b1 << 8U) | (b2 << 16U);
            if ((raw & 0x800000U) != 0U) {
                raw |= 0xFF000000U;
            }
            return static_cast<double>(static_cast<std::int32_t>(raw)) / 8388608.0;
        }
        case 32: {
            const auto sample = static_cast<std::int32_t>(ReadU32LE(data, offset));
            return static_cast<double>(sample) / 2147483648.0;
        }
        default:
            return std::unexpected("Unsupported PCM bit depth");
        }
    }

    if (format.BitsPerSample == 32) {
        const float sample = std::bit_cast<float>(ReadU32LE(data, offset));
        if (!std::isfinite(sample)) {
            return std::unexpected("The WAV file contains a non-finite sample");
        }
        return static_cast<double>(sample);
    }
    if (format.BitsPerSample == 64) {
        const double sample = std::bit_cast<double>(ReadU64LE(data, offset));
        if (!std::isfinite(sample)) {
            return std::unexpected("The WAV file contains a non-finite sample");
        }
        return sample;
    }
    return std::unexpected("Unsupported IEEE float bit depth");
}

std::expected<std::vector<std::byte>, std::string> ReadBinaryFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(std::format("Could not open '{}'", path.string()));
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected(std::format("Could not read '{}'", path.string()));
    }
    std::vector<std::byte> buffer(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char *>(buffer.data()), size)) {
        return std::unexpected(std::format("Could not read '{}'", path.string()));
    }
    return buffer;
}

std::string LowerExtension(const std::filesystem::path &path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

} // namespace

/**
 * @brief   Parses time/value pairs from CSV or whitespace-delimited text into PWL points.
 * @details Skips empty lines, `#` or `//` comments, UTF-8 BOM, and at most one non-numeric header row. Supports
 *          comma, semicolon, tab, or space separators and SPICE scale suffixes on either column.
 * @param[in] text  CSV or table text to parse.
 * @return  The validated PWL points, or an error describing the line or constraint that failed.
 */
std::expected<std::vector<PWLPoint>, std::string> ParsePWLCSV(std::string_view text) {
    if (text.starts_with(UTF8_BOM)) {
        text.remove_prefix(UTF8_BOM.size());
    }

    std::vector<PWLPoint> points;
    bool skipped_header = false;
    std::size_t line_number = 0;
    std::size_t pos = 0;

    while (pos <= text.size()) {
        const std::size_t end = text.find('\n', pos);
        std::string_view line = end == std::string_view::npos ? text.substr(pos) : text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        ++line_number;

        const std::string_view trimmed = Trim(line);
        if (!trimmed.empty() && !trimmed.starts_with('#') && !trimmed.starts_with("//")) {
            const std::optional<PWLPoint> point = ParseCsvRow(trimmed);
            if (!point) {
                if (points.empty() && !skipped_header) {
                    skipped_header = true;
                } else {
                    return std::unexpected(std::format("Invalid time/value pair on line {}", line_number));
                }
            } else {
                if (point->Time < 0.0) {
                    return std::unexpected(std::format("Negative time on line {}", line_number));
                }
                if (!points.empty() && point->Time <= points.back().Time) {
                    return std::unexpected(std::format("Times must strictly increase (line {})", line_number));
                }
                points.push_back(*point);
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        pos = end + 1;
    }

    if (points.size() < 2) {
        return std::unexpected("The CSV file needs at least two time/value points");
    }
    return points;
}

/**
 * @brief   Decodes an uncompressed PCM or IEEE-float WAV byte buffer into PWL points.
 * @details Multi-channel audio is averaged to mono, normalized to `[-1, 1]`, and scaled by `peak_amplitude`.
 * @param[in] bytes           Raw bytes of the RIFF/WAVE file.
 * @param[in] peak_amplitude  Full-scale amplitude multiplying the normalized samples.
 * @return  The decoded PWL points starting at `t = 0`, or an error if the WAV is malformed or unsupported.
 */
std::expected<std::vector<PWLPoint>, std::string> ParsePWLWAV(const std::span<const std::byte> bytes,
                                                              const double peak_amplitude) {
    if (!std::isfinite(peak_amplitude) || peak_amplitude <= 0.0) {
        return std::unexpected("The WAV peak amplitude must be positive");
    }
    if (bytes.size() < RIFF_HEADER_SIZE || FourCC(bytes, 0) != "RIFF" || FourCC(bytes, 8) != "WAVE") {
        return std::unexpected("Not a valid RIFF/WAVE file");
    }

    std::optional<WavFormat> format;
    std::optional<std::span<const std::byte>> data_chunk;
    std::size_t offset = RIFF_HEADER_SIZE;

    while (offset + CHUNK_HEADER_SIZE <= bytes.size()) {
        const std::string_view id = FourCC(bytes, offset);
        const std::uint32_t chunk_size = ReadU32LE(bytes, offset + 4);
        const std::size_t payload_offset = offset + CHUNK_HEADER_SIZE;
        if (chunk_size > bytes.size() - payload_offset) {
            return std::unexpected("The WAV file ends inside a chunk");
        }
        const std::span<const std::byte> payload = bytes.subspan(payload_offset, chunk_size);
        if (id == "fmt ") {
            const auto parsed = ParseFmtChunk(payload);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            format = *parsed;
        } else if (id == "data" && !data_chunk) {
            data_chunk = payload;
        }
        const std::size_t padding = (chunk_size % 2U != 0U) ? 1U : 0U;
        if (bytes.size() - payload_offset - chunk_size < padding) {
            break;
        }
        offset = payload_offset + chunk_size + padding;
    }

    if (!format) {
        return std::unexpected("The WAV file has no fmt chunk");
    }
    if (!data_chunk) {
        return std::unexpected("The WAV file has no data chunk");
    }

    const std::size_t bytes_per_sample = format->BitsPerSample / 8U;
    const std::size_t frame_bytes = bytes_per_sample * format->Channels;
    const std::size_t frames = data_chunk->size() / frame_bytes;
    if (frames < 2) {
        return std::unexpected("The WAV file needs at least two audio frames");
    }
    if (frames > MAX_WAV_SAMPLES) {
        return std::unexpected(
            std::format("The WAV file exceeds the {} sample limit ({} samples)", MAX_WAV_SAMPLES, frames));
    }

    std::vector<PWLPoint> points;
    points.reserve(frames);
    const double sample_period = 1.0 / static_cast<double>(format->SampleRate);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        double sum = 0.0;
        const std::size_t frame_offset = frame * frame_bytes;
        for (std::uint16_t ch = 0; ch < format->Channels; ++ch) {
            const auto decoded = DecodeSample(*data_chunk, frame_offset + ch * bytes_per_sample, *format);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            sum += *decoded;
        }
        const double averaged = sum / static_cast<double>(format->Channels);
        points.push_back({.Time = static_cast<double>(frame) * sample_period, .Value = averaged * peak_amplitude});
    }
    return points;
}

/**
 * @brief   Reads a CSV or whitespace-delimited file from disk into PWL points.
 * @param[in] path  Path to the CSV file.
 * @return  The parsed PWL points, or an error message.
 */
std::expected<std::vector<PWLPoint>, std::string> LoadPWLCSVFile(const std::filesystem::path &path) {
    const auto bytes = ReadBinaryFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const std::string_view text(reinterpret_cast<const char *>(bytes->data()), bytes->size());
    return ParsePWLCSV(text);
}

/**
 * @brief   Reads an uncompressed WAV audio file from disk into PWL points.
 * @param[in] path            Path to the WAV file.
 * @param[in] peak_amplitude  Full-scale amplitude multiplying the normalized samples.
 * @return  The decoded PWL points, or an error message.
 */
std::expected<std::vector<PWLPoint>, std::string> LoadPWLWAVFile(const std::filesystem::path &path,
                                                                 const double peak_amplitude) {
    const auto bytes = ReadBinaryFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return ParsePWLWAV(*bytes, peak_amplitude);
}

/**
 * @brief   Reads either a WAV file (by `.wav` extension) or a CSV/text table into PWL points.
 * @param[in] path                Path to the file.
 * @param[in] wav_peak_amplitude  Full-scale amplitude used when the file is a WAV recording.
 * @return  The imported PWL points, or an error message.
 */
std::expected<std::vector<PWLPoint>, std::string> LoadPWLFile(const std::filesystem::path &path,
                                                              const double wav_peak_amplitude) {
    if (LowerExtension(path) == ".wav") {
        return LoadPWLWAVFile(path, wav_peak_amplitude);
    }
    return LoadPWLCSVFile(path);
}

} // namespace Core
