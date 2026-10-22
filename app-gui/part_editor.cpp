/**
 * @file    part_editor.cpp
 * @brief   Fields that show and edit the component selected in the schematic.
 */

#include "part_editor.h"

#include "element_factory.h"
#include "misc/cpp/imgui_stdlib.h"
#include "simulator.h"
#include "spice_value.h"
#include "theme.h"
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace GUI {

namespace {

bool AnyValue(double /*value*/) {
    return true;
}

// Custom model parameters beyond this many scroll
constexpr float MaxVisibleParameters = 10.0f;

/**
 * @struct  ParameterField
 * @brief   One member of a parameter set, such as Core::PulseParameters, as a field of the part editor.
 */
template <typename Parameters> struct ParameterField {
    const char *Label;
    double Parameters::*Value;
    // No unit means the unit of the source, volts or amperes
    const char *Unit;
    // What the field shows is the stored value times this, such as 100 for a share kept as a fraction and typed in %
    double Scale = 1.0;
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

constexpr auto EXPFields = std::to_array<ParameterField<Core::EXPParameters>>({
    {"Start", &Core::EXPParameters::InitialValue, nullptr},
    {"Peak", &Core::EXPParameters::PeakValue, nullptr},
    {"Rise delay", &Core::EXPParameters::RiseDelay, "s"},
    {"Rise tau", &Core::EXPParameters::RiseTimeConstant, "s"},
    {"Fall delay", &Core::EXPParameters::FallDelay, "s"},
    {"Fall tau", &Core::EXPParameters::FallTimeConstant, "s"},
});

constexpr auto SFFMFields = std::to_array<ParameterField<Core::SFFMParameters>>({
    {"Offset", &Core::SFFMParameters::Offset, nullptr},
    {"Amplitude", &Core::SFFMParameters::Amplitude, nullptr},
    {"Carrier", &Core::SFFMParameters::CarrierFrequency, "Hz"},
    {"Mod index", &Core::SFFMParameters::ModulationIndex, ""},
    {"Signal", &Core::SFFMParameters::SignalFrequency, "Hz"},
});

constexpr auto AMFields = std::to_array<ParameterField<Core::AMParameters>>({
    {"Amplitude", &Core::AMParameters::Amplitude, nullptr},
    {"Offset", &Core::AMParameters::Offset, nullptr},
    {"Mod freq", &Core::AMParameters::ModulatingFrequency, "Hz"},
    {"Carrier", &Core::AMParameters::CarrierFrequency, "Hz"},
    {"Delay", &Core::AMParameters::Delay, "s"},
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

constexpr auto SwitchFields = std::to_array<ParameterField<Core::SwitchParameters>>({
    {"Threshold", &Core::SwitchParameters::Threshold, "V"},
    {"Hysteresis", &Core::SwitchParameters::Hysteresis, "V"},
    {"On resistance", &Core::SwitchParameters::OnResistance, "Ohm"},
    {"Off resistance", &Core::SwitchParameters::OffResistance, "Ohm"},
});

constexpr auto RelayFields = std::to_array<ParameterField<Core::RelayParameters>>({
    {"Coil R", &Core::RelayParameters::CoilResistance, "Ohm"},
    {"Coil L", &Core::RelayParameters::CoilInductance, "H"},
    {"Pull-in", &Core::RelayParameters::PullInVoltage, "V"},
    {"Drop-out", &Core::RelayParameters::DropOutVoltage, "V"},
    {"Contact R", &Core::RelayParameters::ContactResistance, "Ohm"},
});

constexpr auto TransformerFields = std::to_array<ParameterField<Core::TransformerParameters>>({
    {"Turns ratio", &Core::TransformerParameters::TurnsRatio, ":1"},
    {"Coupling", &Core::TransformerParameters::Coupling, ""},
    {"Winding R", &Core::TransformerParameters::WindingResistance, "Ohm"},
});

constexpr auto LogicFields = std::to_array<ParameterField<Core::LogicParameters>>({
    {"Logic level", &Core::LogicParameters::LogicLevel, "V"},
    {"Delay", &Core::LogicParameters::PropagationDelay, "s"},
});

// A clock picks its own rise and fall times, so the user sets only these figures
constexpr auto ClockFields = std::to_array<ParameterField<Core::ClockParameters>>({
    {"Frequency", &Core::ClockParameters::Frequency, "Hz"},
    {"Duty", &Core::ClockParameters::DutyCycle, "%", 100.0},
    {"Logic level", &Core::ClockParameters::LogicLevel, "V"},
    {"Start delay", &Core::ClockParameters::StartDelay, "s"},
});

constexpr auto LogicInputFields = std::to_array<ParameterField<Core::LogicInputParameters>>({
    {"Logic level", &Core::LogicInputParameters::LogicLevel, "V"},
});

template <typename Parameters, std::size_t Count>
void LoadParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const Parameters &parameters) {
    for (std::size_t index = 0; index < Count; ++index) {
        value_fields[index].Load(parameters.*fields[index].Value * fields[index].Scale);
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
            candidate.*field.Value = value / field.Scale;
            return is_valid(candidate);
        };
        const char *unit = field.Unit != nullptr ? field.Unit : source_unit;
        if (const std::optional<double> value = value_fields[index].Draw(field.Label, unit, is_valid_value)) {
            parameters.*field.Value = *value / field.Scale;
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
    // A long list scrolls, so the editor stays small enough for a popover
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f},
                                        {FLT_MAX, ImGui::GetFrameHeightWithSpacing() * MaxVisibleParameters});
    ImGui::BeginChild("parameters", {0.0f, 0.0f}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    if (DrawParameterFields(fields, value_fields, "", is_valid, parameters)) {
        part.SetCustomParameters(parameters);
        changed = true;
    }
    ImGui::EndChild();
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
    // The switches that follow a voltage, the logic parts, a clock and a logic input have only their parameters
    if (type == Core::ComponentType::VSwitch || type == Core::ComponentType::Relay || Core::IsLogicPart(type) ||
        type == Core::ComponentType::Clock || type == Core::ComponentType::LogicInput) {
        return true;
    }
    return component.HasValue();
}

} // namespace

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic  Schematic whose selection is edited; it must outlive the editor.
 */
PartEditor::PartEditor(Schematic &schematic) : m_Schematic(schematic) {
}

/**
 * @brief   Draws the type and name of the selected component and the fields of its values, model or waveform.
 * @note    Edits apply as soon as a field holds a valid value, and mark the schematic modified.
 */
void PartEditor::Draw() {
    UIElement *element = m_Schematic.GetSelectedElement();
    if (element == nullptr) {
        DrawSelectionSummary();
        return;
    }

    Core::Component &component = element->GetComponent();
    ImGui::TextUnformatted(GetPartName(component.GetType()));
    if (!component.GetName().empty()) {
        ImGui::TextUnformatted(std::format("Name: {}", component.GetName()).c_str());
    }
    const Core::ComponentType type = component.GetType();
    if (!component.HasValue() && !HasModel(type) && type != Core::ComponentType::NetLabel && !Core::IsSwitch(type) &&
        type != Core::ComponentType::VSwitch && type != Core::ComponentType::Relay && !Core::IsLogicPart(type) &&
        type != Core::ComponentType::Clock && type != Core::ComponentType::LogicInput) {
        return;
    }

    if (m_LoadedSelection != m_Schematic.GetSelectionVersion()) {
        LoadFields(component);
    }
    DrawFields(component);
    if (!ShowsValueFields(component)) {
        return;
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

// What there is to edit when no single part is selected: how many items are, or a hint
void PartEditor::DrawSelectionSummary() {
    const std::size_t count = m_Schematic.GetSelectedElements().size() + m_Schematic.GetSelectedWires().size();
    if (count > 1) {
        ImGui::TextUnformatted(std::format("{} items selected", count).c_str());
    } else if (count == 1) {
        ImGui::TextUnformatted("Wire");
    } else {
        ImGui::TextDisabled("Select a component to edit it");
    }
}

// The fields of the kind of part: its net name, model, waveform, gain or value
void PartEditor::DrawFields(Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::NetLabel) {
        DrawNetLabel(static_cast<Core::NetLabel &>(component));
        return;
    }
    if (DrawModeledPart(component)) {
        return;
    }
    if (Core::IsSource(type)) {
        DrawSource(static_cast<Core::Source &>(component));
    } else if (Core::IsControlledSource(type)) {
        DrawControlledSource(static_cast<Core::ControlledSource &>(component));
    } else if (Core::IsSwitch(type)) {
        DrawSwitch(static_cast<Core::Switch &>(component));
    } else if (type == Core::ComponentType::VSwitch) {
        DrawControlledSwitch(static_cast<Core::VoltageControlledSwitch &>(component));
    } else if (type == Core::ComponentType::Relay) {
        DrawRelay(static_cast<Core::Relay &>(component));
    } else if (Core::IsLogicPart(type)) {
        DrawLogicPart(static_cast<Core::LogicPart &>(component));
    } else if (type == Core::ComponentType::Clock) {
        DrawClock(static_cast<Core::Clock &>(component));
    } else if (type == Core::ComponentType::LogicInput) {
        DrawLogicInput(static_cast<Core::LogicInput &>(component));
    } else if (type == Core::ComponentType::Transformer) {
        DrawValue(component);
        DrawTransformer(static_cast<Core::Transformer &>(component));
    } else {
        DrawValue(component);
        if (type == Core::ComponentType::Potentiometer) {
            DrawWiper(static_cast<Core::Potentiometer &>(component));
        } else if (component.SupportsInitialCondition()) {
            DrawInitialCondition(component);
        }
    }
}

// Diodes, transistors and op-amps, which a model sets; false for the other parts
bool PartEditor::DrawModeledPart(Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        DrawDiode(static_cast<Core::Diode &>(component));
    } else if (Core::IsBJT(type)) {
        DrawBJT(static_cast<Core::BJT &>(component));
    } else if (Core::IsMOSFET(type)) {
        DrawMOSFET(static_cast<Core::MOSFET &>(component));
    } else if (type == Core::ComponentType::OpAmp) {
        DrawOpAmp(static_cast<Core::OpAmp &>(component));
    } else {
        return false;
    }
    return true;
}

/**
 * @brief   Tells whether the selected part has a main value that typing can start editing.
 * @return  True for parts with a value (resistors, capacitors, inductors, VCC, controlled sources), DC sources and
 *          ideal op-amps; false without a selected part, and for parts set by a model or a waveform.
 */
bool PartEditor::CanTypeValue() const {
    const UIElement *element = m_Schematic.GetSelectedElement();
    if (element == nullptr) {
        return false;
    }
    const Core::Component &component = element->GetComponent();
    const Core::ComponentType type = component.GetType();
    if (Core::IsSource(type)) {
        return static_cast<const Core::Source &>(component).GetSourceType() == Core::Source::SourceType::DC;
    }
    if (type == Core::ComponentType::OpAmp) {
        return static_cast<const Core::OpAmp &>(component).IsIdeal();
    }
    return component.HasValue() && !HasModel(type);
}

/**
 * @brief   Makes the next Draw() focus the main value of the selected part and type a character into it.
 * @param[in] first  The character the user typed on the part, such as the 4 of "4k7".
 * @note    Call it only when CanTypeValue() holds, so the value field is drawn.
 */
void PartEditor::StartTypingValue(const char first) {
    m_Value.StartTyping(first);
}

/**
 * @brief   Makes the next Draw() focus the name of the selected net label.
 * @note    Call it when a label has just been placed, so the user can type its name at once.
 */
void PartEditor::FocusNetName() {
    m_FocusNet = true;
}

/**
 * @brief   Returns the standard series the values of resistors, capacitors and inductors are taken from.
 * @return  The series, or Core::ValueSeries::Free when any value is allowed.
 */
Core::ValueSeries PartEditor::GetValueSeries() const {
    return m_ValueSeries;
}

/**
 * @brief   Chooses the standard series for resistors, capacitors and inductors.
 * @param[in] series  The series, or Core::ValueSeries::Free to allow any value.
 * @note    Parts that already have a value outside the series keep it until the user edits it.
 */
void PartEditor::SetValueSeries(const Core::ValueSeries series) {
    m_ValueSeries = series;
}

void PartEditor::LoadFields(const Core::Component &component) {
    m_Value.Load(component.GetValue());
    m_InitialCondition.LoadOptional(component.GetInitialCondition());
    if (component.GetType() == Core::ComponentType::NetLabel) {
        const std::string &net = static_cast<const Core::NetLabel &>(component).GetNet();
        m_Net.fill('\0');
        net.copy(m_Net.data(), m_Net.size() - 1);
    }
    if (Core::IsSource(component.GetType())) {
        const auto &source = static_cast<const Core::Source &>(component);
        LoadParameterFields(ACFields, m_ACFields, source.GetAC());
        LoadParameterFields(PulseFields, m_PulseFields, source.GetPulse());
        LoadParameterFields(EXPFields, m_EXPFields, source.GetEXP());
        LoadParameterFields(SFFMFields, m_SFFMFields, source.GetSFFM());
        LoadParameterFields(AMFields, m_AMFields, source.GetAM());
        m_PWLText = Core::FormatPWLPoints(source.GetPWL().Points);
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
    if (component.GetType() == Core::ComponentType::VSwitch) {
        LoadParameterFields(SwitchFields, m_SwitchFields,
                            static_cast<const Core::VoltageControlledSwitch &>(component).GetParameters());
    }
    if (component.GetType() == Core::ComponentType::Relay) {
        LoadParameterFields(RelayFields, m_RelayFields, static_cast<const Core::Relay &>(component).GetParameters());
    }
    if (Core::IsLogicPart(component.GetType())) {
        LoadParameterFields(LogicFields, m_LogicFields,
                            static_cast<const Core::LogicPart &>(component).GetParameters());
    }
    if (component.GetType() == Core::ComponentType::Clock) {
        LoadParameterFields(ClockFields, m_ClockFields, static_cast<const Core::Clock &>(component).GetParameters());
    }
    if (component.GetType() == Core::ComponentType::LogicInput) {
        LoadParameterFields(LogicInputFields, m_LogicInputFields,
                            static_cast<const Core::LogicInput &>(component).GetParameters());
    }
    if (component.GetType() == Core::ComponentType::Transformer) {
        LoadParameterFields(TransformerFields, m_TransformerFields,
                            static_cast<const Core::Transformer &>(component).GetParameters());
    }
    m_LoadedSelection = m_Schematic.GetSelectionVersion();
}

// The name applies as soon as it is valid, or empty, which leaves the label unnamed
void PartEditor::DrawNetLabel(Core::NetLabel &label) {
    DrawFieldLabel("Net");
    if (std::exchange(m_FocusNet, false)) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool changed = ImGui::InputText("##net", m_Net.data(), m_Net.size());
    const std::string typed = m_Net.data();
    const bool valid = typed.empty() || Core::NetLabel::IsValidNet(typed);
    if (changed && valid && typed != label.GetNet()) {
        label.SetNet(typed);
        m_Schematic.MarkModified();
    }
    if (!valid) {
        ImGui::TextColored(GetErrorTextColor(), "1 to 32 letters, digits, _, - or .");
    } else {
        ImGui::TextDisabled("Labels with the same name are one net; 0 and GND are ground, VCC is the supplies");
    }
}

void PartEditor::DrawValue(Core::Component &component) {
    const auto is_valid = [&component](const double value) { return component.IsValidValue(value); };
    // Standard values are for the passive parts; the value of a source or a gain is whatever the circuit needs
    const Core::ComponentType type = component.GetType();
    const bool passive = type == Core::ComponentType::Resistor || type == Core::ComponentType::Capacitor ||
                         type == Core::ComponentType::Inductor;
    const Core::ValueSeries series = passive ? m_ValueSeries : Core::ValueSeries::Free;
    if (const std::optional<double> value = m_Value.Draw("Value", component.GetUnit(), is_valid, series)) {
        component.SetValue(*value);
        m_Schematic.MarkModified();
    }
    if (series != Core::ValueSeries::Free) {
        ImGui::TextDisabled("Standard values: %s, %g%%", Core::GetSeriesName(series),
                            Core::GetSeriesTolerancePercent(series));
    }
}

// A capacitor's initial condition is a voltage and an inductor's a current; leaving the field empty means no initial
// condition, but once any part has one the whole transient runs with uic and an empty part starts at zero
void PartEditor::DrawInitialCondition(Core::Component &component) {
    const bool is_capacitor = component.GetType() == Core::ComponentType::Capacitor;
    const GUI::OptionalValueEdit edit = m_InitialCondition.DrawOptional("Initial", is_capacitor ? "V" : "A", AnyValue);
    if (edit.Changed) {
        if (edit.Value) {
            component.SetInitialCondition(*edit.Value);
        } else {
            component.ClearInitialCondition();
        }
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Skips the operating point (uic); parts left empty start at zero");
}

// The switch closes above the threshold plus the hysteresis, and opens below the threshold minus it
void PartEditor::DrawControlledSwitch(Core::VoltageControlledSwitch &controlled) {
    Core::SwitchParameters parameters = controlled.GetParameters();
    const auto is_valid = [](const Core::SwitchParameters &candidate) {
        return Core::VoltageControlledSwitch::IsValidParameters(candidate);
    };
    if (DrawParameterFields(SwitchFields, m_SwitchFields, "", is_valid, parameters)) {
        controlled.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Closes above threshold + hysteresis, opens below threshold - hysteresis");
}

// The contact closes when the coil voltage reaches the pull-in value and opens when it falls to the drop-out one
void PartEditor::DrawRelay(Core::Relay &relay) {
    Core::RelayParameters parameters = relay.GetParameters();
    const auto is_valid = [](const Core::RelayParameters &candidate) {
        return Core::Relay::IsValidParameters(candidate);
    };
    if (DrawParameterFields(RelayFields, m_RelayFields, "", is_valid, parameters)) {
        relay.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The contact follows the coil current, so it lags by L over R");
}

// The value is the primary inductance; the secondary's follows from the turns ratio
void PartEditor::DrawTransformer(Core::Transformer &transformer) {
    Core::TransformerParameters parameters = transformer.GetParameters();
    const auto is_valid = [](const Core::TransformerParameters &candidate) {
        return Core::Transformer::IsValidParameters(candidate);
    };
    if (DrawParameterFields(TransformerFields, m_TransformerFields, "", is_valid, parameters)) {
        transformer.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Value is the primary inductance; the secondary is %s",
                        Core::FormatValue(transformer.GetSecondaryInductance()).c_str());
}

// The levels the part swaps at follow from the logic level: it reads 1 above 70% of it and 0 below 30%
void PartEditor::DrawLogicPart(Core::LogicPart &part) {
    Core::LogicParameters parameters = part.GetParameters();
    const auto is_valid = [](const Core::LogicParameters &candidate) {
        return Core::LogicPart::IsValidParameters(candidate);
    };
    if (DrawParameterFields(LogicFields, m_LogicFields, "", is_valid, parameters)) {
        part.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Inputs read 1 above 70%% of the level and 0 below 30%%; an open input reads 0");
}

// The clock picks its own rise and fall times; the frequency sets the period and the duty the share that is high
void PartEditor::DrawClock(Core::Clock &clock) {
    Core::ClockParameters parameters = clock.GetParameters();
    const auto is_valid = [](const Core::ClockParameters &candidate) {
        return Core::Clock::IsValidParameters(candidate);
    };
    if (DrawParameterFields(ClockFields, m_ClockFields, "", is_valid, parameters)) {
        clock.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Duty is the share of the period above half the level, from 0 to 100%%");
    ImGui::TextDisabled("The start delay holds the clock low; at 0 the first rising edge is at t = 0");
}

// A constant source from the part to ground: high drives the logic level and low 0 V, so a latch has a defined start
void PartEditor::DrawLogicInput(Core::LogicInput &input) {
    bool high = input.IsHigh();
    if (ImGui::Checkbox("High", &high)) {
        input.SetHigh(high);
        m_Schematic.MarkModified();
    }
    Core::LogicInputParameters parameters = input.GetParameters();
    const auto is_valid = [](const Core::LogicInputParameters &candidate) {
        return Core::LogicInput::IsValidParameters(candidate);
    };
    if (DrawParameterFields(LogicInputFields, m_LogicInputFields, "", is_valid, parameters)) {
        input.SetParameters(parameters);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("A source from its terminal to ground, so it gives a gate a defined level");
}

// A closed switch conducts; a push button is pressed when closed
void PartEditor::DrawSwitch(Core::Switch &contact) {
    bool closed = contact.IsClosed();
    const bool is_button = contact.GetType() == Core::ComponentType::PushButton;
    if (ImGui::Checkbox(is_button ? "Pressed" : "Closed", &closed)) {
        contact.SetClosed(closed);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("It is a %s while the circuit is simulated", closed ? "wire" : "gap");
}

// The wiper as a share of the track, counted from terminal 1
void PartEditor::DrawWiper(Core::Potentiometer &pot) {
    DrawFieldLabel("Wiper");
    auto percent = static_cast<float>(pot.GetWiper() * 100.0);
    if (ImGui::SliderFloat("##wiper", &percent, 0.0f, 100.0f, "%.0f%%")) {
        pot.SetWiper(static_cast<double>(percent) / 100.0);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("0%% is terminal 1 and 100%% is terminal 2");
}

void PartEditor::DrawDiode(Core::Diode &diode) {
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

void PartEditor::DrawBJT(Core::BJT &bjt) {
    if (DrawModelChoice(bjt, Core::GetBJTModels(bjt.GetType()), Core::CustomBJTModelName, BJTFields, m_BJTFields)) {
        m_Schematic.MarkModified();
    }
    if (bjt.IsCustom()) {
        ImGui::TextDisabled("A VAF or IKF of 0 turns that effect off");
        ImGui::TextDisabled("VCEO, IC max and P max are checked after each simulation");
    }
}

void PartEditor::DrawMOSFET(Core::MOSFET &mosfet) {
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
void PartEditor::DrawOpAmp(Core::OpAmp &op_amp) {
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

void PartEditor::DrawSource(Core::Source &source) {
    using SourceType = Core::Source::SourceType;
    for (const SourceType type : {SourceType::DC, SourceType::AC, SourceType::Pulse, SourceType::EXP, SourceType::SFFM,
                                  SourceType::AM, SourceType::PWL}) {
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
    case SourceType::EXP:
        DrawEXP(source);
        break;
    case SourceType::SFFM:
        DrawSFFM(source);
        break;
    case SourceType::AM:
        DrawAM(source);
        break;
    case SourceType::PWL:
        DrawPWL(source);
        break;
    }
}

void PartEditor::DrawSine(Core::Source &source) {
    Core::ACParameters ac = source.GetAC();
    const auto is_valid = [&source](const Core::ACParameters &candidate) { return source.IsValidAC(candidate); };
    if (DrawParameterFields(ACFields, m_ACFields, source.GetUnit(), is_valid, ac)) {
        source.SetAC(ac);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The amplitude is the peak of the sine in a transient");
    ImGui::TextDisabled("The AC magnitude drives the AC sweep; 1 reads as gain");
}

void PartEditor::DrawPulse(Core::Source &source) {
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

void PartEditor::DrawEXP(Core::Source &source) {
    Core::EXPParameters exp = source.GetEXP();
    const auto is_valid = [&source](const Core::EXPParameters &candidate) { return source.IsValidEXP(candidate); };
    if (DrawParameterFields(EXPFields, m_EXPFields, source.GetUnit(), is_valid, exp)) {
        source.SetEXP(exp);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("Rises towards the peak, then decays back towards the start");
}

void PartEditor::DrawSFFM(Core::Source &source) {
    Core::SFFMParameters sffm = source.GetSFFM();
    const auto is_valid = [&source](const Core::SFFMParameters &candidate) { return source.IsValidSFFM(candidate); };
    if (DrawParameterFields(SFFMFields, m_SFFMFields, source.GetUnit(), is_valid, sffm)) {
        source.SetSFFM(sffm);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("A carrier whose phase swings with the signal");
}

void PartEditor::DrawAM(Core::Source &source) {
    Core::AMParameters am = source.GetAM();
    const auto is_valid = [&source](const Core::AMParameters &candidate) { return source.IsValidAM(candidate); };
    if (DrawParameterFields(AMFields, m_AMFields, source.GetUnit(), is_valid, am)) {
        source.SetAM(am);
        m_Schematic.MarkModified();
    }
    ImGui::TextDisabled("The modulating and carrier sines multiply; the offset shifts the whole waveform");
}

// The points apply as soon as the text reads, so a half-typed field keeps the last valid shape
void PartEditor::DrawPWL(Core::Source &source) {
    DrawFieldLabel("Points");
    const bool changed = ImGui::InputText("##pwl", &m_PWLText);
    const std::optional<std::vector<Core::PWLPoint>> points = Core::ParsePWLPoints(m_PWLText);
    if (changed && points) {
        source.SetPWL({.Points = *points});
        m_Schematic.MarkModified();
    }
    if (!points) {
        ImGui::TextColored(GetErrorTextColor(), "At least two \"time value\" pairs, times increasing from zero");
    } else {
        ImGui::TextDisabled("For example: 0 0, 1m 5, 2m 0");
    }
}

// The gain is the ratio of output to control, so the line below spells out what it multiplies
void PartEditor::DrawControlledSource(Core::ControlledSource &source) {
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
void PartEditor::DrawControllingCurrent(Core::ControlledSource &source) {
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
