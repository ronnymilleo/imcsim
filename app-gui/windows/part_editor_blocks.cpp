/**
 * @file    part_editor_blocks.cpp
 * @brief   The properties of net labels, pin markers and ports, and of the instances of blocks.
 */

#include "windows/part_editor_blocks.h"

#include "base/theme.h"
#include "schematic/subcircuit_pins.h"
#include "windows/part_fields.h"
#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace GUI {

namespace {

/**
 * @brief   One field per parameter of the block, showing the override of this instance or the default.
 */
void LoadInstanceFields(BlockFields &fields, const Core::SubcircuitInstance &instance, const Schematic &schematic) {
    const auto definition =
        std::ranges::find(schematic.GetSubcircuits(), instance.GetDefinitionName(), &SavedSubcircuit::Name);
    fields.Instance.clear();
    if (definition == schematic.GetSubcircuits().end()) {
        return;
    }
    fields.Instance.resize(definition->Parameters.size());
    for (std::size_t index = 0; index < definition->Parameters.size(); ++index) {
        const Core::SubcircuitParameter &parameter = definition->Parameters[index];
        const auto override_value = instance.GetParameters().find(parameter.Name);
        fields.Instance[index].Load(override_value != instance.GetParameters().end() ? override_value->second
                                                                                     : parameter.DefaultValue);
    }
}

/**
 * @brief   Whether the marker is a port of the S-parameter analysis, with its number and reference impedance.
 * @details Turning it on takes the lowest number no other port has. Only the markers of the top-level schematic count;
 *          inside a block they are ignored. A port holds its node at 0 V in DC, so it belongs behind a capacitor where
 *          the node is biased.
 */
void DrawPortSettings(Core::SubcircuitPinMarker &marker, ValueField &port_reference, Schematic &schematic) {
    Core::PortSettings port = marker.GetPort();
    ImGui::Separator();
    if (ImGui::Checkbox("S-parameter port", &port.IsPort)) {
        if (port.IsPort) {
            port.Number = FindFreePortNumber(schematic.GetElements(), &marker);
        }
        marker.SetPort(port);
        schematic.MarkModified();
    }
    ImGui::SetItemTooltip("Makes this marker a port of the S-parameter analysis, with a source and a reference "
                          "impedance at its node. The port holds the node at 0 V in DC and loads the circuit with its "
                          "impedance, so couple it through a capacitor where the node has a bias. It counts only in "
                          "the top-level schematic and is invisible in every other analysis");
    if (!port.IsPort) {
        return;
    }
    DrawFieldLabel("Number");
    if (ImGui::InputInt("##port_number", &port.Number, 0)) {
        port.Number = std::max(port.Number, 1);
        marker.SetPort(port);
        schematic.MarkModified();
    }
    if (IsPortNumberTaken(schematic.GetElements(), port.Number, &marker)) {
        ImGui::TextColored(GetErrorTextColor(), "Port %d is already used by another marker", port.Number);
    }
    if (const std::optional<double> reference =
            port_reference.Draw("Z0", "Ohm", [](const double value) { return value > 0.0; })) {
        port.ReferenceOhms = *reference;
        marker.SetPort(port);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Ports are numbered 1 to N with no gap; the analysis needs two or more");
}

} // namespace

/**
 * @brief   The net name of a label or pin marker, the reference impedance of a port, and one field per parameter of a
 *          block instance.
 */
void LoadBlockFields(BlockFields &fields, const Core::Component &component, const Schematic &schematic) {
    if (Core::IsSubcircuitPin(component.GetType())) {
        fields.PortReference.Load(static_cast<const Core::SubcircuitPinMarker &>(component).GetPort().ReferenceOhms);
    }
    if (component.GetType() == Core::ComponentType::NET_LABEL || Core::IsSubcircuitPin(component.GetType())) {
        const std::string &net = static_cast<const Core::NetLabel &>(component).GetNet();
        fields.Net.fill('\0');
        net.copy(fields.Net.data(), fields.Net.size() - 1);
    }
    if (component.GetType() == Core::ComponentType::SUBCIRCUIT) {
        LoadInstanceFields(fields, static_cast<const Core::SubcircuitInstance &>(component), schematic);
    }
}

/**
 * @brief   The name applies as soon as it is valid, or empty, which leaves the label or marker unnamed.
 */
void DrawNetLabel(Core::NetLabel &label, BlockFields &fields, Schematic &schematic) {
    const bool is_pin = Core::IsSubcircuitPin(label.GetType());
    DrawFieldLabel(is_pin ? "Pin" : "Net");
    if (std::exchange(fields.IsNetFocusRequested, false)) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool changed = ImGui::InputText("##net", fields.Net.data(), fields.Net.size());
    const std::string typed = fields.Net.data();
    const bool valid = typed.empty() || (is_pin ? Core::SubcircuitInstance::IsValidIdentifier(typed)
                                                : Core::NetLabel::IsValidNet(typed));
    if (changed && valid && typed != label.GetNet()) {
        label.SetNet(typed);
        schematic.MarkModified();
    }
    if (!valid) {
        ImGui::TextColored(GetErrorTextColor(), "%s",
                           is_pin ? "Use a letter or _, then letters, digits or _"
                                  : "1 to 32 letters, digits, _, - or .");
    } else if (is_pin) {
        ImGui::TextDisabled("The arrow points from the block's edge into the circuit; turn it to pick the edge");
        DrawPortSettings(static_cast<Core::SubcircuitPinMarker &>(label), fields.PortReference, schematic);
    } else {
        ImGui::TextDisabled("Labels with the same name are one net; 0 and GND are ground, VCC is the supplies");
    }
}

bool DrawSubcircuit(Core::SubcircuitInstance &instance, BlockFields &fields, Schematic &schematic) {
    bool is_reset = false;
    const auto definition =
        std::ranges::find(schematic.GetSubcircuits(), instance.GetDefinitionName(), &SavedSubcircuit::Name);
    if (definition == schematic.GetSubcircuits().end()) {
        ImGui::TextColored(GetErrorTextColor(), "Unknown definition: %s", instance.GetDefinitionName().c_str());
        return false;
    }
    ImGui::TextUnformatted(std::format("Definition: {}", definition->Name).c_str());
    if (definition->Spice) {
        ImGui::TextDisabled("SPICE text from a library; double click the block for its pins");
    }
    for (std::size_t index = 0; index < definition->Parameters.size() && index < fields.Instance.size(); ++index) {
        const Core::SubcircuitParameter &parameter = definition->Parameters[index];
        if (const auto value = fields.Instance[index].Draw(parameter.Name.c_str(), "", AnyValue)) {
            instance.SetParameter(parameter.Name, *value);
            schematic.MarkModified();
        }
    }
    if (ImGui::Button("Reset parameters")) {
        instance.ClearParameters();
        schematic.MarkModified();
        is_reset = true;
    }
    ImGui::TextDisabled("Values shown are overrides or the definition defaults");
    return is_reset;
}

} // namespace GUI
