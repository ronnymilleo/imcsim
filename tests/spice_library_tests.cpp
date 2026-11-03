/**
 * @file    spice_library_tests.cpp
 * @brief   Tests for reading the subcircuits of SPICE library files and simulating them as text.
 */

#include "spice_library.h"

#include "circuit.h"
#include "components/ground.h"
#include "components/subcircuit.h"
#include "components/vcc.h"
#include "simulator.h"
#include "subcircuit.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

// A library as vendors write them: upper and lower case, continuations, comments, definitions outside the subcircuits
// that use them, a subcircuit one uses, a model no one uses, an include and an encrypted model
constexpr std::string_view LIBRARY = R"(* Test library
.param RLOAD=10k
.SUBCKT DIVIDER IN OUT
+ PARAMS: RTOP=1k RBOT={RLOAD}
R1 IN OUT {RTOP} ; the upper resistor
R2 OUT 0 {RBOT}
.ENDS DIVIDER
.subckt CLAMP A K
* The element and its model share a name, as some libraries do
D1 A K D1
X1 K 0 LOAD
.ends
.subckt LOAD P N
R1 P N 2k
.ends LOAD
.model D1 D(IS=1e-14 N=1)
.model UNUSED NPN
.include other.lib
.subckt SECRET A B
$CDNENCSTART
not readable
$CDNENCEND
.ends
)";

const Core::LibrarySubcircuit &Find(const Core::SpiceLibrary &library, const std::string &name) {
    for (const Core::LibrarySubcircuit &subcircuit : library.Subcircuits) {
        if (subcircuit.Name == name) {
            return subcircuit;
        }
    }
    FAIL("No subcircuit " << name);
    return library.Subcircuits.front();
}

// A definition of the text of a library subcircuit, with one pin per port
std::shared_ptr<Core::SubcircuitDefinition> MakeDefinition(const Core::LibrarySubcircuit &subcircuit) {
    auto definition = std::make_shared<Core::SubcircuitDefinition>();
    definition->Name = subcircuit.Name;
    for (const std::string &port : subcircuit.Ports) {
        definition->Pins.push_back({port, 0});
    }
    definition->Parameters = subcircuit.Parameters;
    definition->Spice = Core::SubcircuitText{.Ports = subcircuit.Ports, .Text = subcircuit.Text};
    return definition;
}

} // namespace

TEST_CASE("A library gives each subcircuit with its ports, numeric parameters and what it uses", "[spice_library]") {
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(LIBRARY);
    REQUIRE(library.Subcircuits.size() == 3);

    const Core::LibrarySubcircuit &divider = Find(library, "DIVIDER");
    CHECK(divider.Ports == std::vector<std::string>{"IN", "OUT"});
    REQUIRE(divider.Parameters.size() == 1);
    CHECK(divider.Parameters[0].Name == "RTOP");
    CHECK(divider.Parameters[0].DefaultValue == Catch::Approx(1e3));
    // A default that is an expression stays in the text, after the file parameter it reads
    CHECK(divider.Text.contains(".param RBOT={RLOAD}"));
    CHECK(divider.Text.find(".param RLOAD=10k") < divider.Text.find(".param RBOT={RLOAD}"));
    CHECK(divider.Text.contains("R1 IN OUT {RTOP}"));
    CHECK_FALSE(divider.Text.contains(".ENDS"));

    // Only a comment right above a .subckt line describes it
    CHECK(divider.Description.empty());

    const Core::LibrarySubcircuit &clamp = Find(library, "CLAMP");
    CHECK(clamp.Text.contains(".model D1 D(IS=1e-14 N=1)"));
    CHECK(clamp.Text.contains(".subckt LOAD P N"));
    CHECK_FALSE(clamp.Text.contains("UNUSED"));
    CHECK_FALSE(clamp.Text.contains("RLOAD"));
}

TEST_CASE("A library reports what it cannot follow and leaves encrypted models out", "[spice_library]") {
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(LIBRARY);
    REQUIRE(library.Warnings.size() == 2);
    CHECK(library.Warnings[0].contains(".include"));
    CHECK(library.Warnings[1].contains("SECRET is encrypted"));

    const Core::SpiceLibrary empty = Core::ReadSpiceLibrary("* only a comment\nR1 1 0 1k\n");
    CHECK(empty.Subcircuits.empty());
    REQUIRE(empty.Warnings.size() == 1);
    CHECK(empty.Warnings[0].contains("no .subckt"));

    const Core::SpiceLibrary unclosed = Core::ReadSpiceLibrary(".subckt OPEN A B\nR1 A B 1k\n");
    CHECK(unclosed.Subcircuits.empty());
    CHECK(unclosed.Warnings[0].contains("ends inside a subcircuit"));

    const Core::SpiceLibrary inner = Core::ReadSpiceLibrary(".subckt WITH A\n.lib parts.lib TYP\nR1 A 0 1k\n.ends\n");
    REQUIRE(inner.Subcircuits.size() == 1);
    REQUIRE(inner.Subcircuits[0].Warnings.size() == 1);
    CHECK(inner.Subcircuits[0].Warnings[0].contains(".lib is not followed"));
}

TEST_CASE("SPICE numbers read their scale without regard to case and ignore the unit", "[spice_library]") {
    CHECK(Core::ParseSpiceNumber("4.7k") == Catch::Approx(4.7e3));
    CHECK(Core::ParseSpiceNumber("10Meg") == Catch::Approx(10e6));
    // SPICE reads M as milli, unlike the fields of the editor
    CHECK(Core::ParseSpiceNumber("2M") == Catch::Approx(2e-3));
    CHECK(Core::ParseSpiceNumber("100nF") == Catch::Approx(100e-9));
    CHECK(Core::ParseSpiceNumber("1e-3") == Catch::Approx(1e-3));
    CHECK(Core::ParseSpiceNumber("5") == Catch::Approx(5.0));
    CHECK_FALSE(Core::ParseSpiceNumber("{2*R}"));
    CHECK_FALSE(Core::ParseSpiceNumber("k"));
    CHECK_FALSE(Core::ParseSpiceNumber("1k-2"));
}

TEST_CASE("A subcircuit kept as text simulates with its parameters, models and the subcircuits it uses",
          "[spice_library]") {
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(LIBRARY);
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    const Core::Ground ground;

    Core::SubcircuitInstance divider("DIVIDER", {"IN", "OUT"});
    divider.SetName("X1");
    Core::Circuit divided;
    divided.Add(supply, {1});
    divided.Add(divider, {1, 2});
    divided.Add(ground, {0});
    divided.AddSubcircuit(MakeDefinition(Find(library, "DIVIDER")));
    const std::string netlist = divided.ToSpiceNetlist();
    INFO(netlist);
    CHECK(netlist.contains(".subckt DIVIDER IN OUT params: RTOP=1k\n"));
    const Core::OperatingPointRun by_default = Core::RunOperatingPoint(divided);
    REQUIRE(by_default.Result);
    // 1k over the 10k the file parameter gives
    CHECK(by_default.Result->NodeVoltages[2] == Catch::Approx(5.0 * 10.0 / 11.0).margin(1e-6));
    divider.SetParameter("RTOP", 10e3);
    const Core::OperatingPointRun overridden = Core::RunOperatingPoint(divided);
    REQUIRE(overridden.Result);
    CHECK(overridden.Result->NodeVoltages[2] == Catch::Approx(2.5).margin(1e-6));

    Core::SubcircuitInstance clamp("CLAMP", {"A", "K"});
    clamp.SetName("X2");
    Core::Circuit clamped;
    clamped.Add(supply, {1});
    clamped.Add(clamp, {1, 2});
    clamped.Add(ground, {0});
    clamped.AddSubcircuit(MakeDefinition(Find(library, "CLAMP")));
    const std::string clamp_netlist = clamped.ToSpiceNetlist();
    INFO(clamp_netlist);
    // The model is scoped to the block, and the element keeps its own name, D1, so it stays a diode
    CHECK(clamp_netlist.contains("D1 A K imcsim_CLAMP_D1\n"));
    CHECK(clamp_netlist.contains(".model imcsim_CLAMP_D1 D(IS=1e-14 N=1)"));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(clamped);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // A silicon diode drops a little under a volt into the 2k load
    CHECK(run.Result->NodeVoltages[2] > 4.0);
    CHECK(run.Result->NodeVoltages[2] < 4.6);
}
