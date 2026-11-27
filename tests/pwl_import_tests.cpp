/**
 * @file    pwl_import_tests.cpp
 * @brief   Tests for CSV and WAV PWL source import and end-to-end Nodalys transient simulation.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "backends/nodalys/nodalys_backend.h"
#include "circuit/circuit.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"
#include "circuit/pwl_import.h"
#include "simulation/simulation_backend.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

void PushU16LE(std::vector<std::byte> &out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

void PushU32LE(std::vector<std::byte> &out, const std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
}

void PushU64LE(std::vector<std::byte> &out, const std::uint64_t value) {
    PushU32LE(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
    PushU32LE(out, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFU));
}

void PushFourCC(std::vector<std::byte> &out, const char (&id)[5]) {
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::byte>(id[index]));
    }
}

void PushChunk(std::vector<std::byte> &out, const char (&id)[5], const std::vector<std::byte> &payload) {
    PushFourCC(out, id);
    PushU32LE(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    if (payload.size() % 2U != 0U) {
        out.push_back(std::byte{0});
    }
}

std::vector<std::byte> BuildWav(const std::uint16_t format_code,
                                const std::uint16_t channels,
                                const std::uint32_t sample_rate,
                                const std::uint16_t bits_per_sample,
                                const std::vector<std::byte> &data_payload,
                                const bool include_odd_junk = false) {
    std::vector<std::byte> fmt;
    PushU16LE(fmt, format_code);
    PushU16LE(fmt, channels);
    PushU32LE(fmt, sample_rate);
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * (bits_per_sample / 8U));
    PushU32LE(fmt, sample_rate * block_align);
    PushU16LE(fmt, block_align);
    PushU16LE(fmt, bits_per_sample);

    std::vector<std::byte> body;
    PushFourCC(body, "WAVE");
    if (include_odd_junk) {
        // Odd chunk sizes in RIFF carry a padding byte that the parser must skip
        PushChunk(body, "JUNK", {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}});
    }
    PushChunk(body, "fmt ", fmt);
    PushChunk(body, "data", data_payload);

    std::vector<std::byte> wav;
    PushFourCC(wav, "RIFF");
    PushU32LE(wav, static_cast<std::uint32_t>(body.size()));
    wav.insert(wav.end(), body.begin(), body.end());
    return wav;
}

/**
 * @class   TempFile
 * @brief   Path in the system temporary folder that removes its file when the scope ends.
 */
class TempFile {
public:
    explicit TempFile(const std::string &filename) : m_Path(std::filesystem::temp_directory_path() / filename) {}

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(m_Path, error);
    }

    const std::filesystem::path &Path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

} // namespace

TEST_CASE("ParsePWLCSV reads headers, SPICE suffixes, and varied delimiters", "[pwl_import]") {
    const std::string csv = "\xEF\xBB\xBF"
                            "# captured waveform\n"
                            "\"Time (s)\",\"V(1) (V)\",\"I(R1) (A)\"\n"
                            "0, 0, 10\n"
                            "1m; 2.5; 20\n"
                            "2m\t-1.25\n"
                            "3m 0\n";

    const auto points = Core::ParsePWLCSV(csv);
    REQUIRE(points.has_value());
    REQUIRE(points->size() == 4);
    CHECK_THAT(points->at(0).Time, WithinAbs(0.0, 1e-12));
    CHECK_THAT(points->at(0).Value, WithinAbs(0.0, 1e-12));
    CHECK_THAT(points->at(1).Time, WithinAbs(1e-3, 1e-12));
    CHECK_THAT(points->at(1).Value, WithinAbs(2.5, 1e-12));
    CHECK_THAT(points->at(2).Time, WithinAbs(2e-3, 1e-12));
    CHECK_THAT(points->at(2).Value, WithinAbs(-1.25, 1e-12));
    CHECK_THAT(points->at(3).Time, WithinAbs(3e-3, 1e-12));
    CHECK_THAT(points->at(3).Value, WithinAbs(0.0, 1e-12));
}

TEST_CASE("ParsePWLCSV rejects malformed rows, negative times, and non-increasing times", "[pwl_import]") {
    CHECK_FALSE(Core::ParsePWLCSV("").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("0, 1\n").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("-1m, 0\n1m, 5\n").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("0, 0\n0, 5\n").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("2m, 0\n1m, 5\n").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("0, 0\nbad, 5\n").has_value());
    CHECK_FALSE(Core::ParsePWLCSV("0, 0\n1m\n").has_value());
}

TEST_CASE("ParsePWLWAV decodes 16-bit stereo PCM, skips odd chunks, and scales peak amplitude", "[pwl_import]") {
    // 3 stereo frames at 1000 Hz: (+0.5, -0.5) -> 0, (+0.5, +0.5) -> +0.5, (-1.0, -1.0) -> -1.0
    std::vector<std::byte> pcm;
    const std::array<std::int16_t, 6> samples = {16'384, -16'384, 16'384, 16'384, -32'768, -32'768};
    for (const std::int16_t sample : samples) {
        PushU16LE(pcm, static_cast<std::uint16_t>(sample));
    }
    const std::vector<std::byte> wav = BuildWav(1, 2, 1000, 16, pcm, true);

    const auto points = Core::ParsePWLWAV(wav, 2.0);
    REQUIRE(points.has_value());
    REQUIRE(points->size() == 3);
    CHECK_THAT(points->at(0).Time, WithinAbs(0.0, 1e-12));
    CHECK_THAT(points->at(0).Value, WithinAbs(0.0, 1e-9));
    CHECK_THAT(points->at(1).Time, WithinAbs(1e-3, 1e-12));
    CHECK_THAT(points->at(1).Value, WithinAbs(1.0, 1e-9));
    CHECK_THAT(points->at(2).Time, WithinAbs(2e-3, 1e-12));
    CHECK_THAT(points->at(2).Value, WithinAbs(-2.0, 1e-9));
}

TEST_CASE("ParsePWLWAV decodes 8-bit, 24-bit PCM, and 32/64-bit IEEE float WAVs", "[pwl_import]") {
    SECTION("8-bit unsigned PCM") {
        const std::vector<std::byte> data = {std::byte{128}, std::byte{0}};
        const auto points = Core::ParsePWLWAV(BuildWav(1, 1, 100, 8, data), 1.0);
        REQUIRE(points.has_value());
        CHECK_THAT(points->at(0).Value, WithinAbs(0.0, 1e-9));
        CHECK_THAT(points->at(1).Value, WithinAbs(-1.0, 1e-9));
    }

    SECTION("24-bit signed PCM") {
        // +0.5 (0x400000) and -0.5 (0xC00000)
        const std::vector<std::byte> data =
            {std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0xC0}};
        const auto points = Core::ParsePWLWAV(BuildWav(1, 1, 200, 24, data), 4.0);
        REQUIRE(points.has_value());
        CHECK_THAT(points->at(0).Value, WithinAbs(2.0, 1e-9));
        CHECK_THAT(points->at(1).Value, WithinAbs(-2.0, 1e-9));
    }

    SECTION("32-bit IEEE float") {
        std::vector<std::byte> data;
        PushU32LE(data, std::bit_cast<std::uint32_t>(0.25f));
        PushU32LE(data, std::bit_cast<std::uint32_t>(-0.75f));
        const auto points = Core::ParsePWLWAV(BuildWav(3, 1, 500, 32, data), 2.0);
        REQUIRE(points.has_value());
        CHECK_THAT(points->at(0).Value, WithinAbs(0.5, 1e-6));
        CHECK_THAT(points->at(1).Value, WithinAbs(-1.5, 1e-6));
    }

    SECTION("64-bit IEEE float") {
        std::vector<std::byte> data;
        PushU64LE(data, std::bit_cast<std::uint64_t>(0.5));
        PushU64LE(data, std::bit_cast<std::uint64_t>(-0.25));
        const auto points = Core::ParsePWLWAV(BuildWav(3, 1, 500, 64, data), 2.0);
        REQUIRE(points.has_value());
        CHECK_THAT(points->at(0).Value, WithinAbs(1.0, 1e-12));
        CHECK_THAT(points->at(1).Value, WithinAbs(-0.5, 1e-12));
    }
}

TEST_CASE("ParsePWLWAV rejects malformed headers, unsupported formats, and bad amplitudes", "[pwl_import]") {
    const std::vector<std::byte> two_samples = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    CHECK_FALSE(Core::ParsePWLWAV({}, 1.0).has_value());
    CHECK_FALSE(Core::ParsePWLWAV(BuildWav(1, 1, 1000, 16, two_samples), 0.0).has_value());
    CHECK_FALSE(Core::ParsePWLWAV(BuildWav(2, 1, 1000, 16, two_samples), 1.0).has_value());
    CHECK_FALSE(Core::ParsePWLWAV(BuildWav(1, 1, 1000, 12, two_samples), 1.0).has_value());
    CHECK_FALSE(Core::ParsePWLWAV(BuildWav(1, 0, 1000, 16, two_samples), 1.0).has_value());
    CHECK_FALSE(Core::ParsePWLWAV(BuildWav(1, 1, 1000, 16, {std::byte{0}, std::byte{0}}), 1.0).has_value());
}

TEST_CASE("LoadPWLFile dispatches CSV and WAV files from disk", "[pwl_import]") {
    const TempFile csv_file("imcsim_test_pwl.csv");
    {
        std::ofstream out(csv_file.Path());
        out << "t,v\n0,0\n1m,3.3\n";
    }
    const auto csv_points = Core::LoadPWLFile(csv_file.Path(), 1.0);
    REQUIRE(csv_points.has_value());
    REQUIRE(csv_points->size() == 2);
    CHECK_THAT(csv_points->at(1).Value, WithinAbs(3.3, 1e-9));

    const TempFile wav_file("imcsim_test_pwl.WAV");
    {
        std::vector<std::byte> pcm;
        PushU16LE(pcm, 0);
        PushU16LE(pcm, 16'384);
        const std::vector<std::byte> wav = BuildWav(1, 1, 1000, 16, pcm);
        std::ofstream out(wav_file.Path(), std::ios::binary);
        out.write(reinterpret_cast<const char *>(wav.data()), static_cast<std::streamsize>(wav.size()));
    }
    const auto wav_points = Core::LoadPWLFile(wav_file.Path(), 5.0);
    REQUIRE(wav_points.has_value());
    REQUIRE(wav_points->size() == 2);
    CHECK_THAT(wav_points->at(1).Value, WithinAbs(2.5, 1e-9));

    CHECK_FALSE(Core::LoadPWLFile("does_not_exist_12345.csv", 1.0).has_value());
}

TEST_CASE("Nodalys transient simulates a circuit driven by a decoded WAV source", "[pwl_import][nodalys]") {
    // 10 kHz, 1 kHz cosine at 3 V peak across a 1:2 voltage divider -> output is half the input
    constexpr std::uint32_t SAMPLE_RATE = 10'000;
    constexpr std::size_t SAMPLES = 21;
    std::vector<std::byte> pcm;
    for (std::size_t index = 0; index < SAMPLES; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(SAMPLE_RATE);
        const double normalized = 0.5 * std::cos(2.0 * std::numbers::pi * 1000.0 * t);
        const auto sample = static_cast<std::int16_t>(std::lround(normalized * 32768.0));
        PushU16LE(pcm, static_cast<std::uint16_t>(sample));
    }
    const auto points = Core::ParsePWLWAV(BuildWav(1, 1, SAMPLE_RATE, 16, pcm), 3.0);
    REQUIRE(points.has_value());

    Core::VoltageSource source;
    source.SetName("V1");
    source.SetSourceType(Core::Source::SourceType::PWL);
    source.SetPWL({.Points = *points});

    Core::Resistor top;
    top.SetName("R1");
    top.SetValue(1000.0);
    Core::Resistor bottom;
    bottom.SetName("R2");
    bottom.SetValue(1000.0);
    Core::Ground ground;

    Core::Circuit circuit;
    circuit.Add(source, {1, 0});
    circuit.Add(top, {1, 2});
    circuit.Add(bottom, {2, 0});
    circuit.Add(ground, {0});

    Core::NodalysBackend backend;
    const Core::TransientRun run =
        backend.RunTransient(circuit, {.StopTime = 1e-3, .TimeStep = 5e-5}, std::nullopt, {});
    REQUIRE(run.Result.has_value());
    REQUIRE(run.Result->NodeVoltages.size() > 2);
    REQUIRE_FALSE(run.Result->NodeVoltages[2].empty());
    // At t = 0 and t = 1 ms (one full 1 kHz cycle), cos = +1 -> V(2) = +0.75 V; at t = 0.5 ms, cos = -1 -> -0.75 V
    CHECK_THAT(run.Result->NodeVoltages[2].front(), WithinAbs(0.75, 1e-3));
    CHECK_THAT(run.Result->NodeVoltages[2].back(), WithinAbs(0.75, 1e-3));
    const double min_voltage = *std::ranges::min_element(run.Result->NodeVoltages[2]);
    CHECK_THAT(min_voltage, WithinAbs(-0.75, 1e-3));
}
