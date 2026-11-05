/**
 * @file    spice_import_tests.cpp
 * @brief   Tests for turning the subcircuits of a SPICE library into blocks of the schematic.
 */

#include "document/spice_import.h"

#include "components/subcircuit.h"
#include "document/clipboard.h"
#include "document/schematic.h"
#include "document/schematic_file.h"
#include "document/subcircuit_pins.h"
#include "editing/element_factory.h"
#include "model/library_model.h"
#include "spice/spice_library.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

/// An op-amp as a vendor writes it: numbered ports, documented in a comment.
constexpr std::string_view OP_AMP_LIBRARY = R"(* connections: non-inverting input
*              | inverting input
*              | | positive supply
*              | | | negative supply
*              | | | | output
.SUBCKT OPA-1 1 2 3 4 5
E1 6 0 1 2 100k
R1 6 5 10
.ENDS
)";

/**
 * @brief   A library subcircuit whose ports say what they are.
 */
Core::LibrarySubcircuit NamedPorts() {
    return {.Name = "AMP",
            .Ports = {"IN+", "IN-", "VCC", "VEE", "OUT", "OUT"},
            .Parameters = {{"GAIN", 10.0}},
            .Text = "R1 IN+ OUT 1k\n",
            .Warnings = {}};
}

GUI::SavedSubcircuit ImportOpAmp() {
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(OP_AMP_LIBRARY);
    REQUIRE(library.Subcircuits.size() == 1);
    const Core::LibrarySubcircuit &subcircuit = library.Subcircuits.front();
    std::vector<GUI::SavedSubcircuitPin> pins = GUI::SuggestSpicePins(subcircuit);
    // The user names the numbered ports after the comment and puts them on their edges
    pins[0] = {.Name = "INP", .Point = {}, .Side = Core::PinSide::LEFT};
    pins[1] = {.Name = "INN", .Point = {}, .Side = Core::PinSide::LEFT};
    pins[2] = {.Name = "VP", .Point = {}, .Side = Core::PinSide::TOP};
    pins[3] = {.Name = "VN", .Point = {}, .Side = Core::PinSide::BOTTOM};
    pins[4] = {.Name = "OUT", .Point = {}, .Side = Core::PinSide::RIGHT};
    return GUI::MakeSpiceBlock(subcircuit, std::move(pins));
}

} // namespace

TEST_CASE("Library names become names the schematic accepts", "[spice_import]") {
    CHECK(GUI::MakeIdentifier("OPA-2134", "X") == "OPA_2134");
    CHECK(GUI::MakeIdentifier("IN+", "P") == "IN_");
    CHECK(GUI::MakeIdentifier("3", "P") == "P3");
    CHECK(GUI::MakeIdentifier("", "P") == "P");
    CHECK(GUI::MakeIdentifier("LM358", "X") == "LM358");
}

TEST_CASE("Each port gets a unique pin name and the edge its name suggests", "[spice_import]") {
    const std::vector<GUI::SavedSubcircuitPin> pins = GUI::SuggestSpicePins(NamedPorts());
    REQUIRE(pins.size() == 6);
    CHECK(pins[0].Name == "IN_");
    CHECK(pins[1].Name == "IN__2");
    CHECK(pins[0].Side == Core::PinSide::LEFT);
    CHECK(pins[2].Side == Core::PinSide::TOP);
    CHECK(pins[3].Side == Core::PinSide::BOTTOM);
    CHECK(pins[4].Side == Core::PinSide::RIGHT);
    CHECK(pins[5].Name == "OUT_2");
    CHECK_FALSE(GUI::CheckSpicePins(pins));

    // Numbered ports suggest nothing about their edge, but the comment above them says what they are
    const Core::SpiceLibrary library = Core::ReadSpiceLibrary(OP_AMP_LIBRARY);
    CHECK(library.Subcircuits.front().Description.starts_with(" connections: non-inverting input\n"));
    CHECK(library.Subcircuits.front().Description.contains("| | | | output"));
    const std::vector<GUI::SavedSubcircuitPin> numbered = GUI::SuggestSpicePins(library.Subcircuits.front());
    REQUIRE(numbered.size() == 5);
    CHECK(numbered[0].Name == "P1");
    CHECK(numbered[4].Name == "P5");
    CHECK(numbered[4].Side == Core::PinSide::LEFT);
}

TEST_CASE("Pin names the user types are checked before a SPICE block is kept", "[spice_import]") {
    std::vector<GUI::SavedSubcircuitPin> pins = {{.Name = "A", .Point = {}, .Side = Core::PinSide::LEFT},
                                                 {.Name = "A", .Point = {}, .Side = Core::PinSide::RIGHT}};
    const std::optional<std::string> repeated = GUI::CheckSpicePins(pins);
    REQUIRE(repeated);
    CHECK(repeated->contains("Two pins"));
    pins[1].Name = "2B";
    const std::optional<std::string> invalid = GUI::CheckSpicePins(pins);
    REQUIRE(invalid);
    CHECK(invalid->contains("not a pin name"));
}

TEST_CASE("A SPICE block keeps its ports, text and pin edges through a file", "[spice_import]") {
    std::vector<GUI::SavedSubcircuit> definitions;
    definitions.push_back(ImportOpAmp());
    CHECK(definitions[0].Name == "OPA_1");
    REQUIRE(definitions[0].Spice);
    CHECK(definitions[0].Spice->Ports == std::vector<std::string>{"1", "2", "3", "4", "5"});

    const std::string saved = GUI::SaveSchematic({}, {}, {}, {}, definitions);
    const auto loaded = GUI::LoadSchematic(saved);
    REQUIRE(loaded);
    CHECK(loaded->Warnings.empty());
    REQUIRE(loaded->Subcircuits.size() == 1);
    const GUI::SavedSubcircuit &copy = loaded->Subcircuits.front();
    REQUIRE(copy.Spice);
    CHECK(*copy.Spice == *definitions[0].Spice);
    CHECK(copy.Pins == definitions[0].Pins);
    CHECK(copy.Elements.empty());

    // A file whose pins and ports do not pair up is refused with a reason
    const auto mismatched = GUI::LoadSchematic(R"({"format":"imcsim-schematic","version":1,"elements":[],
        "subcircuits":[{"name":"B","pins":[{"name":"A","side":"left"}],
                        "spice":{"ports":["1","2"],"text":"R1 1 2 1k\n"}}]})");
    REQUIRE(mismatched);
    CHECK(mismatched->Subcircuits.empty());
    REQUIRE(mismatched->Warnings.size() == 1);
    CHECK(mismatched->Warnings[0].contains("number of pins"));
    const auto sideless = GUI::LoadSchematic(R"({"format":"imcsim-schematic","version":1,"elements":[],
        "subcircuits":[{"name":"B","pins":[{"name":"A"}],"spice":{"ports":["1"],"text":""}}]})");
    REQUIRE(sideless);
    CHECK(sideless->Subcircuits.empty());
    CHECK(sideless->Warnings[0].contains("edge"));
}

TEST_CASE("An imported block is placed with its pins on their edges and written with its ports", "[spice_import]") {
    GUI::Schematic schematic;
    std::vector<GUI::SavedSubcircuit> imported;
    imported.push_back(ImportOpAmp());
    std::vector<std::unique_ptr<GUI::UIElement>> no_instances;
    const std::vector<std::string> names = schematic.MergeSubcircuits(std::move(imported), no_instances);
    REQUIRE(names == std::vector<std::string>{"OPA_1"});

    auto element = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0);
    auto &instance = static_cast<Core::SubcircuitInstance &>(element->GetComponent());
    GUI::ApplyDefinitionPins(instance, schematic.GetSubcircuits().front());
    CHECK(instance.GetPinSide("VP") == Core::PinSide::TOP);
    CHECK(instance.GetPinSide("OUT") == Core::PinSide::RIGHT);
    schematic.AddElement(std::move(element));

    const std::string netlist = schematic.BuildSpiceNetlist();
    INFO(netlist);
    CHECK(netlist.contains(".subckt OPA_1 1 2 3 4 5\n"));
    CHECK(netlist.contains("E1 6 0 1 2 100k\n"));
    // A drawing of SPICE text cannot be opened
    CHECK_FALSE(schematic.OpenSubcircuit("OPA_1"));
}

TEST_CASE("New pin names and edges of a SPICE block reach its instances, which keep their wires", "[spice_import]") {
    GUI::Schematic schematic;
    std::vector<GUI::SavedSubcircuit> imported;
    imported.push_back(ImportOpAmp());
    std::vector<std::unique_ptr<GUI::UIElement>> no_instances;
    schematic.MergeSubcircuits(std::move(imported), no_instances);
    auto element = GUI::CreateElement(Core::ComponentType::SUBCIRCUIT, {0, 0}, GUI::Rotation::R0);
    GUI::ApplyDefinitionPins(static_cast<Core::SubcircuitInstance &>(element->GetComponent()),
                             schematic.GetSubcircuits().front());
    schematic.AddElement(std::move(element));
    // A wire from the output, on the right edge, to a point further right
    const GUI::GridPoint output = schematic.GetElement(0).GetTerminals()[4];
    schematic.AddWire(output, {output.X + 4, output.Y});
    schematic.CommitUndoStep();

    std::vector<GUI::SavedSubcircuitPin> pins = schematic.GetSubcircuits().front().Pins;
    pins[4].Name = "Q";
    pins[4].Side = Core::PinSide::BOTTOM;
    REQUIRE(schematic.UpdateSpiceSubcircuit("OPA_1", pins, {}));
    const auto &instance = static_cast<const Core::SubcircuitInstance &>(schematic.GetElement(0).GetComponent());
    CHECK(instance.GetPins()[4] == "Q");
    CHECK(instance.GetPinSide("Q") == Core::PinSide::BOTTOM);
    const GUI::GridPoint moved = schematic.GetElement(0).GetTerminals()[4];
    CHECK(moved != output);
    const GUI::Connectivity &nodes = schematic.GetConnectivity();
    CHECK(nodes.GetNode(moved) == nodes.GetNode({output.X + 4, output.Y}));

    // The text has five ports, so it needs five pins
    pins.pop_back();
    CHECK_FALSE(schematic.UpdateSpiceSubcircuit("OPA_1", pins, {}));
}

TEST_CASE("Library models and the parts that use them come back from a file", "[spice_import]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::DIODE, {0, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetName("D1");
    elements.back()->GetComponent().SetLibraryModel("1N4148");
    const std::vector<Core::LibraryModel> models = {*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")};

    const auto loaded = GUI::LoadSchematic(GUI::SaveSchematic(elements, {}, {}, {}, {}, models));
    REQUIRE(loaded);
    CHECK(loaded->Warnings.empty());
    CHECK(loaded->Models == models);
    REQUIRE(loaded->Elements.size() == 1);
    CHECK(loaded->Elements[0]->GetComponent().GetLibraryModel() == "1N4148");

    const auto broken = GUI::LoadSchematic(R"json({"format":"imcsim-schematic","version":1,"elements":[],
        "models":[{"text":".model SW1 SW(VT=1)"},{"text":".model D1 D"},{"text":".model D1 D(IS=1n)"}]})json");
    REQUIRE(broken);
    CHECK(broken->Models.size() == 1);
    REQUIRE(broken->Warnings.size() == 2);
    CHECK(broken->Warnings[0].contains("not a diode or transistor"));
    CHECK(broken->Warnings[1].contains("repeated"));
}

TEST_CASE("A library model that differs from one of the same name joins under a name of its own", "[spice_import]") {
    GUI::Schematic schematic;
    std::vector<std::unique_ptr<GUI::UIElement>> no_parts;
    std::vector<GUI::SavedSubcircuit> no_blocks;
    CHECK(schematic.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")}, no_parts, no_blocks) ==
          std::vector<std::string>{"1N4148"});
    // The same model again adds nothing
    CHECK(schematic.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")}, no_parts, no_blocks) ==
          std::vector<std::string>{"1N4148"});
    REQUIRE(schematic.GetLibraryModels().size() == 1);

    // A different one is renamed, and so are the parts that came with it, in blocks too
    std::vector<std::unique_ptr<GUI::UIElement>> parts;
    parts.push_back(GUI::CreateElement(Core::ComponentType::DIODE, {0, 0}, GUI::Rotation::R0));
    parts.back()->GetComponent().SetLibraryModel("1N4148");
    std::vector<GUI::SavedSubcircuit> blocks(1);
    blocks[0].Elements.push_back(GUI::CreateElement(Core::ComponentType::LED, {0, 0}, GUI::Rotation::R0));
    blocks[0].Elements.back()->GetComponent().SetLibraryModel("1N4148");
    CHECK(schematic.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=1n)")}, parts, blocks) ==
          std::vector<std::string>{"1N4148_2"});
    REQUIRE(schematic.GetLibraryModels().size() == 2);
    CHECK(schematic.GetLibraryModels()[1].Text == ".model 1N4148_2 D(IS=1n)\n");
    CHECK(parts[0]->GetComponent().GetLibraryModel() == "1N4148_2");
    CHECK(blocks[0].Elements[0]->GetComponent().GetLibraryModel() == "1N4148_2");

    // The circuit carries the models, and undo takes them back
    schematic.CommitUndoStep();
    CHECK(schematic.BuildSpiceNetlist().contains(".model LIB_1N4148_2 D(IS=1n)"));
}

TEST_CASE("A copied part takes the library model it uses along", "[spice_import]") {
    std::vector<std::unique_ptr<GUI::UIElement>> elements;
    elements.push_back(GUI::CreateElement(Core::ComponentType::DIODE, {0, 0}, GUI::Rotation::R0));
    elements.back()->GetComponent().SetLibraryModel("1N4148");
    elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {6, 0}, GUI::Rotation::R0));
    const std::vector<Core::LibraryModel> models = {*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)"),
                                                    *Core::ReadLibraryModel(".model 2N3904 NPN(BF=416)")};

    const std::string text = GUI::CopySelection(elements, {}, {.Elements = {0}}, {}, models);
    const auto group = GUI::ParseCopiedSchematic(text);
    REQUIRE(group);
    // Only the model the copied diode uses goes along
    REQUIRE(group->Models.size() == 1);
    CHECK(group->Models[0].Name == "1N4148");
    REQUIRE(group->Elements.size() == 1);
    CHECK(group->Elements[0]->GetComponent().GetLibraryModel() == "1N4148");
}
