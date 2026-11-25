/**
 * @file    json_exporter_tests.cpp
 * @brief   Tests of the JSON files the exporter writes: the shared parts of the documents and the escaping of names.
 */

#include <catch2/catch_test_macros.hpp>

#include <nodalys/nodalys.h>

#include <string>

TEST_CASE("The JSON of a pole-zero result lists the poles and the zeros with their frequencies in hertz",
          "[json_export]") {
    Nodalys::PoleZeroResult result;
    // A pole at -2 pi rad/s sits at 1 Hz
    result.Poles = {{-2.0 * 3.14159265358979323846, 0.0}};

    const std::string expected = "{\n  \"analysis\": \"pole_zero\",\n"
                                 "  \"poles\": [{\"real\": -6.2831853, \"imag\": 0, \"frequency_hz\": 1}],\n"
                                 "  \"zeros\": []\n}\n";
    CHECK(Nodalys::ExportJson(result) == expected);
}

TEST_CASE("The JSON of a transient writes one series for each node but ground", "[json_export]") {
    Nodalys::TransientResult result;
    result.Times = {0.0, 1.0};
    result.NodeVoltages = {{0.0, 0.0}, {1.5, 2.5}, {3.0, 4.0}};
    result.Currents = {{"i1", {0.1, 0.2}}};

    const std::string expected = "{\n  \"analysis\": \"transient\",\n"
                                 "  \"points\": 2,\n"
                                 "  \"times\": [0, 1],\n"
                                 "  \"node_voltages\": {\n"
                                 "    \"1\": [1.5, 2.5],\n"
                                 "    \"2\": [3, 4]\n"
                                 "  },\n"
                                 "  \"currents\": {\n"
                                 "    \"i1\": [0.1, 0.2]\n"
                                 "  }\n}\n";
    CHECK(Nodalys::ExportJson(result) == expected);
}

TEST_CASE("The JSON of a harmonic balance escapes the names of its currents and its strategy", "[json_export]") {
    Nodalys::HarmonicBalanceResult result;
    result.Strategy = "a\"b";
    result.CurrentPhasors = {{"V\\1", {{1.0, 0.0}}}};

    const std::string text = Nodalys::ExportJson(result);
    CHECK(text.find("\"strategy\": \"a\\\"b\"") != std::string::npos);
    CHECK(text.find("\"V\\\\1\": [{\"re\": 1, \"im\": 0, \"mag\": 1, \"phase_deg\": 0}]") != std::string::npos);
}
