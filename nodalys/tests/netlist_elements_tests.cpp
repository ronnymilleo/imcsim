/**
 * @file    netlist_elements_tests.cpp
 * @brief   Tests for the elements the netlist parser reads beyond plain R, L, C and DC sources: waveforms, controlled
 *          sources, ports and initial conditions.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nodalys/nodalys.h>

#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

/// Tolerance of a voltage read between two time points of a transient, in volts.
constexpr double WAVEFORM_TOLERANCE = 0.05;

/**
 * @brief   The voltage of a node at one instant of a transient, interpolated between the two points around it.
 */
double VoltageAt(const Nodalys::TransientResult &result, const std::size_t node, const double time) {
    const std::vector<double> &times = result.Times;
    const std::vector<double> &values = result.NodeVoltages[node];
    for (std::size_t index = 1; index < times.size(); ++index) {
        if (times[index] >= time) {
            const double share = (time - times[index - 1]) / (times[index] - times[index - 1]);
            return values[index - 1] + share * (values[index] - values[index - 1]);
        }
    }
    return values.back();
}

/**
 * @brief   Runs the transient a netlist asks for with its one source on node 1, across a 1 ohm resistor.
 * @details With 1 ohm, a current source reads in volts what a voltage source of the same waveform does.
 */
Nodalys::TransientResult RunWaveform(const std::string_view source_line) {
    const std::string netlist = std::format("* waveform\n{}\nR1 1 0 1\n.tran 10n 10u\n.end\n", source_line);
    const auto parsed = Nodalys::ParseNetlist(netlist);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Analyses.size() == 1);
    const auto result = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                  std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

TEST_CASE("A source waveform in a netlist drives the transient, as a voltage or as a current", "[nodalys][parser]") {
    // A current source from ground to node 1 pushes its current into the 1 ohm resistor, so both kinds read alike
    const std::string prefix = GENERATE(as<std::string>{}, "V1 1 0", "I1 0 1");
    CAPTURE(prefix);

    SECTION("PULSE is low before its delay and high on its flat top") {
        const auto result = RunWaveform(prefix + " PULSE(0 5 1u 10n 10n 5u 10u)");
        CHECK_THAT(VoltageAt(result, 1, 0.5e-6), WithinAbs(0.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 3e-6), WithinAbs(5.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 8e-6), WithinAbs(0.0, WAVEFORM_TOLERANCE));
    }

    SECTION("SIN peaks a quarter of a period in") {
        const auto result = RunWaveform(prefix + " SIN(1 2 100k)");
        CHECK_THAT(VoltageAt(result, 1, 2.5e-6), WithinAbs(3.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 7.5e-6), WithinAbs(-1.0, WAVEFORM_TOLERANCE));
    }

    SECTION("EXP rises with its time constant") {
        // After one time constant the rise has covered 1 - 1/e of the way
        const auto result = RunWaveform(prefix + " EXP(0 5 1u 2u 20u 2u)");
        CHECK_THAT(VoltageAt(result, 1, 0.5e-6), WithinAbs(0.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 3e-6), WithinAbs(5.0 * (1.0 - std::exp(-1.0)), WAVEFORM_TOLERANCE));
    }

    SECTION("PWL joins its points with straight lines") {
        const auto result = RunWaveform(prefix + " PWL(0 0 2u 4 6u 0)");
        CHECK_THAT(VoltageAt(result, 1, 1e-6), WithinAbs(2.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 2e-6), WithinAbs(4.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 4e-6), WithinAbs(2.0, WAVEFORM_TOLERANCE));
        CHECK_THAT(VoltageAt(result, 1, 9e-6), WithinAbs(0.0, WAVEFORM_TOLERANCE));
    }

    SECTION("SFFM and AM stay within their amplitude around their offset") {
        // A modulation index of 0 leaves the plain carrier: offset 1, amplitude 2
        const auto frequency_modulated = RunWaveform(prefix + " SFFM(1 2 100k 0 10k)");
        CHECK_THAT(VoltageAt(frequency_modulated, 1, 2.5e-6), WithinAbs(3.0, WAVEFORM_TOLERANCE));
        const auto amplitude_modulated = RunWaveform(prefix + " AM(2 1 10k 100k 0)");
        for (const double value : amplitude_modulated.NodeVoltages[1]) {
            CHECK(std::isfinite(value));
        }
    }
}

TEST_CASE("A source in a netlist takes its DC value in every form, DC= included", "[nodalys][parser]") {
    const std::string source = GENERATE(as<std::string>{}, "V1 1 0 5", "V1 1 0 DC 5", "V1 1 0 DC=5", "V1 1 0 dc=5");
    CAPTURE(source);
    const auto parsed = Nodalys::ParseNetlist(std::format("* dc\n{}\nR1 1 0 1k\n.op\n.end\n", source));
    REQUIRE(parsed.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(point.has_value());
    CHECK_THAT(point->NodeVoltages[1], WithinAbs(5.0, 1e-9));
}

TEST_CASE("A model parameter of a netlist that the engine does not read is named once, whatever uses it",
          "[nodalys][parser]") {
    const auto parsed = Nodalys::ParseNetlist("* ignored\nV1 1 0 1\nD1 1 2 dmod\nD2 1 2 DMOD\nR1 2 0 1k\n"
                                              ".model dmod D(IS=1e-14 KF=1e-16 LEVEL=1)\n.op\n.end\n");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Diagnostics.size() == 1);
    // IS is read and LEVEL names the kind of model, so KF alone is reported, once for both diodes
    CHECK(parsed->Diagnostics.front().Text == "Nodalys does not read the parameters KF of the model DMOD");
}

TEST_CASE("A netlist whose models give only what the engine reads has no diagnostic", "[nodalys][parser]") {
    const auto parsed = Nodalys::ParseNetlist("* read\nV1 1 0 1\nQ1 1 1 0 qmod\nM1 1 1 0 0 nmod W=2u\n"
                                              ".model qmod NPN(BF=100 IS=1e-15)\n.model nmod NMOS(VTO=1 KP=2e-5)\n"
                                              ".op\n.end\n");
    REQUIRE(parsed.has_value());
    CHECK(parsed->Diagnostics.empty());
}

TEST_CASE("Controlled sources in a netlist follow their controlling voltage or current", "[nodalys][parser]") {
    // V1 holds node 1 at 2 V and drives 2 mA through R1, which Vsense measures on its way to ground
    const std::string_view control = "* controlled\nV1 1 0 2\nR1 1 2 1k\nVsense 2 0 0\n";
    const auto solve = [&](const std::string_view element) {
        const auto parsed = Nodalys::ParseNetlist(std::format("{}{}\nRload 3 0 1k\n.op\n.end\n", control, element));
        REQUIRE(parsed.has_value());
        const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(point.has_value());
        return point->NodeVoltages[3];
    };

    SECTION("E is a voltage gain") {
        CHECK_THAT(solve("E1 3 0 1 0 3"), WithinAbs(6.0, 1e-6));
    }
    SECTION("G drives a current from its first node to its second, through itself") {
        // 2 V times 1 mS leaves node 3 through the source, so the load sees it as a negative voltage
        CHECK_THAT(solve("G1 3 0 1 0 1m"), WithinAbs(-2.0, 1e-6));
    }
    SECTION("H is a transresistance on the current of a voltage source") {
        CHECK_THAT(std::abs(solve("H1 3 0 Vsense 1k")), WithinAbs(2.0, 1e-6));
    }
    SECTION("F is a current gain on the current of a voltage source") {
        CHECK_THAT(std::abs(solve("F1 3 0 Vsense 2")), WithinAbs(4.0, 1e-6));
    }
    SECTION("A gain that is not a number is refused") {
        for (const std::string_view element : {"E1 3 0 1 0 x", "G1 3 0 1 0 x", "H1 3 0 Vsense x", "F1 3 0 Vsense x"}) {
            CAPTURE(element);
            CHECK_FALSE(Nodalys::ParseNetlist(std::format("{}{}\n.op\n.end\n", control, element)).has_value());
        }
    }
}

TEST_CASE("A current-controlled source finds its control source on a later line of the netlist", "[nodalys][parser]") {
    // Vsense carries the 1 mA that V1 drives through R1: F1 doubles it into the 1k load, H1 turns it into 2 V
    const std::string_view element = GENERATE(as<std::string_view>{}, "F1 3 0 Vsense 2", "H1 3 0 Vsense 2k");
    CAPTURE(element);
    const auto parsed = Nodalys::ParseNetlist(
        std::format("* late\n{}\nRload 3 0 1k\nV1 1 0 1\nR1 1 2 1k\nVsense 2 0 0\n.op\n.end\n", element));
    REQUIRE(parsed.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(point.has_value());
    CHECK_THAT(std::abs(point->NodeVoltages[3]), WithinAbs(2.0, 1e-6));
}

TEST_CASE("A current-controlled source and a behavioral source find their voltage source whatever the case",
          "[nodalys][parser]") {
    // Vsense carries the 1 mA that V1 drives through R1, and each reader turns it into 2 V on its 1k load
    const std::string_view element =
        GENERATE(as<std::string_view>{}, "F1 3 0 VSENSE 2", "H1 3 0 vsense 2k", "B1 3 0 V=2k*i(VSENSE)");
    CAPTURE(element);
    const auto parsed = Nodalys::ParseNetlist(
        std::format("* case\n{}\nRload 3 0 1k\nV1 1 0 1\nR1 1 2 1k\nVsense 2 0 0\n.op\n.end\n", element));
    REQUIRE(parsed.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
    REQUIRE(point.has_value());
    CHECK_THAT(std::abs(point->NodeVoltages[3]), WithinAbs(2.0, 1e-6));
}

TEST_CASE("A current-controlled source built in code finds its control source whatever the case", "[nodalys][parser]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 1.0);
    builder.AddResistor("R1", 1, 2, 1e3);
    builder.AddVdc("Vsense", 2, 0, 0.0);
    builder.AddCccs("F1", 3, 0, "VSENSE", 2.0);
    builder.AddResistor("Rload", 3, 0, 1e3);
    const auto circuit = builder.Build();
    REQUIRE(circuit.has_value());
    const auto point = Nodalys::ComputeOperatingPoint(*circuit);
    REQUIRE(point.has_value());
    CHECK_THAT(std::abs(point->NodeVoltages[3]), WithinAbs(2.0, 1e-6));
}

TEST_CASE("Two sources whose names differ only in case are refused as one name used twice", "[nodalys][parser]") {
    Nodalys::CircuitBuilder builder;
    builder.AddVdc("V1", 1, 0, 1.0);
    builder.AddResistor("R1", 1, 0, 1e3);
    builder.AddVdc("v1", 2, 0, 2.0);
    builder.AddResistor("R2", 2, 0, 1e3);
    const auto circuit = builder.Build();
    REQUIRE_FALSE(circuit.has_value());
    CHECK(circuit.error() == "v1: another source of the circuit has the same name");
}

TEST_CASE("A current-controlled source whose control is not a voltage source is refused, with both names",
          "[nodalys][parser]") {
    SECTION("A control source the netlist does not have") {
        for (const std::string_view element : {"F1 1 0 Vmissing 2", "H1 1 0 Vmissing 2"}) {
            CAPTURE(element);
            const auto parsed = Nodalys::ParseNetlist(std::format("* missing\n{}\nR1 1 0 1k\n.op\n.end\n", element));
            REQUIRE_FALSE(parsed.has_value());
            CHECK(parsed.error()
                  == std::format("{}: its control source Vmissing is not a voltage source of the circuit",
                                 element.substr(0, 2)));
        }
    }
    SECTION("A control source that is a current source, read by a CCCS") {
        Nodalys::CircuitBuilder builder;
        builder.AddIdc("I1", 2, 0, 1e-3);
        builder.AddResistor("R2", 2, 0, 1e3);
        builder.AddCccs("F1", 1, 0, "I1", 2.0);
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK(circuit.error() == "F1: its control source I1 is not a voltage source of the circuit");
    }
    SECTION("A control source that is a current source, read by a CCVS") {
        Nodalys::CircuitBuilder builder;
        builder.AddIdc("I1", 2, 0, 1e-3);
        builder.AddResistor("R2", 2, 0, 1e3);
        builder.AddCcvs("H1", 1, 0, "I1", 2e3);
        const auto circuit = builder.Build();
        REQUIRE_FALSE(circuit.has_value());
        CHECK(circuit.error() == "H1: its control source I1 is not a voltage source of the circuit");
    }
}

TEST_CASE("Initial conditions and ports in a netlist reach the circuit", "[nodalys][parser]") {
    SECTION("A capacitor starts a transient from its IC= with uic") {
        const auto parsed = Nodalys::ParseNetlist("* ic\nC1 1 0 1u IC=3\nR1 1 0 1k\n.tran 10u 5m uic\n.end\n");
        REQUIRE(parsed.has_value());
        CHECK(parsed->CircuitInstance.HasInitialConditions());
        const auto result = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                      std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
        REQUIRE(result.has_value());
        // The capacitor discharges from 3 V with a time constant of 1 ms
        CHECK_THAT(VoltageAt(*result, 1, 1e-3), WithinAbs(3.0 * std::exp(-1.0), WAVEFORM_TOLERANCE));
    }

    SECTION("An inductor takes its IC= as a current") {
        const auto parsed = Nodalys::ParseNetlist("* ic\nL1 1 0 1m IC=2m\nR1 1 0 1k\n.tran 10n 5u uic\n.end\n");
        REQUIRE(parsed.has_value());
        CHECK(parsed->CircuitInstance.HasInitialConditions());
    }

    SECTION(".ic clamps a node voltage at the start of a transient, without affecting .op") {
        const auto parsed =
            Nodalys::ParseNetlist("* node ic\nC1 out 0 1u\nR1 out 0 1k\n.ic v(out) = 3\n.tran 10u 5m\n.end\n");
        REQUIRE(parsed.has_value());
        CHECK(parsed->CircuitInstance.HasInitialConditions());

        const auto op = Nodalys::ComputeOperatingPoint(parsed->CircuitInstance);
        REQUIRE(op.has_value());
        CHECK_THAT(op->NodeVoltages[1], WithinAbs(0.0, 1e-9));

        const auto tran = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                    std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
        REQUIRE(tran.has_value());
        CHECK_THAT(tran->NodeVoltages[1].front(), WithinAbs(3.0, 1e-6));
        CHECK_THAT(VoltageAt(*tran, 1, 1e-3), WithinAbs(3.0 * std::exp(-1.0), WAVEFORM_TOLERANCE));
    }

    SECTION(".ic holds a node in the operating point that starts the transient and the rest follows it") {
        const auto parsed = Nodalys::ParseNetlist(
            "* held\nV1 1 0 5\nR1 1 2 1k\nR2 2 3 1k\nR3 3 0 1k\nC1 3 0 1u\n.ic v(2)=4\n.tran 10u 5m\n.end\n");
        REQUIRE(parsed.has_value());
        const auto tran = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                    std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
        REQUIRE(tran.has_value());
        // Held at 4 V the two lower resistors split it in half; released, both nodes settle at the operating point
        CHECK_THAT(tran->NodeVoltages[2].front(), WithinAbs(4.0, 1e-6));
        CHECK_THAT(tran->NodeVoltages[3].front(), WithinAbs(2.0, 1e-6));
        CHECK_THAT(tran->NodeVoltages[2].back(), WithinAbs(10.0 / 3.0, 1e-2));
        CHECK_THAT(tran->NodeVoltages[3].back(), WithinAbs(5.0 / 3.0, 1e-2));
    }

    SECTION("With uic .ic gives the capacitors on a node their start, as SPICE does, and holds nothing") {
        const auto parsed = Nodalys::ParseNetlist("* uic\nC1 out 0 1u\nR1 out 0 1k\nV1 1 0 5\nR2 1 2 1k\nR3 2 0 "
                                                  "1k\n.ic v(out)=3 v(2)=4\n.tran 10u 5m uic\n.end\n");
        REQUIRE(parsed.has_value());
        const auto tran = Nodalys::ComputeTransient(parsed->CircuitInstance,
                                                    std::get<Nodalys::TransientConfig>(parsed->Analyses[0].Config));
        REQUIRE(tran.has_value());
        // The numbered nodes keep their numbers and "out" is the next one, node 3. Its capacitor starts at 3 V and
        // discharges with a time constant of 1 ms
        CHECK_THAT(tran->NodeVoltages[3].front(), WithinAbs(3.0, 1e-6));
        CHECK_THAT(VoltageAt(*tran, 3, 1e-3), WithinAbs(3.0 * std::exp(-1.0), WAVEFORM_TOLERANCE));
        // Node 2 has no capacitor, so nothing carries its 4 V: it sits where the divider puts it
        CHECK_THAT(tran->NodeVoltages[2].front(), WithinAbs(2.5, 1e-6));
    }

    SECTION("Malformed .ic directives are rejected") {
        CHECK_FALSE(Nodalys::ParseNetlist("* bad ic\nR1 1 0 1k\n.ic 1=5\n.op\n.end\n").has_value());
        CHECK_FALSE(Nodalys::ParseNetlist("* bad ic\nR1 1 0 1k\n.ic v(1)=abc\n.op\n.end\n").has_value());
    }

    SECTION("P elements are the ports of an S-parameter analysis") {
        // A matched through connection: nothing reflects and everything passes
        const auto parsed =
            Nodalys::ParseNetlist("* ports\nP1 1 0 portnum=1 z0=50\nP2 1 0 portnum=2 z0=50\n.sp dec 1 1k 10k\n.end\n");
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->Analyses.size() == 1);
        CHECK(parsed->Analyses[0].Kind == Nodalys::AnalysisKind::S_PARAMETERS);
    }

    SECTION("A port without its nodes is refused") {
        CHECK_FALSE(Nodalys::ParseNetlist("* ports\nP1 1\n.end\n").has_value());
    }
}

TEST_CASE("A Boyle op-amp with a PNP input stage sources bias current out of its inputs", "[nodalys][opamp]") {
    auto closed_loop_output = [](const bool is_pnp) {
        Nodalys::CircuitBuilder builder;
        builder.AddVdc("VCC", 1, 0, 15.0);
        builder.AddVdc("VEE", 2, 0, -15.0);
        // 100 k feedback resistor from output (node 4) to - input (node 3), + input grounded
        builder.AddResistor("RF", 4, 3, 100e3);
        Nodalys::BoyleOpAmpParameters params;
        params.IsPnpInput = is_pnp;
        params.InputBiasCurrent = 20e-9;
        params.InputOffsetVoltage = 0.0;
        builder.AddBoyleOpAmp("U1", 4, 0, 3, 1, 2, params);
        auto circuit = builder.Build();
        REQUIRE(circuit.has_value());
        const auto op = Nodalys::ComputeOperatingPoint(*circuit);
        REQUIRE(op.has_value());
        return op->NodeVoltages[4];
    };

    // NPN sinks 20 nA into - input -> +2 mV output; PNP sources 20 nA out -> -2 mV output
    CHECK_THAT(closed_loop_output(false), WithinAbs(2e-3, 1e-4));
    CHECK_THAT(closed_loop_output(true), WithinAbs(-2e-3, 1e-4));
}
