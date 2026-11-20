/**
 * @file    fuse_blow_tracker_tests.cpp
 * @brief   Tests for how the fuses of the transient on display are followed from frame to frame.
 */

#include "overlays/fuse_blow_tracker.h"

#include "overlays/fuse_flash.h"
#include "schematic/connectivity.h"
#include "schematic/element_factory.h"
#include "test_printers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace {

/// The index of the fuse among the parts of FuseCircuit.
constexpr std::size_t FUSE_INDEX = 1;

/**
 * @struct  FuseCircuit
 * @brief   A supply feeding a fuse into a resistor to ground, and a transient in which the fuse blows at 200 ms.
 */
struct FuseCircuit {
    std::vector<std::unique_ptr<GUI::UIElement>> Elements;
    std::vector<GUI::UIWire> Wires;
    std::unique_ptr<GUI::Connectivity> Nodes;
    Core::Transient Result;

    FuseCircuit() {
        Elements.push_back(GUI::CreateElement(Core::ComponentType::VCC, {0, 0}, GUI::Rotation::R0));
        Elements.push_back(GUI::CreateElement(Core::ComponentType::FUSE, {6, 0}, GUI::Rotation::R0));
        Elements.push_back(GUI::CreateElement(Core::ComponentType::RESISTOR, {10, 0}, GUI::Rotation::R0));
        Elements.push_back(GUI::CreateElement(Core::ComponentType::GROUND, {12, 4}, GUI::Rotation::R0));
        Elements[0]->GetComponent().SetName("V1");
        Elements[1]->GetComponent().SetName("FU1");
        Elements[2]->GetComponent().SetName("R1");
        Wires = {{{0, 0}, {4, 0}}, {{12, 0}, {12, 4}}};
        Nodes = std::make_unique<GUI::Connectivity>(Elements, Wires);
        Result.Times = {0.0, 0.1, 0.2, 0.3};
        Result.NodeVoltages.assign(3, std::vector<double>(4, 0.0));
        Result.NodeVoltages[static_cast<std::size_t>(Nodes->GetNode({4, 0}).value_or(0))] = {5.0, 5.0, 5.0, 5.0};
        // The load loses its supply when the fuse opens
        Result.NodeVoltages[static_cast<std::size_t>(Nodes->GetNode({8, 0}).value_or(0))] = {4.7, 4.7, 0.0, 0.0};
        Result.Currents = {{"FU1", {2.0, 2.0, 5e-9, 5e-9}}};
    }

    std::optional<GUI::FuseDisplay> Show(GUI::FuseBlowTracker &tracker, const double instant, const double now) const {
        const GUI::ShownTransient shown = {.Data = Result, .DataVersion = 1, .Instant = instant};
        return tracker.Update(Elements, *Nodes, &shown, now)[FUSE_INDEX];
    }
};

} // namespace

TEST_CASE("A fuse seen blowing while the result plays flashes from that frame and fades on the screen clock",
          "[fuse_blow_tracker]") {
    const FuseCircuit circuit;
    GUI::FuseBlowTracker tracker;
    CHECK(circuit.Show(tracker, 0.1, 5.0) == GUI::FuseDisplay{.IsBlown = false, .FlashLevel = 0.0f});
    CHECK(circuit.Show(tracker, 0.25, 10.0) == GUI::FuseDisplay{.IsBlown = true, .FlashLevel = 1.0f});

    const std::optional<GUI::FuseDisplay> fading = circuit.Show(tracker, 0.3, 10.0 + GUI::FUSE_FLASH_SECONDS / 2.0);
    REQUIRE(fading.has_value());
    CHECK(fading->IsBlown);
    CHECK_THAT(fading->FlashLevel, Catch::Matchers::WithinAbs(0.5, 1e-6));
    CHECK(circuit.Show(tracker, 0.3, 11.0) == GUI::FuseDisplay{.IsBlown = true, .FlashLevel = 0.0f});
}

TEST_CASE("A result that arrives with a fuse already blown shows it open without a flash", "[fuse_blow_tracker]") {
    const FuseCircuit circuit;
    GUI::FuseBlowTracker tracker;
    CHECK(circuit.Show(tracker, 0.3, 10.0) == GUI::FuseDisplay{.IsBlown = true, .FlashLevel = 0.0f});
    CHECK(tracker.GetBlowTimes()[FUSE_INDEX] == 0.2);
}

TEST_CASE("Only the fuses have a display, and without a transient they are whole and their blow times forgotten",
          "[fuse_blow_tracker]") {
    const FuseCircuit circuit;
    GUI::FuseBlowTracker tracker;
    REQUIRE(circuit.Show(tracker, 0.3, 10.0).has_value());

    const std::vector<std::optional<GUI::FuseDisplay>> displays =
        tracker.Update(circuit.Elements, *circuit.Nodes, nullptr, 11.0);
    REQUIRE(displays.size() == circuit.Elements.size());
    CHECK_FALSE(displays[0].has_value());
    CHECK(displays[FUSE_INDEX] == GUI::FuseDisplay{.IsBlown = false, .FlashLevel = 0.0f});
    CHECK_FALSE(displays[2].has_value());
    CHECK(tracker.GetBlowTimes().empty());
    // Coming back to the transient with the fuse open does not flash, as the frame before showed no transient
    CHECK(circuit.Show(tracker, 0.3, 12.0) == GUI::FuseDisplay{.IsBlown = true, .FlashLevel = 0.0f});
}
