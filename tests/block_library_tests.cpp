/**
 * @file    block_library_tests.cpp
 * @brief   Tests for the block definitions and library models a schematic keeps, and the blocks as the simulator takes
 *          them.
 */

#include "circuit/library_model.h"
#include "schematic/block_library.h"
#include "schematic/element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

GUI::SavedSubcircuit MakeBlock(const std::string &name) {
    GUI::SavedSubcircuit block;
    block.Name = name;
    block.Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {0, 0}, GUI::Rotation::R0));
    return block;
}

std::vector<GUI::SavedSubcircuit> MakeBlocks(const std::vector<std::string> &names) {
    std::vector<GUI::SavedSubcircuit> blocks;
    for (const std::string &name : names) {
        blocks.push_back(MakeBlock(name));
    }
    return blocks;
}

} // namespace

TEST_CASE("A new block takes an identifier no other block has", "[block_library]") {
    const GUI::BlockLibrary library(MakeBlocks({"filter"}), {});

    CHECK(library.CheckNewName("amplifier").has_value());
    CHECK(library.CheckNewName("filter").error() == "A subcircuit with this name already exists");
    CHECK(library.CheckNewName("two words").error() == "The subcircuit name must be an identifier");
}

TEST_CASE("The blocks are built for the simulator once, and again after they change", "[block_library]") {
    GUI::BlockLibrary library(MakeBlocks({"filter"}), {});

    const auto first = library.GetBuiltDefinitions();
    REQUIRE(first.size() == 1);
    CHECK(first[0]->Name == "filter");
    // Nothing changed, so the same definitions come back
    CHECK(library.GetBuiltDefinitions()[0] == first[0]);

    library.AddSubcircuit(MakeBlock("amplifier"));
    const auto &second = library.GetBuiltDefinitions();
    REQUIRE(second.size() == 2);
    CHECK(second[0] != first[0]);
    CHECK(second[1]->Name == "amplifier");
}

TEST_CASE("Merging tells whether anything joined the library", "[block_library]") {
    GUI::BlockLibrary library(MakeBlocks({"filter"}), {*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")});
    std::vector<std::unique_ptr<GUI::UIElement>> no_parts;
    std::vector<GUI::SavedSubcircuit> no_blocks;

    // Equal ones are dropped in favour of those already here
    const GUI::MergeResult same_block = library.MergeSubcircuits(MakeBlocks({"filter"}), no_parts);
    CHECK(same_block.Names == std::vector<std::string>{"filter"});
    CHECK_FALSE(same_block.IsAnyAdded);
    const GUI::MergeResult same_model =
        library.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")}, no_parts, no_blocks);
    CHECK_FALSE(same_model.IsAnyAdded);

    // Each model that differs takes the next free name
    const GUI::MergeResult second =
        library.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=1n)")}, no_parts, no_blocks);
    CHECK(second.Names == std::vector<std::string>{"1N4148_2"});
    CHECK(second.IsAnyAdded);
    const GUI::MergeResult third =
        library.MergeLibraryModels({*Core::ReadLibraryModel(".model 1N4148 D(IS=2n)")}, no_parts, no_blocks);
    CHECK(third.Names == std::vector<std::string>{"1N4148_3"});
    CHECK(library.GetLibraryModels().size() == 3);
}

TEST_CASE("A copy of another library leaves out the block it is asked to", "[block_library]") {
    const GUI::BlockLibrary document(MakeBlocks({"outer", "inner"}),
                                     {*Core::ReadLibraryModel(".model 1N4148 D(IS=2.52n)")});
    GUI::BlockLibrary content;

    content.CopyFrom(document, "outer");

    REQUIRE(content.GetSubcircuits().size() == 1);
    CHECK(content.GetSubcircuits()[0].Name == "inner");
    CHECK(content.GetLibraryModels() == document.GetLibraryModels());
    // The copy shares no parts with the document
    CHECK(content.GetSubcircuits()[0].Elements[0].get() != document.GetSubcircuits()[1].Elements[0].get());
}

TEST_CASE("A block opens for editing as copies of it and of the other blocks", "[block_library]") {
    const GUI::BlockLibrary library(MakeBlocks({"outer", "inner"}), {});

    const auto copy = library.CopyForEditing("inner");
    REQUIRE(copy.has_value());
    CHECK(copy->Definition.Name == "inner");
    CHECK(copy->Definition.Elements.size() == 1);
    REQUIRE(copy->OtherDefinitions.size() == 1);
    CHECK(copy->OtherDefinitions[0].Name == "outer");

    CHECK(library.CopyForEditing("missing").error() == "Unknown subcircuit missing");
}
