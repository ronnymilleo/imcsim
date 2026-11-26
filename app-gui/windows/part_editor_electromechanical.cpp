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

constexpr auto THYRISTOR_FIELDS = std::to_array<ParameterField<Core::ThyristorParameters>>({
    {"V0", &Core::ThyristorParameters::OnThresholdVoltage, "V"},
    {"RT", &Core::ThyristorParameters::OnResistance, "Ohm"},
    {"Off R", &Core::ThyristorParameters::OffResistance, "Ohm"},
    {"IH", &Core::ThyristorParameters::HoldingCurrent, "A"},
    {"IGT", &Core::ThyristorParameters::GateTriggerCurrent, "A"},
    {"VGT", &Core::ThyristorParameters::GateTriggerVoltage, "V"},
    {"RG", &Core::ThyristorParameters::GateResistance, "Ohm"},
    {"VBO", &Core::ThyristorParameters::BreakoverVoltage, "V"},
    {"dv/dt", &Core::ThyristorParameters::CriticalDvDt, "V/s"},
    {"tq", &Core::ThyristorParameters::TurnOffTime, "s"},
});

constexpr auto DIAC_FIELDS = std::to_array<ParameterField<Core::DiacParameters>>({
    {"VBO", &Core::DiacParameters::BreakoverVoltage, "V"},
    {"V0", &Core::DiacParameters::OnThresholdVoltage, "V"},
    {"RT", &Core::DiacParameters::OnResistance, "Ohm"},
    {"Off R", &Core::DiacParameters::OffResistance, "Ohm"},
    {"IH", &Core::DiacParameters::HoldingCurrent, "A"},
});

constexpr auto TRANSFORMER_FIELDS = std::to_array<ParameterField<Core::TransformerParameters>>({
    {"Turns ratio", &Core::TransformerParameters::TurnsRatio, ":1"},
    {"Coupling", &Core::TransformerParameters::Coupling, ""},
    {"Winding R", &Core::TransformerParameters::WindingResistance, "Ohm"},
});

constexpr auto TRANSMISSION_LINE_FIELDS = std::to_array<ParameterField<Core::TransmissionLineParameters>>({
    {"Delay", &Core::TransmissionLineParameters::Delay, "s"},
    {"DC R", &Core::TransmissionLineParameters::DcResistance, "Ohm"},
    {"Skin R", &Core::TransmissionLineParameters::SkinResistance, "Ohm"},
    {"Ref freq", &Core::TransmissionLineParameters::ReferenceFrequency, "Hz"},
    {"DC G", &Core::TransmissionLineParameters::DcConductance, "S"},
    {"Loss tangent", &Core::TransmissionLineParameters::LossTangent, ""},
});

} // namespace

/**
 * @brief   The parameters of the switches that follow a voltage, relays, fuses and transformers.
 */
void LoadElectromechanicalFields(ElectromechanicalFields &fields, const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::V_SWITCH) {
        LoadParameterFields(SWITCH_FIELDS,
                            fields.VoltageSwitch,
                            static_cast<const Core::VoltageControlledSwitch &>(component).GetParameters());
    } else if (type == Core::ComponentType::RELAY) {
        LoadParameterFields(RELAY_FIELDS, fields.Relay, static_cast<const Core::Relay &>(component).GetParameters());
    } else if (type == Core::ComponentType::FUSE) {
        LoadParameterFields(FUSE_FIELDS, fields.Fuse, static_cast<const Core::Fuse &>(component).GetParameters());
    } else if (type == Core::ComponentType::THYRISTOR || type == Core::ComponentType::TRIAC) {
        LoadParameterFields(THYRISTOR_FIELDS,
                            fields.Thyristor,
                            static_cast<const Core::Thyristor &>(component).GetParameters());
    } else if (type == Core::ComponentType::DIAC) {
        LoadParameterFields(DIAC_FIELDS, fields.Diac, static_cast<const Core::Diac &>(component).GetParameters());
    } else if (type == Core::ComponentType::TRANSFORMER) {
        LoadParameterFields(TRANSFORMER_FIELDS,
                            fields.Transformer,
                            static_cast<const Core::Transformer &>(component).GetParameters());
    } else if (type == Core::ComponentType::TRANSMISSION_LINE) {
        LoadParameterFields(TRANSMISSION_LINE_FIELDS,
                            fields.TransmissionLine,
                            static_cast<const Core::TransmissionLine &>(component).GetParameters());
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
void DrawControlledSwitch(Core::VoltageControlledSwitch &controlled,
                          ElectromechanicalFields &fields,
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
 * @brief   The thyristor fires at the gate trigger current, latches on, and releases below the holding current; a
 *          TRIAC does so both ways, firing in all four gate quadrants.
 */
void DrawThyristor(Core::Thyristor &thyristor, ElectromechanicalFields &fields, Schematic &schematic) {
    Core::ThyristorParameters parameters = thyristor.GetParameters();
    const auto is_valid = [](const Core::ThyristorParameters &candidate) {
        return Core::Thyristor::IsValidParameters(candidate);
    };
    if (DrawParameterFields(THYRISTOR_FIELDS, fields.Thyristor, "", is_valid, parameters)) {
        thyristor.SetParameters(parameters);
        schematic.MarkModified();
    }
    const bool is_triac = thyristor.GetType() == Core::ComponentType::TRIAC;
    ImGui::TextDisabled("Fires at IGT, latches on, and releases when the %s current falls below IH",
                        is_triac ? "main" : "anode");
    if (is_triac) {
        ImGui::TextDisabled("Conducts both ways and fires in all four gate quadrants; the gate is referenced to MT1");
    }
    ImGui::TextDisabled("On Nodalys every figure is a parameter; on ngspice only RG and RT are, and IH, IGT and V0"
                        " follow from the transistor pair");
    ImGui::TextDisabled("VBO, dv/dt firing and the turn-off time tq run on Nodalys only; 0 disables each");
}

/**
 * @brief   The DIAC breaks over at VBO either way, snaps back to the on-state line from V0 and releases below IH.
 */
void DrawDiac(Core::Diac &diac, ElectromechanicalFields &fields, Schematic &schematic) {
    Core::DiacParameters parameters = diac.GetParameters();
    const auto is_valid = [](const Core::DiacParameters &candidate) {
        return Core::Diac::IsValidParameters(candidate);
    };
    if (DrawParameterFields(DIAC_FIELDS, fields.Diac, "", is_valid, parameters)) {
        diac.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Breaks over at VBO either way and snaps back by %sV; releases below IH",
                        Core::FormatValue(parameters.BreakoverVoltage - parameters.OnThresholdVoltage).c_str());
    ImGui::TextDisabled("V0 plus RT times IH must stay below VBO");
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

/**
 * @brief   The value is the characteristic impedance Z0; zero losses run in transient (Bergeron) and frequency domain,
 *          while skin/dielectric losses run in frequency-domain and Harmonic Balance analyses. The geometry calculator
 *          sets Z0 and every parameter at once from a real line.
 * @param[in,out] line       Line being edited.
 * @param[in,out] fields     Texts of its parameters and the calculator.
 * @param[in,out] impedance  Field of the value, reloaded when the calculator sets Z0.
 * @param[in,out] schematic  Marked modified on a change.
 */
void DrawTransmissionLine(Core::TransmissionLine &line,
                          ElectromechanicalFields &fields,
                          ValueField &impedance,
                          Schematic &schematic) {
    Core::TransmissionLineParameters parameters = line.GetParameters();
    const auto is_valid = [](const Core::TransmissionLineParameters &candidate) {
        return Core::TransmissionLine::IsValidParameters(candidate);
    };
    if (DrawParameterFields(TRANSMISSION_LINE_FIELDS, fields.TransmissionLine, "", is_valid, parameters)) {
        line.SetParameters(parameters);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Value is Z0; quarter-wave at %sHz (Nodalys backend)",
                        Core::FormatValue(line.GetQuarterWaveFrequency()).c_str());
    if (line.GetParameters().IsLossy()) {
        ImGui::TextDisabled("Lossy/dispersive: runs in DC, AC, S-parameters, noise and HB (not transient)");
    } else {
        ImGui::TextDisabled("Lossless (0 losses): runs in transient (Bergeron) and all frequency analyses");
    }
    if (ImGui::Button("From geometry...")) {
        OpenLineGeometryCalculator(fields.LineGeometry);
    }
    if (const std::optional<Core::LineElectrical> figures = DrawLineGeometryCalculator(fields.LineGeometry)) {
        line.SetValue(figures->CharacteristicImpedance);
        line.SetParameters(figures->Parameters);
        impedance.Load(figures->CharacteristicImpedance);
        LoadParameterFields(TRANSMISSION_LINE_FIELDS, fields.TransmissionLine, figures->Parameters);
        schematic.MarkModified();
    }
}

} // namespace GUI
