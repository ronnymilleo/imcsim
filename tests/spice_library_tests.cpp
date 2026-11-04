/**
 * @file    spice_library_tests.cpp
 * @brief   Tests for reading the subcircuits of SPICE library files and simulating them as text.
 */

#include "spice_library.h"

#include "circuit.h"
#include "circuit_checks.h"
#include "components/diode.h"
#include "components/ground.h"
#include "components/mosfet.h"
#include "components/resistor.h"
#include "components/subcircuit.h"
#include "components/vcc.h"
#include "simulator.h"
#include "subcircuit.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

/**
 * @brief   A definition of the text of a library subcircuit, with one pin per port.
 */
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

TEST_CASE("A device model is read with its name, kind and text, and written under a name of its own",
          "[spice_library]") {
    const std::optional<Core::LibraryModel> diode =
        Core::ReadLibraryModel(".MODEL 1N4148 D(IS=2.52n RS=0.568\n+ N=1.752)");
    REQUIRE(diode);
    CHECK(diode->Name == "1N4148");
    CHECK(diode->Kind == Core::ComponentType::DIODE);
    CHECK(diode->Text == ".MODEL 1N4148 D(IS=2.52n RS=0.568\n+ N=1.752)\n");
    CHECK(Core::FormatLibraryModel(*diode).starts_with(".MODEL LIB_1N4148 D(IS=2.52n"));
    CHECK(Core::RenameLibraryModel(*diode, "1N4148_2").starts_with(".MODEL 1N4148_2 D("));

    const std::optional<Core::LibraryModel> npn = Core::ReadLibraryModel(".model 2N3904 NPN (IS=6.734f BF=416.4)");
    REQUIRE(npn);
    CHECK(npn->Kind == Core::ComponentType::NPN);
    CHECK(Core::ReadLibraryModel(".model IRF540 NMOS(LEVEL=3 VTO=3.5)")->Kind == Core::ComponentType::NMOS);
    // No part of imcsim takes these
    CHECK_FALSE(Core::ReadLibraryModel(".model IRFP VDMOS(VTO=4)"));
    CHECK_FALSE(Core::ReadLibraryModel(".model SW1 SW(VT=1)"));
    CHECK_FALSE(Core::ReadLibraryModel("R1 1 2 1k"));

    CHECK(Core::CanUseLibraryModel(Core::ComponentType::LED, Core::ComponentType::DIODE));
    CHECK(Core::CanUseLibraryModel(Core::ComponentType::ZENER_DIODE, Core::ComponentType::DIODE));
    CHECK(Core::CanUseLibraryModel(Core::ComponentType::NPN, Core::ComponentType::NPN));
    CHECK_FALSE(Core::CanUseLibraryModel(Core::ComponentType::PNP, Core::ComponentType::NPN));
    CHECK_FALSE(Core::CanUseLibraryModel(Core::ComponentType::NPN, Core::ComponentType::DIODE));
}

TEST_CASE("A library offers the diode and transistor models outside its subcircuits", "[spice_library]") {
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(R"(.model 1N4148 D(IS=2.52n)
.model Q2N3904 NPN(IS=6.734f BF=416.4)
.model RELAY SW(VT=1)
.subckt WITH A B
.model INSIDE D(IS=1n)
D1 A B INSIDE
.ends
)");
    REQUIRE(library.Models.size() == 2);
    CHECK(library.Models[0].Name == "1N4148");
    CHECK(library.Models[1].Name == "Q2N3904");
    CHECK(library.Warnings.empty());

    const Core::SpiceLibrary models_only = Core::ReadSpiceLibrary(".model 1N4148 D(IS=2.52n)\n");
    CHECK(models_only.Subcircuits.empty());
    CHECK(models_only.Models.size() == 1);
    CHECK(models_only.Warnings.empty());
}

TEST_CASE("A diode that uses a library model is written with it and simulates", "[spice_library]") {
    Core::Diode diode(Core::ComponentType::DIODE);
    diode.SetName("D1");
    diode.SetLibraryModel("1N4148");
    CHECK(diode.HasLibraryModel());
    CHECK_FALSE(diode.IsCustom());
    CHECK(diode.GetModel() == nullptr);
    CHECK(std::string_view(diode.GetModelName()) == "1N4148");
    CHECK(diode.GetSpiceModelName() == "LIB_1N4148");
    Core::Resistor resistor;
    resistor.SetName("R1");
    resistor.SetValue(1e3);
    Core::VCC supply;
    supply.SetName("V1");
    supply.SetValue(5.0);
    const Core::Ground ground;
    Core::Circuit circuit;
    circuit.Add(supply, {1});
    circuit.Add(resistor, {1, 2});
    circuit.Add(diode, {2, 0});
    circuit.Add(ground, {0});

    // Without the model in the circuit, the run says which part misses it
    const std::optional<std::string> missing = Core::CheckCircuit(circuit);
    REQUIRE(missing);
    CHECK(missing->contains("D1 uses the model 1N4148"));
    circuit.AddLibraryModel(*Core::ReadLibraryModel(".model 2N3904 NPN(IS=6.734f BF=416.4)"));
    diode.SetLibraryModel("2N3904");
    const std::optional<std::string> wrong_kind = Core::CheckCircuit(circuit);
    REQUIRE(wrong_kind);
    CHECK(wrong_kind->contains("NPN model"));

    circuit.AddLibraryModel(*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n RS=0.568 N=1.752)"));
    diode.SetLibraryModel("1N4148");
    const std::string netlist = circuit.ToSpiceNetlist();
    INFO(netlist);
    CHECK(netlist.contains("D1 2 0 LIB_1N4148\n"));
    CHECK(netlist.contains(".model LIB_1N4148 D(IS=2.52n RS=0.568 N=1.752)\n"));
    CHECK_FALSE(netlist.contains("DCUSTOM"));
    const Core::OperatingPointRun run = Core::RunOperatingPoint(circuit);
    if (!run.Result) {
        FAIL(run.Result.error());
    }
    // A small-signal silicon diode at a few milliamps
    CHECK(run.Result->NodeVoltages[2] > 0.55);
    CHECK(run.Result->NodeVoltages[2] < 0.85);

    // A ready model or custom parameters take the place of the library model again
    diode.SetCustom();
    CHECK_FALSE(diode.HasLibraryModel());
    CHECK(diode.IsCustom());
}

TEST_CASE("A MOSFET with a library model takes its channel size from the model", "[spice_library]") {
    Core::MOSFET mosfet(Core::ComponentType::NMOS);
    mosfet.SetName("M1");
    mosfet.SetLibraryModel("IRF540");
    Core::Circuit circuit;
    circuit.Add(mosfet, {1, 2, 0});
    circuit.AddLibraryModel(*Core::ReadLibraryModel(".model IRF540 NMOS(LEVEL=3 VTO=3.5 KP=20u)"));
    const std::string netlist = circuit.ToSpiceNetlist();
    INFO(netlist);
    CHECK(netlist.contains("M1 1 2 0 0 LIB_IRF540\n"));
    CHECK(netlist.contains(".model LIB_IRF540 NMOS(LEVEL=3 VTO=3.5 KP=20u)\n"));
    // A resistor cannot use one
    Core::Resistor resistor;
    resistor.SetLibraryModel("IRF540");
    CHECK_FALSE(resistor.HasLibraryModel());
}
