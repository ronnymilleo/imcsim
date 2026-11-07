/**
 * @file    part_catalog.cpp
 * @brief   How the placeable parts are grouped on the part rail, the one list the rail and the part search share.
 */

#include "editing/part_catalog.h"

#include <array>

namespace GUI {

namespace {

using Core::ComponentType;

// Parts with a button of their own
constexpr auto RESISTOR = std::to_array({ComponentType::RESISTOR});
constexpr auto CAPACITOR = std::to_array({ComponentType::CAPACITOR});
constexpr auto INDUCTOR = std::to_array({ComponentType::INDUCTOR});
constexpr auto GROUND = std::to_array({ComponentType::GROUND});
constexpr auto OP_AMP = std::to_array({ComponentType::OP_AMP});

// Groups, the first part being the one a group places until another is picked
constexpr auto SOURCES =
    std::to_array({ComponentType::VOLTAGE_SOURCE, ComponentType::CURRENT_SOURCE, ComponentType::CLOCK});
constexpr auto SUPPLIES = std::to_array({ComponentType::VCC, ComponentType::NET_LABEL, ComponentType::PIN});
constexpr auto DIODES = std::to_array({ComponentType::DIODE, ComponentType::ZENER_DIODE, ComponentType::LED});
constexpr auto TRANSISTORS =
    std::to_array({ComponentType::NPN, ComponentType::PNP, ComponentType::NMOS, ComponentType::PMOS});
constexpr auto CONTROLLED =
    std::to_array({ComponentType::VCVS, ComponentType::VCCS, ComponentType::CCCS, ComponentType::CCVS,
                   ComponentType::BEHAVIORAL_VOLTAGE, ComponentType::BEHAVIORAL_CURRENT});
constexpr auto ADJUSTABLE = std::to_array({ComponentType::POTENTIOMETER, ComponentType::TRANSFORMER});
constexpr auto SWITCHES = std::to_array({ComponentType::SWITCH, ComponentType::PUSH_BUTTON, ComponentType::V_SWITCH,
                                         ComponentType::RELAY, ComponentType::FUSE});
constexpr auto LOGIC_IO =
    std::to_array({ComponentType::LOGIC_INPUT, ComponentType::LOGIC_OUTPUT, ComponentType::SEVEN_SEGMENT});
constexpr auto GATES =
    std::to_array({ComponentType::AND, ComponentType::OR, ComponentType::NOT, ComponentType::NAND, ComponentType::NOR,
                   ComponentType::XOR, ComponentType::XNOR, ComponentType::BUFFER});
constexpr auto FLIP_FLOPS = std::to_array(
    {ComponentType::D_FLIP_FLOP, ComponentType::JK_FLIP_FLOP, ComponentType::T_FLIP_FLOP, ComponentType::SR_FLIP_FLOP});

// Sections of the rail, which a separator divides
const auto PASSIVE_SECTION = std::to_array<RailEntry>({
    {.Name = "Resistor", .Parts = RESISTOR},
    {.Name = "Capacitor", .Parts = CAPACITOR},
    {.Name = "Inductor", .Parts = INDUCTOR},
});
const auto SOURCE_SECTION = std::to_array<RailEntry>({
    {.Name = "Ground", .Parts = GROUND},
    {.Name = "Sources", .Parts = SOURCES},
    {.Name = "Supplies and labels", .Parts = SUPPLIES},
});
const auto ACTIVE_SECTION = std::to_array<RailEntry>({
    {.Name = "Diodes", .Parts = DIODES},
    {.Name = "Transistors", .Parts = TRANSISTORS},
    {.Name = "Op-amp", .Parts = OP_AMP},
    {.Name = "Controlled sources", .Parts = CONTROLLED},
});
const auto CONTROL_SECTION = std::to_array<RailEntry>({
    {.Name = "Adjustable parts", .Parts = ADJUSTABLE},
    {.Name = "Switches and fuses", .Parts = SWITCHES},
});
const auto LOGIC_SECTION = std::to_array<RailEntry>({
    {.Name = "Logic inputs and outputs", .Parts = LOGIC_IO},
    {.Name = "Logic gates", .Parts = GATES},
    {.Name = "Flip-flops", .Parts = FLIP_FLOPS},
    {.Name = "Logic blocks", .Parts = {}, .IsReadyBlocks = true},
});
const auto SECTIONS = std::to_array<std::span<const RailEntry>>(
    {PASSIVE_SECTION, SOURCE_SECTION, ACTIVE_SECTION, CONTROL_SECTION, LOGIC_SECTION});

} // namespace

/**
 * @brief   Tells a button with a list from a button of one part.
 * @return  True for a group of parts and for the ready blocks, whose buttons open a list.
 */
bool RailEntry::IsGroup() const {
    return IsReadyBlocks || Parts.size() > 1;
}

/**
 * @brief   The buttons of the part rail, top to bottom, in sections a separator divides: passive parts, ground and
 *          sources, semiconductors and amplifiers, parts set by hand, and logic.
 * @return  Every section, each a list of buttons; together they hold every part the part search finds.
 */
std::span<const std::span<const RailEntry>> GetRailSections() {
    return SECTIONS;
}

} // namespace GUI
