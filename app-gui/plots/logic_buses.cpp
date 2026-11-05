/**
 * @file    logic_buses.cpp
 * @brief   The panel of the logic buses beside the plots, where they are added, edited and shown.
 */

#include "plots/logic_buses.h"

#include "drawing/theme.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

namespace GUI {

namespace {

constexpr auto BUS_FORMATS = std::to_array({BusFormat::BINARY, BusFormat::HEXADECIMAL, BusFormat::DECIMAL});
/// Width of the name field and of the bit picker, in font sizes.
constexpr float EDITOR_FIELD_WIDTH = 10.0f;
/// Bit rows the editor shows before the list scrolls.
constexpr float MAX_VISIBLE_BITS = 8.0f;

/**
 * @brief   Moves the current popup up when its bottom falls off its viewport.
 * @details The editor opens from the foot of the trace list and grows as bits are added, so without this the rows
 *          added last, and the button that removes the bus, would end up below the window.
 */
void KeepPopupInsideViewport() {
    const ImGuiViewport *viewport = ImGui::GetWindowViewport();
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y;
    const ImVec2 position = ImGui::GetWindowPos();
    const float overflow = position.y + ImGui::GetWindowSize().y - bottom;
    if (overflow > 0.0f) {
        ImGui::SetWindowPos({position.x, std::max(viewport->WorkPos.y, position.y - overflow)});
    }
}

} // namespace

/**
 * @brief   Creates the panel of the buses of a schematic.
 * @param[in,out] schematic  Schematic that keeps the buses; it must outlive the panel.
 */
LogicBuses::LogicBuses(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the list of buses: each shows its check box, which shows or hides its lane, and an Edit button. Add
 *          creates an empty bus and opens its editor right away, where its bits are picked.
 * @param[in] signals  The voltages a bit can read, such as "V(Q0)", in the order the trace list shows them.
 */
void LogicBuses::Draw(const std::vector<std::string> &signals) {
    ImGui::TextDisabled("Buses");
    std::vector<LogicBus> buses = m_Schematic.GetBuses();
    bool open_new_editor = false;
    if (ImGui::SmallButton("Add##bus")) {
        buses.push_back({.Name = GetNextBusName(buses)});
        open_new_editor = true;
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < buses.size(); ++index) {
        LogicBus &bus = buses[index];
        ImGui::PushID(static_cast<int>(index));
        // Edit comes first, so a long name is what the narrow list clips, not the button
        ImGui::AlignTextToFramePadding();
        const bool is_edit_pressed = ImGui::SmallButton("Edit");
        ImGui::SameLine();
        ImGui::Checkbox(bus.Name.c_str(), &bus.Shown);
        ImGui::SetItemTooltip("%s", DescribeBus(bus).c_str());
        if (is_edit_pressed || (open_new_editor && index + 1 == buses.size())) {
            ImGui::OpenPopup("bus_editor");
        }
        if (ImGui::BeginPopup("bus_editor")) {
            if (!DrawEditor(bus, signals)) {
                removed = index;
            }
            KeepPopupInsideViewport();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (removed) {
        buses.erase(buses.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
    if (buses != m_Schematic.GetBuses()) {
        m_Schematic.SetBuses(std::move(buses));
    }
}

/**
 * @brief   The name, the format and the bits of a bus, the most significant first, each of which moves up or down or
 *          goes; a list adds the signals it does not have yet.
 * @details Returns false when the bus is removed.
 */
bool LogicBuses::DrawEditor(LogicBus &bus, const std::vector<std::string> &signals) const {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * EDITOR_FIELD_WIDTH);
    std::string name = bus.Name;
    // A name another bus has, or none, is not taken, so the list never shows two alike
    if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_CharsNoBlank) && !name.empty() &&
        !std::ranges::contains(m_Schematic.GetBuses(), name, &LogicBus::Name)) {
        bus.Name = name;
    }
    for (std::size_t index = 0; index < BUS_FORMATS.size(); ++index) {
        if (index > 0) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(GetBusFormatName(BUS_FORMATS[index]), bus.Format == BUS_FORMATS[index])) {
            bus.Format = BUS_FORMATS[index];
        }
    }
    ImGui::SeparatorText("Bits, most significant first");
    DrawBitRows(bus);
    DrawAddBit(bus, signals);
    ImGui::Separator();
    if (ImGui::Button("Remove bus")) {
        ImGui::CloseCurrentPopup();
        return false;
    }
    return true;
}

/**
 * @brief   One row per bit, with its weight, its signal and buttons to move it or take it out.
 */
void LogicBuses::DrawBitRows(LogicBus &bus) {
    if (bus.Bits.empty()) {
        ImGui::TextDisabled("Add the signals of its bits");
        return;
    }
    std::optional<std::pair<std::size_t, std::size_t>> swapped;
    std::optional<std::size_t> removed;
    // A long bus scrolls, so the editor stays small enough for a popup
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f}, {FLT_MAX, ImGui::GetFrameHeightWithSpacing() * MAX_VISIBLE_BITS});
    ImGui::BeginChild("bits", {0.0f, 0.0f}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    for (std::size_t index = 0; index < bus.Bits.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::Text("%zu  %s", bus.Bits.size() - 1 - index, bus.Bits[index].c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0);
        if (ImGui::SmallButton("Up")) {
            swapped = {index - 1, index};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1 == bus.Bits.size());
        if (ImGui::SmallButton("Down")) {
            swapped = {index, index + 1};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removed = index;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (swapped) {
        std::swap(bus.Bits[swapped->first], bus.Bits[swapped->second]);
    }
    if (removed) {
        bus.Bits.erase(bus.Bits.begin() + static_cast<std::ptrdiff_t>(*removed));
    }
}

/**
 * @brief   A list of the signals the bus does not have yet; the one picked becomes its least significant bit.
 */
void LogicBuses::DrawAddBit(LogicBus &bus, const std::vector<std::string> &signals) {
    ImGui::BeginDisabled(bus.Bits.size() >= MAX_BUS_BITS);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * EDITOR_FIELD_WIDTH);
    if (ImGui::BeginCombo("##add_bit", "Add a bit")) {
        for (const std::string &signal : signals) {
            if (!std::ranges::contains(bus.Bits, signal) && ImGui::Selectable(signal.c_str())) {
                bus.Bits.push_back(signal);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

/**
 * @brief   Describes a bus in one line, as its tooltip shows it.
 * @param[in] bus  The bus.
 * @return  Such as "V(Q2) V(Q1) V(Q0), hex", the most significant bit first; "no bits, hex" for an empty one.
 */
std::string DescribeBus(const LogicBus &bus) {
    std::string bits;
    for (const std::string &bit : bus.Bits) {
        bits += bits.empty() ? bit : " " + bit;
    }
    return std::format("{}, {}", bits.empty() ? "no bits" : bits, GetBusFormatName(bus.Format));
}

} // namespace GUI
