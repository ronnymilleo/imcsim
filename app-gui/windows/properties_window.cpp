/**
 * @file    properties_window.cpp
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#include "properties_window.h"

#include "simulator.h"
#include "spice_value.h"
#include "theme.h"
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace GUI {

namespace {

bool AnyValue(double /*value*/) {
    return true;
}

/**
 * @struct  ParameterField
 * @brief   One member of a parameter set, such as Core::PulseParameters, as a field of the Properties window.
 */
template <typename Parameters> struct ParameterField {
    const char *Label;
    double Parameters::*Value;
    // No unit means the unit of the source, volts or amperes
    const char *Unit;
};

constexpr auto ACFields = std::to_array<ParameterField<Core::ACParameters>>({
    {"Amplitude", &Core::ACParameters::Amplitude, nullptr},
    {"Frequency", &Core::ACParameters::Frequency, "Hz"},
    {"Offset", &Core::ACParameters::Offset, nullptr},
    {"AC magnitude", &Core::ACParameters::Magnitude, nullptr},
});

constexpr auto PulseFields = std::to_array<ParameterField<Core::PulseParameters>>({
    {"Low", &Core::PulseParameters::Low, nullptr},
    {"High", &Core::PulseParameters::High, nullptr},
    {"Delay", &Core::PulseParameters::Delay, "s"},
    {"Rise", &Core::PulseParameters::RiseTime, "s"},
    {"Fall", &Core::PulseParameters::FallTime, "s"},
    {"Width", &Core::PulseParameters::Width, "s"},
    {"Period", &Core::PulseParameters::Period, "s"},
});

// Labeled with the SPICE parameter names, as datasheet models list them
constexpr auto DiodeFields = std::to_array<ParameterField<Core::DiodeParameters>>({
    {"IS", &Core::DiodeParameters::SaturationCurrent, "A"},
    {"N", &Core::DiodeParameters::EmissionCoefficient, ""},
    {"RS", &Core::DiodeParameters::SeriesResistance, "Ohm"},
    {"BV", &Core::DiodeParameters::BreakdownVoltage, "V"},
    {"IBV", &Core::DiodeParameters::BreakdownCurrent, "A"},
    {"CJO", &Core::DiodeParameters::JunctionCapacitance, "F"},
    {"VJ", &Core::DiodeParameters::JunctionPotential, "V"},
    {"M", &Core::DiodeParameters::GradingCoefficient, ""},
    {"TT", &Core::DiodeParameters::TransitTime, "s"},
});

constexpr auto BJTFields = std::to_array<ParameterField<Core::BJTParameters>>({
    {"IS", &Core::BJTParameters::SaturationCurrent, "A"},
    {"BF", &Core::BJTParameters::ForwardBeta, ""},
    {"BR", &Core::BJTParameters::ReverseBeta, ""},
    {"VAF", &Core::BJTParameters::EarlyVoltage, "V"},
    {"IKF", &Core::BJTParameters::ForwardKneeCurrent, "A"},
    {"ISE", &Core::BJTParameters::LeakageSaturationCurrent, "A"},
    {"NE", &Core::BJTParameters::LeakageEmissionCoefficient, ""},
    {"RB", &Core::BJTParameters::BaseResistance, "Ohm"},
    {"RC", &Core::BJTParameters::CollectorResistance, "Ohm"},
    {"RE", &Core::BJTParameters::EmitterResistance, "Ohm"},
    {"CJE", &Core::BJTParameters::EmitterCapacitance, "F"},
    {"CJC", &Core::BJTParameters::CollectorCapacitance, "F"},
    {"TF", &Core::BJTParameters::TransitTime, "s"},
    {"VCEO", &Core::BJTParameters::MaxCollectorEmitterVoltage, "V"},
    {"IC max", &Core::BJTParameters::MaxCollectorCurrent, "A"},
    {"P max", &Core::BJTParameters::MaxPower, "W"},
});

constexpr auto MOSFETFields = std::to_array<ParameterField<Core::MOSFETParameters>>({
    {"VTO", &Core::MOSFETParameters::ThresholdVoltage, "V"},
    {"KP", &Core::MOSFETParameters::Transconductance, "A/V2"},
    {"LAMBDA", &Core::MOSFETParameters::ChannelModulation, "1/V"},
    {"RD", &Core::MOSFETParameters::DrainResistance, "Ohm"},
    {"RS", &Core::MOSFETParameters::SourceResistance, "Ohm"},
    {"CGSO", &Core::MOSFETParameters::GateSourceOverlap, "F/m"},
    {"CGDO", &Core::MOSFETParameters::GateDrainOverlap, "F/m"},
    {"W", &Core::MOSFETParameters::Width, "m"},
    {"L", &Core::MOSFETParameters::Length, "m"},
    {"VDS max", &Core::MOSFETParameters::MaxDrainSourceVoltage, "V"},
    {"VGS max", &Core::MOSFETParameters::MaxGateSourceVoltage, "V"},
    {"ID max", &Core::MOSFETParameters::MaxDrainCurrent, "A"},
    {"P max", &Core::MOSFETParameters::MaxPower, "W"},
});

// Datasheet specifications, from which the macromodel is built
constexpr auto OpAmpFields = std::to_array<ParameterField<Core::OpAmpParameters>>({
    {"Avol", &Core::OpAmpParameters::OpenLoopGainDecibels, "dB"},
    {"GBW", &Core::OpAmpParameters::GainBandwidth, "Hz"},
    {"Slew rate", &Core::OpAmpParameters::SlewRate, "V/us"},
    {"Phase margin", &Core::OpAmpParameters::PhaseMarginDegrees, "deg"},
    {"IB", &Core::OpAmpParameters::InputBiasCurrent, "A"},
    {"CMRR", &Core::OpAmpParameters::CommonModeRejectionDecibels, "dB"},
    {"Rout", &Core::OpAmpParameters::OutputResistance, "Ohm"},
    {"Isc", &Core::OpAmpParameters::ShortCircuitCurrent, "A"},
    {"V+ headroom", &Core::OpAmpParameters::PositiveHeadroom, "V"},
    {"V- headroom", &Core::OpAmpParameters::NegativeHeadroom, "V"},
    {"Supply current", &Core::OpAmpParameters::SupplyCurrent, "A"},
});

template <typename Parameters, std::size_t Count>
void LoadParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const Parameters &parameters) {
    for (std::size_t index = 0; index < Count; ++index) {
        value_fields[index].Load(parameters.*fields[index].Value);
    }
}

// Draws one field per member and applies a valid edit to the parameters; each value is checked by trying it in a
// copy of the whole set, since some limits depend on several members. Returns whether a value changed
template <typename Parameters, std::size_t Count>
bool DrawParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const char *source_unit, const auto &is_valid,
                         Parameters &parameters) {
    bool changed = false;
    for (std::size_t index = 0; index < Count; ++index) {
        const ParameterField<Parameters> &field = fields[index];
        const auto is_valid_value = [&](const double value) {
            Parameters candidate = parameters;
            candidate.*field.Value = value;
            return is_valid(candidate);
        };
        const char *unit = field.Unit != nullptr ? field.Unit : source_unit;
        if (const std::optional<double> value = value_fields[index].Draw(field.Label, unit, is_valid_value)) {
            parameters.*field.Value = *value;
            changed = true;
        }
    }
    return changed;
}

// Draws the model combo of a part that offers ready models or custom parameters, its description or its custom
// fields, and applies a change; returns whether the part changed. Custom starts from the parameters of the model it
// replaces, so a ready part can be tweaked
template <typename Part, typename Model, typename Parameters, std::size_t Count>
bool DrawModelChoice(Part &part, const std::span<const Model> models, const char *custom_name,
                     const std::array<ParameterField<Parameters>, Count> &fields,
                     std::array<ValueField, Count> &value_fields) {
    bool changed = false;
    DrawFieldLabel("Model");
    if (ImGui::BeginCombo("##Model", part.GetModelName())) {
        for (const Model &model : models) {
            const bool selected = &model == part.GetModel();
            if (ImGui::Selectable(model.Name, selected) && !selected) {
                part.SetModel(model);
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        if (ImGui::Selectable(custom_name, part.IsCustom()) && !part.IsCustom()) {
            part.SetCustom();
            LoadParameterFields(fields, value_fields, part.GetParameters());
            changed = true;
        }
        ImGui::EndCombo();
    }

    if (!part.IsCustom()) {
        ImGui::TextDisabled("%s", part.GetModel()->Description);
        return changed;
    }
    Parameters parameters = part.GetParameters();
    const auto is_valid = [&part](const Parameters &candidate) { return part.IsValidParameters(candidate); };
    if (DrawParameterFields(fields, value_fields, "", is_valid, parameters)) {
        part.SetCustomParameters(parameters);
        changed = true;
    }
    return changed;
}

bool HasModel(const Core::ComponentType type) {
    return Core::IsDiode(type) || Core::IsBJT(type) || Core::IsMOSFET(type) || type == Core::ComponentType::OpAmp;
}

// Value fields show for parts with a value, and for parts with a model only while they are custom
bool ShowsValueFields(const Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        return static_cast<const Core::Diode &>(component).IsCustom();
    }
    if (Core::IsBJT(type)) {
        return static_cast<const Core::BJT &>(component).IsCustom();
    }
    if (Core::IsMOSFET(type)) {
        return static_cast<const Core::MOSFET &>(component).IsCustom();
    }
    // The ideal op-amp has its gain-bandwidth product to edit
    if (type == Core::ComponentType::OpAmp) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        return op_amp.IsCustom() || op_amp.IsIdeal();
    }
    return component.HasValue();
}

} // namespace

/**
 * @brief   Creates the window for a schematic.
 * @param[in] schematic  Schematic whose selection is shown; it must outlive the window.
 */
PropertiesWindow::PropertiesWindow(Schematic &schematic) : AppWindow("Properties", true), m_Schematic(schematic) {
}

void PropertiesWindow::Draw() {
    if (m_Schematic.GetSelectedWireIndex()) {
        ImGui::TextUnformatted("Wire");
        return;
    }
    UIElement *element = m_Schematic.GetSelectedElement();
    if (element == nullptr) {
        ImGui::TextDisabled("Select a component to edit it");
        return;
    }

    Core::Component &component = element->GetComponent();
    ImGui::TextUnformatted(component.GetTypeName());
    if (!component.GetName().empty()) {
        ImGui::TextUnformatted(std::format("Name: {}", component.GetName()).c_str());
    }
    const Core::ComponentType type = component.GetType();
    if (!component.HasValue() && !HasModel(type)) {
        return;
    }

    if (m_LoadedSelection != m_Schematic.GetSelectionVersion()) {
        LoadFields(component);
    }
    if (Core::IsDiode(type)) {
        DrawDiode(static_cast<Core::Diode &>(component));
    } else if (Core::IsBJT(type)) {
        DrawBJT(static_cast<Core::BJT &>(component));
    } else if (Core::IsMOSFET(type)) {
        DrawMOSFET(static_cast<Core::MOSFET &>(component));
    } else if (type == Core::ComponentType::OpAmp) {
        DrawOpAmp(static_cast<Core::OpAmp &>(component));
    } else if (Core::IsSource(type)) {
        DrawSource(static_cast<Core::Source &>(component));
    } else if (Core::IsControlledSource(type)) {
        DrawControlledSource(static_cast<Core::ControlledSource &>(component));
    } else {
        DrawValue(component);
    }
    if (!ShowsValueFields(component)) {
        return;
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

void PropertiesWindow::LoadFields(const Core::Component &component) {
    m_Value.Load(component.GetValue());
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        LoadParameterFields(ACFields, m_ACFields, source.GetAC());
        LoadParameterFields(PulseFields, m_PulseFields, source.GetPulse());
    }
    if (Core::IsDiode(component.GetType())) {
        const auto &diode = static_cast<const Core::Diode &>(component);
        LoadParameterFields(DiodeFields, m_DiodeFields, diode.GetParameters());
    }
    if (Core::IsBJT(component.GetType())) {
        const auto &bjt = static_cast<const Core::BJT &>(component);
        LoadParameterFields(BJTFields, m_BJTFields, bjt.GetParameters());
    }
    if (Core::IsMOSFET(component.GetType())) {
        const auto &mosfet = static_cast<const Core::MOSFET &>(component);
        LoadParameterFields(MOSFETFields, m_MOSFETFields, mosfet.GetParameters());
    }
    if (component.GetType() == Core::ComponentType::OpAmp) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        LoadParameterFields(OpAmpFields, m_OpAmpFields, op_amp.GetParameters());
        m_Value.Load(op_amp.GetIdealBandwidth());
    }
    m_LoadedSelection = m_Schematic.GetSelectionVersion();
}

void PropertiesWindow::DrawValue(Core::Component &component) {
    const auto is_valid = [&component](const double value) { return component.IsValidValue(value); };
    if (const std::optional<double> value = m_Value.Draw("Value", component.GetUnit(), is_valid)) {
        component.SetValue(*value);
        m_Schematic.MarkModified();
    }
}

void PropertiesWindow::DrawDiode(Core::Diode &diode) {
    if (DrawModelChoice(diode, Core::GetDiodeModels(diode.GetType()), Core::CustomDiodeModelName, DiodeFields,
                        m_DiodeFields)) {
        m_Schematic.MarkModified();
    }
    if (diode.IsCustom()) {
        ImGui::TextDisabled("%s", diode.GetType() == Core::ComponentType::ZenerDiode
                                      ? "BV is the voltage the Zener regulates at"
                                      : "BV is the reverse voltage the diode is rated for");
    }
}

void PropertiesWindow::DrawBJT(Core::BJT &bjt) {
    if (DrawModelChoice(bjt, Core::GetBJTModels(bjt.GetType()), Core::CustomBJTModelName, BJTFields, m_BJTFields)) {
        m_Schematic.MarkModified();
    }
    if (bjt.IsCustom()) {
        ImGui::TextDisabled("A VAF or IKF of 0 turns that effect off");
        ImGui::TextDisabled("VCEO, IC max and P max are checked after each simulation");
    }
}

void PropertiesWindow::DrawMOSFET(Core::MOSFET &mosfet) {
    if (DrawModelChoice(mosfet, Core::GetMOSFETModels(mosfet.GetType()), Core::CustomMOSFETModelName, MOSFETFields,
                        m_MOSFETFields)) {
        m_Schematic.MarkModified();
    }
    if (mosfet.IsCustom()) {
        ImGui::TextDisabled("Level 1: ID = KP/2 W/L (VGS - VTO)^2; VTO is negative for PMOS");
        ImGui::TextDisabled("The max ratings are checked after each simulation");
    }
}

// The ideal op-amp has only its gain-bandwidth product to set; a macromodel shows its specifications when custom
void PropertiesWindow::DrawOpAmp(Core::OpAmp &op_amp) {
    if (DrawModelChoice(op_amp, Core::GetOpAmpModels(op_amp.GetType()), Core::CustomOpAmpModelName, OpAmpFields,
                        m_OpAmpFields)) {
        m_Schematic.MarkModified();
    }
    if (op_amp.IsIdeal()) {
        if (const std::optional<double> bandwidth = m_Value.Draw("GBW", "Hz", Core::IsValidIdealBandwidth)) {
            op_amp.SetIdealBandwidth(*bandwidth);
            m_Schematic.MarkModified();
        }
        ImGui::TextDisabled("The open-loop gain falls to 1 at the GBW");
    } else if (op_amp.IsCustom()) {
        ImGui::TextDisabled("Headrooms: how close the output gets to V+ and V-");
        ImGui::TextDisabled("Built as a Boyle macromodel from these specs");
    }
}

void PropertiesWindow::DrawSource(Core::Source &source) {
    using SourceType = Core::Source::SourceType;
    for (const SourceType type : {SourceType::DC, SourceType::AC, SourceType::Pulse}) {
        if (type != SourceType::DC) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(Core::GetSourceTypeName(type), source.GetSourceType() == type) &&
            source.GetSourceType() != type) {
            source.SetSourceType(type);
            m_Schematic.MarkModified();
        }
    }

    switch (source.GetSourceType()) {
    case SourceType::DC: {
        const char *label = source.GetType() == Core::ComponentType::CurrentSource ? "Current" : "Voltage";
        if (const std::optional<double> value = m_Value.Draw(label, source.GetUnit(), AnyValue)) {
            source.SetValue(*value);
            m_Schematic.MarkModified();
        }
        break;
    }
    case SourceType::AC:
        DrawSine(source);
        break;
    case SourceType::Pulse:
        DrawPulse(source);
        break;
    }
}

void PropertiesWindow::DrawSine(Core::Source &source) {
    Core::ACParameters ac = source.GetAC();
    const auto is_valid = [&source](const Core::ACParameters &candidate) { return source.IsValidAC(candidate); };
    if (DrawParameterFields(ACFields, m_ACFields, source.GetUnit(), is_valid, ac)) {
        source.SetAC(ac);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The amplitude is the peak of the sine in a transient");
    ImGui::TextDisabled("The AC magnitude drives the AC sweep; 1 reads as gain");
}

void PropertiesWindow::DrawPulse(Core::Source &source) {
    Core::PulseParameters pulse = source.GetPulse();
    const auto is_valid = [&source](const Core::PulseParameters &candidate) { return source.IsValidPulse(candidate); };
    if (DrawParameterFields(PulseFields, m_PulseFields, source.GetUnit(), is_valid, pulse)) {
        source.SetPulse(pulse);
        m_Schematic.MarkModified();
    }

    ImGui::TextDisabled("%s", std::format("Repeats at {}Hz", Core::FormatValue(1.0 / pulse.Period)).c_str());
    if (pulse.RiseTime + pulse.Width + pulse.FallTime > pulse.Period) {
        ImGui::TextColored(GetWarningTextColor(), "Rise, width and fall add up to more than the period");
    }
}

// The gain is the ratio of output to control, so the line below spells out what it multiplies
void PropertiesWindow::DrawControlledSource(Core::ControlledSource &source) {
    if (const std::optional<double> value = m_Value.Draw("Gain", source.GetUnit(), AnyValue)) {
        source.SetValue(*value);
        m_Schematic.MarkModified();
    }
    const Core::ComponentType type = source.GetType();
    if (Core::IsCurrentControlled(type)) {
        DrawControllingCurrent(source);
    }
    if (type == Core::ComponentType::VCVS) {
        ImGui::TextDisabled("V(1, 2) = gain x V(+, -) of the control pins");
    } else if (type == Core::ComponentType::VCCS) {
        ImGui::TextDisabled("Pushes gain x V(+, -) from terminal 1 to 2");
    } else if (type == Core::ComponentType::CCCS) {
        ImGui::TextDisabled("Pushes gain x I from terminal 1 to 2");
    } else {
        ImGui::TextDisabled("V(1, 2) = gain x I");
    }
    ImGui::TextDisabled("A negative gain inverts the output");
}

// Any current the results report can control the source, except its own; they read as the plots name them
void PropertiesWindow::DrawControllingCurrent(Core::ControlledSource &source) {
    const std::string &current = source.GetControllingCurrent();
    const std::string preview = current.empty() ? "(none)" : std::format("I({})", current);
    DrawFieldLabel("Follows");
    if (ImGui::BeginCombo("##Follows", preview.c_str())) {
        for (const auto &element : m_Schematic.GetElements()) {
            if (&element->GetComponent() == &source) {
                continue;
            }
            for (const std::string &name : Core::GetCurrentNames(element->GetComponent())) {
                const bool selected = name == current;
                if (ImGui::Selectable(std::format("I({})", name).c_str(), selected) && !selected) {
                    source.SetControllingCurrent(name);
                    m_Schematic.MarkModified();
                }
            }
        }
        ImGui::EndCombo();
    }
    if (current.empty()) {
        ImGui::TextColored(GetWarningTextColor(), "Pick the current the source follows");
    }
}

} // namespace GUI
