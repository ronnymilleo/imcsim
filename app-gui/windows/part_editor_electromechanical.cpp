/**
 * @file    part_editor_electromechanical.cpp
 * @brief   The properties of switches, relays, fuses and transformers.
 */

#include "windows/part_editor_electromechanical.h"

#include "spice/spice_value.h"
#include "windows/part_fields.h"
#include <algorithm>
#include <span>

namespace GUI {

namespace {

constexpr auto SWITCH_FIELDS = std::to_array<ParameterField<Core::SwitchParameters>>({
    {"Threshold", &Core::SwitchParameters::Threshold, "V"},
    {"Hysteresis", &Core::SwitchParameters::Hysteresis, "V"},
    {"On resistance", &Core::SwitchParameters::OnResistance, "Ohm"},
    {"Off resistance", &Core::SwitchParameters::OffResistance, "Ohm"},
});

constexpr auto RELAY_FIELDS = std::to_array<ParameterField<Core::RelayParameters>>({
    {"Coil R", &Core::RelayParameters::CoilResistance, "Ohm"},
    {"Coil L", &Core::RelayParameters::CoilInductance, "H"},
    {"Pull-in", &Core::RelayParameters::PullInVoltage, "V"},
    {"Drop-out", &Core::RelayParameters::DropOutVoltage, "V"},
    {"Contact R", &Core::RelayParameters::ContactResistance, "Ohm"},
});

constexpr auto FUSE_FIELDS = std::to_array<ParameterField<Core::FuseParameters>>({
    {"Rated current", &Core::FuseParameters::RatedCurrent, "A"},
    {"Melting I2t", &Core::FuseParameters::MeltingI2t, "A2s"},
    {"Cold R", &Core::FuseParameters::ColdResistance, "Ohm"},
    {"Blown R", &Core::FuseParameters::BlownResistance, "Ohm"},
});

constexpr auto TRANSFORMER_FIELDS = std::to_array<ParameterField<Core::TransformerParameters>>({
    {"Turns ratio", &Core::TransformerParameters::TurnsRatio, ":1"},
    {"Coupling", &Core::TransformerParameters::Coupling, ""},
    {"Winding R", &Core::TransformerParameters::WindingResistance, "Ohm"},
});

} // namespace

/**
 * @brief   The parameters of the switches that follow a voltage, relays, fuses and transformers.
 */
void LoadElectromechanicalFields(ElectromechanicalFields &fields, const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::V_SWITCH) {
        LoadParameterFields(SWITCH_FIELDS, fields.VoltageSwitch,
                            static_cast<const Core::VoltageControlledSwitch &>(component).GetParameters());
    } else if (type == Core::ComponentType::RELAY) {
        LoadParameterFields(RELAY_FIELDS, fields.Relay, static_cast<const Core::Relay &>(component).GetParameters());
    } else if (type == Core::ComponentType::FUSE) {
        LoadParameterFields(FUSE_FIELDS, fields.Fuse, static_cast<const Core::Fuse &>(component).GetParameters());
    } else if (type == Core::ComponentType::TRANSFORMER) {
        LoadParameterFields(TRANSFORMER_FIELDS, fields.Transformer,
                            static_cast<const Core::Transformer &>(component).GetParameters());
    }
}

/**
 * @brief   A closed switch conducts; a push button is pressed when closed.
 */
void DrawSwitch(Core::Switch &contact, Schematic &schematic) {
    bool closed = contact.IsClosed();
    const bool is_button = contact.GetType() == Core::ComponentType::PUSH_BUTTON;
    if (ImGui::Checkbox(is_button ? "Pressed" : "Closed", &closed)) {
        contact.SetClosed(closed);
        schematic.MarkLiveChanged(contact);
    }
    ImGui::TextDisabled("It is a %s while the circuit is simulated", closed ? "wire" : "gap");
}

/**
 * @brief   The switch closes above the threshold plus the hysteresis, and opens below the threshold minus it.
 */
void DrawControlledSwitch(Core::VoltageControlledSwitch &controlled, ElectromechanicalFields &fields,
                          Schematic &schematic) {
    Core::SwitchParameters parameters = controlled.GetParameters();
    const auto is_valid = [](const Core::SwitchParameters &candidate) {
        return Core::VoltageControlledSwitch::IsValidParameters(candidate);
    };
    if (DrawParameterFields(SWITCH_FIELDS, fields.VoltageSwitch, "", is_valid, parameters)) {
        controlled.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Closes above threshold + hysteresis, opens below threshold - hysteresis");
}

/**
 * @brief   The contact closes when the coil voltage reaches the pull-in value and opens when it falls to the drop-out
 *          one.
 */
void DrawRelay(Core::Relay &relay, ElectromechanicalFields &fields, Schematic &schematic) {
    Core::RelayParameters parameters = relay.GetParameters();
    const auto is_valid = [](const Core::RelayParameters &candidate) {
        return Core::Relay::IsValidParameters(candidate);
    };
    if (DrawParameterFields(RELAY_FIELDS, fields.Relay, "", is_valid, parameters)) {
        relay.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("The contact follows the coil current, so it lags by L over R");
}

/**
 * @brief   A combo of ready fuses fills the figures, which stay editable, and the cooling time constant that they
 *          imply is shown read-only.
 */
void DrawFuse(Core::Fuse &fuse, ElectromechanicalFields &fields, Schematic &schematic) {
    Core::FuseParameters parameters = fuse.GetParameters();
    const std::span<const Core::ReadyFuse> ready_fuses = Core::GetReadyFuses();
    const auto matching = std::ranges::find(ready_fuses, parameters, &Core::ReadyFuse::Parameters);
    const Core::ReadyFuse *selected = matching != ready_fuses.end() ? &*matching : nullptr;
    DrawFieldLabel("Ready fuse");
    if (ImGui::BeginCombo("##ReadyFuse", selected != nullptr ? selected->Name : "Custom")) {
        for (const Core::ReadyFuse &ready : ready_fuses) {
            const bool is_selected = &ready == selected;
            if (ImGui::Selectable(ready.Name, is_selected) && !is_selected) {
                fuse.SetParameters(ready.Parameters);
                LoadParameterFields(FUSE_FIELDS, fields.Fuse, ready.Parameters);
                schematic.MarkModified();
            }
        }
        ImGui::EndCombo();
    }
    parameters = fuse.GetParameters();
    const auto is_valid = [](const Core::FuseParameters &candidate) {
        return Core::Fuse::IsValidParameters(candidate);
    };
    if (DrawParameterFields(FUSE_FIELDS, fields.Fuse, "", is_valid, parameters)) {
        fuse.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Cooling time constant: %ss",
                        Core::FormatValue(Core::GetCoolingTimeConstant(parameters)).c_str());
    ImGui::TextDisabled("Never blows below the rated current; blows open for the rest of the run, and starts whole");
}

/**
 * @brief   The value is the primary inductance; the secondary's follows from the turns ratio.
 */
void DrawTransformer(Core::Transformer &transformer, ElectromechanicalFields &fields, Schematic &schematic) {
    Core::TransformerParameters parameters = transformer.GetParameters();
    const auto is_valid = [](const Core::TransformerParameters &candidate) {
        return Core::Transformer::IsValidParameters(candidate);
    };
    if (DrawParameterFields(TRANSFORMER_FIELDS, fields.Transformer, "", is_valid, parameters)) {
        transformer.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Value is the primary inductance; the secondary is %s",
                        Core::FormatValue(transformer.GetSecondaryInductance()).c_str());
}

} // namespace GUI
