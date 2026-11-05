/**
 * @file    spice_inclusions_tests.cpp
 * @brief   Tests for following the .include and .lib lines of a SPICE library.
 */

#include "spice/spice_inclusions.h"
#include "spice/spice_library.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {

/**
 * @brief   A reader of files that live in a map, by their path with forward slashes.
 */
Core::LibraryFileReader MakeReader(std::map<std::string, std::string> files) {
    return [files = std::move(files)](const std::filesystem::path &path) -> std::expected<std::string, std::string> {
        const auto found = files.find(path.generic_string());
        if (found == files.end()) {
            return std::unexpected("No such file");
        }
        return found->second;
    };
}

const Core::LibrarySubcircuit *Find(const Core::SpiceLibrary &library, const std::string_view name) {
    const auto found = std::ranges::find(library.Subcircuits, name, &Core::LibrarySubcircuit::Name);
    return found == library.Subcircuits.end() ? nullptr : &*found;
}

bool HasWarning(const std::vector<std::string> &warnings, const std::string_view text) {
    return std::ranges::any_of(warnings, [&](const std::string &warning) { return warning.contains(text); });
}

} // namespace

TEST_CASE("An included file takes the place of its line, found from the folder of the file that names it",
          "[spice_inclusions]") {
    const auto read = MakeReader({{"/lib/main.lib", ".include \"models/diode.mod\"\n"
                                                    ".subckt CLAMP A B\nD1 A B DX\n.ends\n"},
                                  {"/lib/models/diode.mod", ".model DX D(IS=1e-14)\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/main.lib", read);
    CHECK(library.Warnings.empty());
    const Core::LibrarySubcircuit *clamp = Find(library, "CLAMP");
    REQUIRE(clamp != nullptr);
    // The model comes along in the text, so the block simulates without the files
    CHECK(clamp->Text.contains(".model DX D(IS=1e-14)"));
    REQUIRE(library.Models.size() == 1);
    CHECK(library.Models[0].Name == "DX");
}

TEST_CASE("A .lib line with a section reads only that section of the file", "[spice_inclusions]") {
    const auto read = MakeReader({{"/lib/main.lib", ".lib corners.lib TYP\n.subckt LOAD A B\nR1 A B {RVAL}\n.ends\n"},
                                  {"/lib/corners.lib", ".lib FAST\n.param RVAL=900\n.endl FAST\n"
                                                       ".lib TYP\n.param RVAL=1k\n.endl TYP\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/main.lib", read);
    CHECK(library.Warnings.empty());
    const Core::LibrarySubcircuit *load = Find(library, "LOAD");
    REQUIRE(load != nullptr);
    CHECK(load->Text.contains(".param RVAL=1k"));
    CHECK_FALSE(load->Text.contains("900"));
}

TEST_CASE("A section may include another section of its own file", "[spice_inclusions]") {
    // Vendors split a corner into sections of one file that name each other
    const auto read = MakeReader({{"/lib/main.lib", ".lib parts.lib TYP\n"},
                                  {"/lib/parts.lib", ".lib TYP\n.lib 'parts.lib' MODELS\n.subckt CELL A\nD1 A 0 DC\n"
                                                     ".ends\n.endl\n.lib MODELS\n.model DC D(IS=2e-14)\n.endl\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/main.lib", read);
    CHECK(library.Warnings.empty());
    const Core::LibrarySubcircuit *cell = Find(library, "CELL");
    REQUIRE(cell != nullptr);
    CHECK(cell->Text.contains(".model DC D(IS=2e-14)"));
}

TEST_CASE("An inclusion inside a subcircuit brings its lines into that subcircuit", "[spice_inclusions]") {
    const auto read = MakeReader(
        {{"/lib/amp.lib", ".subckt AMP IN OUT\n.include stage.inc\n.ends\n"}, {"/lib/stage.inc", "R1 IN OUT 1k\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/amp.lib", read);
    const Core::LibrarySubcircuit *amp = Find(library, "AMP");
    REQUIRE(amp != nullptr);
    CHECK(amp->Text.contains("R1 IN OUT 1k"));
    CHECK(amp->Warnings.empty());
}

TEST_CASE("A .lib line naming only a file includes all of it, as LTspice writes it", "[spice_inclusions]") {
    const auto read = MakeReader({{"/lib/main.lib", ".lib opamps.lib\n"},
                                  {"/lib/opamps.lib", ".subckt BUFFER IN OUT\nE1 OUT 0 IN 0 1\n.ends\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/main.lib", read);
    CHECK(library.Warnings.empty());
    CHECK(Find(library, "BUFFER") != nullptr);
}

TEST_CASE("The sections of the file picked are all read, without their markers", "[spice_inclusions]") {
    const auto read = MakeReader({{"/lib/main.lib", ".lib TYP\n.subckt ONE A\nR1 A 0 1k\n.ends\n.endl TYP\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/main.lib", read);
    CHECK(library.Warnings.empty());
    const Core::LibrarySubcircuit *one = Find(library, "ONE");
    REQUIRE(one != nullptr);
    CHECK_FALSE(one->Text.contains(".endl"));
}

TEST_CASE("Missing files, missing sections and loops are reported where they are written", "[spice_inclusions]") {
    const auto read = MakeReader({{"/lib/a.lib", ".include b.lib\n.include gone.lib\n.lib b.lib NONE\n"
                                                 ".subckt KEEP A\nR1 A 0 1k\n.ends\n"},
                                  {"/lib/b.lib", "* b includes a again\n.include a.lib\n"}});
    const Core::SpiceLibrary library = Core::ReadSpiceLibraryFile("/lib/a.lib", read);
    CHECK(HasWarning(library.Warnings, "Line 2 of b.lib: a.lib includes itself"));
    CHECK(HasWarning(library.Warnings, "Line 2: could not read gone.lib"));
    CHECK(HasWarning(library.Warnings, "Line 3: b.lib has no section NONE"));
    // What could be read still is
    CHECK(Find(library, "KEEP") != nullptr);

    const Core::SpiceLibrary unread = Core::ReadSpiceLibraryFile("/lib/none.lib", read);
    CHECK(unread.Subcircuits.empty());
    REQUIRE(unread.Warnings.size() == 1);
    CHECK(unread.Warnings[0].contains("Could not read none.lib"));
}

TEST_CASE("The lines of a library given as text keep their numbers for the warnings", "[spice_inclusions]") {
    const Core::LibraryLines lines = Core::ListLibraryLines("R1 A B 1k\r\n.ends\n");
    REQUIRE(lines.Lines.size() == 3);
    CHECK(lines.Lines[0] == "R1 A B 1k");
    CHECK(lines.Origins[1] == "Line 2");
    CHECK(lines.Warnings.empty());

    const Core::SpiceLibrary library = Core::ReadSpiceLibrary("R1 A B 1k\n.ends\n");
    CHECK(HasWarning(library.Warnings, "Line 2 closes a subcircuit that was never opened"));
}
