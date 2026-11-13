/**
 * @file    sens_analysis_tests.cpp
 * @brief   Tests for small-signal DC sensitivity analysis (.sens) and adjoint solver.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("DC sensitivity analysis computes exact closed-form sensitivities of a voltage divider",
          "[nodalys][sensitivity]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 10.0);
    builder.AddResistor("R1", 1, 2, 1000.0);
    builder.AddResistor("R2", 2, 0, 2000.0);
    builder.AddCapacitor("C1", 2, 0, 1e-6);

    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    const Nodalys::SensitivityConfig config{.OutputPositiveNode = 2, .OutputNegativeNode = 0};
    const auto result = Nodalys::ComputeSensitivity(*circuit, config);

    REQUIRE(result.has_value());
    CHECK(result->OutputPositiveNode == 2);
    CHECK(result->OutputNegativeNode == 0);

    const auto &vecs = result->Vectors;

    // Output voltage V2 = 10 * 2000 / 3000 = 20/3 V
    // dV2/dR1 = -10 * 2000 / 3000^2 = -20000 / 9e6 = -2.222222e-3 V/Ohm
    // scale(R1) = R1 * dV2/dR1 = -20/9 V
    REQUIRE(vecs.contains("r1"));
    REQUIRE(vecs.contains("r1_scale"));
    CHECK_THAT(vecs.at("r1"), WithinRel(-20000.0 / 9e6, 1e-5));
    CHECK_THAT(vecs.at("r1_scale"), WithinRel(-20.0 / 9.0, 1e-5));

    // dV2/dR2 = 10 * 1000 / 3000^2 = 10000 / 9e6 = +1.111111e-3 V/Ohm
    // scale(R2) = R2 * dV2/dR2 = +20/9 V
    REQUIRE(vecs.contains("r2"));
    REQUIRE(vecs.contains("r2_scale"));
    CHECK_THAT(vecs.at("r2"), WithinRel(10000.0 / 9e6, 1e-5));
    CHECK_THAT(vecs.at("r2_scale"), WithinRel(20.0 / 9.0, 1e-5));

    // dV2/dV1 = 2000 / 3000 = 2/3 V/V
    // scale(V1) = 10 * 2/3 = 20/3 V
    REQUIRE(vecs.contains("v1"));
    REQUIRE(vecs.contains("v1_scale"));
    CHECK_THAT(vecs.at("v1"), WithinRel(2.0 / 3.0, 1e-5));
    CHECK_THAT(vecs.at("v1_scale"), WithinRel(20.0 / 3.0, 1e-5));

    // Capacitors have zero DC sensitivity
    REQUIRE(vecs.contains("c1"));
    REQUIRE(vecs.contains("c1_scale"));
    CHECK_THAT(vecs.at("c1"), WithinAbs(0.0, 1e-12));
    CHECK_THAT(vecs.at("c1_scale"), WithinAbs(0.0, 1e-12));
}

TEST_CASE("DC sensitivity analysis evaluates sensitivities of controlled sources", "[nodalys][sensitivity]") {
    SECTION("Voltage-Controlled Voltage Source (VCVS)") {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("V1", 1, 0, 2.0);
        builder.AddVcvs("E1", 2, 0, 1, 0, 3.0); // V2 = 3.0 * V1 = 6.0 V
        builder.AddResistor("R1", 2, 0, 1000.0);

        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        const auto result = Nodalys::ComputeSensitivity(*circuit, {.OutputPositiveNode = 2, .OutputNegativeNode = 0});

        REQUIRE(result.has_value());
        // dV2/dGain = V1 = 2.0 V/gain
        REQUIRE(result->Vectors.contains("e1_gain"));
        CHECK_THAT(result->Vectors.at("e1_gain"), WithinRel(2.0, 1e-5));
        // scale = Gain * dV2/dGain = 3.0 * 2.0 = 6.0 V
        REQUIRE(result->Vectors.contains("e1_scale"));
        CHECK_THAT(result->Vectors.at("e1_scale"), WithinRel(6.0, 1e-5));
    }

    SECTION("Voltage-Controlled Current Source (VCCS)") {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("V1", 1, 0, 2.0);
        builder.AddVccs("G1", 0, 2, 1, 0, 0.005); // Current into node 2 is 5 mA/V * 2 V = 10 mA
        builder.AddResistor("R1", 2, 0, 500.0);   // V2 = 10 mA * 500 Ohm = 5.0 V

        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        const auto result = Nodalys::ComputeSensitivity(*circuit, {.OutputPositiveNode = 2, .OutputNegativeNode = 0});

        REQUIRE(result.has_value());
        // V2 = Gm * V1 * R1 -> dV2/dGm = V1 * R1 = 2.0 * 500 = 1000.0 V/(A/V)
        REQUIRE(result->Vectors.contains("g1_gain"));
        CHECK_THAT(result->Vectors.at("g1_gain"), WithinRel(1000.0, 1e-5));
        // scale = Gm * dV2/dGm = 0.005 * 1000.0 = 5.0 V
        REQUIRE(result->Vectors.contains("g1_scale"));
        CHECK_THAT(result->Vectors.at("g1_scale"), WithinRel(5.0, 1e-5));
    }
}

TEST_CASE("SPICE netlist parser recognizes .sens directives and exports results", "[nodalys][sensitivity][parser]") {
    const std::string_view netlist = "* Sensitivity test netlist\n"
                                     "V1 1 0 5V\n"
                                     "R1 1 2 2k\n"
                                     "R2 2 0 3k\n"
                                     ".SENS V(2)\n"
                                     ".END\n";

    auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::SENSITIVITY);

    const auto &cfg = std::get<Nodalys::SensitivityConfig>(parsed->Analyses[0].Config);
    CHECK(cfg.OutputPositiveNode == 2);
    CHECK(cfg.OutputNegativeNode == 0);

    const auto result = Nodalys::ComputeSensitivity(parsed->CircuitInstance, cfg);
    REQUIRE(result.has_value());

    const std::string csv = Nodalys::ExportCsv(*result);
    CHECK(csv.contains("Vector,Value"));
    CHECK(csv.contains("r1,"));
    CHECK(csv.contains("r2,"));
    CHECK(csv.contains("v1,"));

    const std::string json = Nodalys::ExportJson(*result);
    CHECK(json.contains("\"analysis\": \"sensitivity\""));
    CHECK(json.contains("\"output_positive_node\": 2"));
    CHECK(json.contains("\"vectors\""));

    const std::string raw = Nodalys::ExportRaw(*result);
    CHECK(raw.contains("Plotname: DC Sensitivity Analysis"));
    CHECK(raw.contains("No. Variables:"));
}

TEST_CASE("DC sensitivity analysis validates output nodes", "[nodalys][sensitivity]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 5.0);
    builder.AddResistor("R1", 1, 0, 1000.0);
    auto circuit = builder.Build();
    REQUIRE(circuit.has_value());

    // Ground output node 0 referenced to 0
    CHECK_FALSE(Nodalys::ComputeSensitivity(*circuit, {.OutputPositiveNode = 0, .OutputNegativeNode = 0}).has_value());
    // Out of bounds node
    CHECK_FALSE(Nodalys::ComputeSensitivity(*circuit, {.OutputPositiveNode = 42, .OutputNegativeNode = 0}).has_value());
}
