/**
 * @file    line_geometry_tests.cpp
 * @brief   Tests for the electrical figures of a transmission line worked out from its geometry.
 */

#include "circuit/line_geometry.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <expected>
#include <numbers>
#include <string>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double SPEED_OF_LIGHT = 299792458.0;
/// Copper thin enough that the thickness correction of the width vanishes, as in the textbook examples.
constexpr double THIN_COPPER = 1e-9;
/// The surface resistance of copper at 1 GHz, \f$\sqrt{\pi f \mu_0 / \sigma}\f$.
constexpr double COPPER_SURFACE_RESISTANCE_AT_1GHZ = 8.2502e-3;

Core::LineElectrical Compute(const Core::LineGeometry &geometry) {
    const std::expected<Core::LineElectrical, std::string> electrical = Core::ComputeLineElectrical(geometry);
    REQUIRE(electrical.has_value());
    return *electrical;
}

} // namespace

TEST_CASE("A coax in air with a shield e times the inner diameter is 60 Ohm and runs at the speed of light",
          "[line_geometry]") {
    const Core::LineGeometry geometry{
        .CrossSection = Core::CoaxGeometry{.InnerDiameter = 1e-3, .OuterDiameter = std::numbers::e * 1e-3},
        .Materials = {.RelativePermittivity = 1.0, .LossTangent = 0.0, .Conductivity = 5.8e7},
        .Length = 1.0,
        .ReferenceFrequency = 1e9};
    const Core::LineElectrical line = Compute(geometry);
    // eta0 / (2 pi) ln(e)
    CHECK_THAT(line.CharacteristicImpedance, WithinRel(59.9585, 1e-4));
    CHECK_THAT(line.Parameters.Delay, WithinRel(1.0 / SPEED_OF_LIGHT, 1e-9));
}

TEST_CASE("A coax gets the DC resistance of its inner conductor and the skin resistance of both", "[line_geometry]") {
    const Core::LineGeometry geometry{
        .CrossSection = Core::CoaxGeometry{.InnerDiameter = 1e-3, .OuterDiameter = 3e-3},
        .Materials = {.RelativePermittivity = 2.25, .LossTangent = 4e-4, .Conductivity = 5.8e7},
        .Length = 1.0,
        .ReferenceFrequency = 1e9};
    const Core::LineElectrical line = Compute(geometry);
    // 1 m / (sigma pi a^2) with a = 0.5 mm, and Rs (1/a + 1/b) / (2 pi) with b = 1.5 mm
    CHECK_THAT(line.Parameters.DcResistance, WithinRel(1.0 / (5.8e7 * std::numbers::pi * 0.25e-6), 1e-9));
    CHECK_THAT(line.Parameters.SkinResistance,
               WithinRel(COPPER_SURFACE_RESISTANCE_AT_1GHZ * (2000.0 + 2000.0 / 3.0) / (2.0 * std::numbers::pi), 1e-4));
    // The field is all in the dielectric, so the wave sees its whole loss tangent and its permittivity
    CHECK(line.Parameters.LossTangent == 4e-4);
    CHECK_THAT(line.EffectivePermittivity, WithinAbs(2.25, 1e-12));
    CHECK_THAT(line.Parameters.Delay, WithinRel(1.5 / SPEED_OF_LIGHT, 1e-9));
    CHECK(line.Parameters.DcConductance == 0.0);
    CHECK(line.Parameters.ReferenceFrequency == 1e9);
}

TEST_CASE("A microstrip matches Pozar's 50 Ohm example, and the air above it dilutes its dielectric loss",
          "[line_geometry]") {
    // Pozar, Microwave Engineering, example 3.7: er = 2.20, d = 0.159 cm, W/d = 3.081 for 50 Ohm, eeff = 1.87
    const Core::LineGeometry geometry{
        .CrossSection = Core::MicrostripGeometry{.Width = 3.081 * 1.59e-3, .Height = 1.59e-3, .Thickness = THIN_COPPER},
        .Materials = {.RelativePermittivity = 2.2, .LossTangent = 1e-3, .Conductivity = 5.8e7},
        .Length = 0.1,
        .ReferenceFrequency = 1e9};
    const Core::LineElectrical line = Compute(geometry);
    CHECK_THAT(line.CharacteristicImpedance, WithinRel(50.0, 5e-3));
    CHECK_THAT(line.EffectivePermittivity, WithinRel(1.87, 5e-3));
    CHECK_THAT(line.Parameters.Delay, WithinRel(0.1 * std::sqrt(line.EffectivePermittivity) / SPEED_OF_LIGHT, 1e-9));
    CHECK(line.Parameters.LossTangent > 0.5e-3);
    CHECK(line.Parameters.LossTangent < 1e-3);
}

TEST_CASE("Thicker copper widens a microstrip, which lowers its impedance", "[line_geometry]") {
    Core::LineGeometry geometry;
    const double thin = Compute(geometry).CharacteristicImpedance;
    std::get<Core::MicrostripGeometry>(geometry.CrossSection).Thickness = 70e-6;
    CHECK(Compute(geometry).CharacteristicImpedance < thin);
}

TEST_CASE("A stripline matches Pozar's 50 Ohm example", "[line_geometry]") {
    // Pozar, Microwave Engineering, example 3.5: er = 2.20, b = 0.32 cm, W = 0.266 cm for 50 Ohm
    const Core::LineGeometry geometry{
        .CrossSection = Core::StriplineGeometry{.Width = 2.66e-3, .PlaneSpacing = 3.2e-3, .Thickness = 35e-6},
        .Materials = {.RelativePermittivity = 2.2, .LossTangent = 1e-3, .Conductivity = 5.8e7},
        .Length = 0.1,
        .ReferenceFrequency = 1e9};
    const Core::LineElectrical line = Compute(geometry);
    CHECK_THAT(line.CharacteristicImpedance, WithinRel(50.0, 5e-3));
    CHECK(line.Parameters.LossTangent == 1e-3);
    CHECK_THAT(line.Parameters.SkinResistance, WithinRel(0.1 * COPPER_SURFACE_RESISTANCE_AT_1GHZ / 2.66e-3, 1e-4));
    CHECK_THAT(line.Parameters.DcResistance, WithinRel(0.1 / (5.8e7 * 2.66e-3 * 35e-6), 1e-9));
}

TEST_CASE("The figures of a default microstrip make a valid TransmissionLine", "[line_geometry]") {
    const Core::LineElectrical line = Compute({});
    CHECK(line.CharacteristicImpedance > 0.0);
    CHECK(Core::TransmissionLine::IsValidParameters(line.Parameters));
}

TEST_CASE("A geometry that is not a line says why", "[line_geometry]") {
    const auto error_of = [](const Core::LineGeometry &geometry) {
        const std::expected<Core::LineElectrical, std::string> electrical = Core::ComputeLineElectrical(geometry);
        REQUIRE_FALSE(electrical.has_value());
        return electrical.error();
    };
    CHECK_THAT(error_of({.CrossSection = Core::CoaxGeometry{.InnerDiameter = 2e-3, .OuterDiameter = 1e-3}}),
               ContainsSubstring("shield diameter"));
    CHECK_THAT(
        error_of({.CrossSection = Core::StriplineGeometry{.Width = 1e-3, .PlaneSpacing = 1e-3, .Thickness = 2e-3}}),
        ContainsSubstring("thinner"));
    CHECK_THAT(error_of({.CrossSection = Core::MicrostripGeometry{.Width = 0.0}}), ContainsSubstring("above zero"));
    CHECK_THAT(error_of({.Materials = {.RelativePermittivity = 0.5}}), ContainsSubstring("permittivity"));
    CHECK_THAT(error_of({.Materials = {.Conductivity = 0.0}}), ContainsSubstring("conductivity"));
    CHECK_THAT(error_of({.Length = 0.0}), ContainsSubstring("length"));
}
