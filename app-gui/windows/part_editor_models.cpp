/**
 * @file    part_editor_models.cpp
 * @brief   The properties of diodes, transistors and op-amps: the model they use and its parameters.
 */

#include "windows/part_editor_models.h"

#include "circuit/components/component.h"
#include "spice/spice_value.h"
#include "windows/part_fields.h"
#include <cfloat>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace GUI {

namespace {

/// Custom model parameters beyond this many scroll.
constexpr float MAX_VISIBLE_PARAMETERS = 10.0f;
/// Width, in font sizes, at which the description of a ready model wraps, so a long one does not widen the popover.
constexpr float DESCRIPTION_WRAP_FONT_SIZES = 26.0f;

/// Labeled with the SPICE parameter names, as datasheet models list them.
constexpr auto DIODE_FIELDS = std::to_array<ParameterField<Core::DiodeParameters>>({
    {"IS", &Core::DiodeParameters::SaturationCurrent, "A"},
    {"N", &Core::DiodeParameters::EmissionCoefficient, ""},
    {"RS", &Core::DiodeParameters::SeriesResistance, "Ohm"},
    {"BV", &Core::DiodeParameters::BreakdownVoltage, "V"},
    {"IBV", &Core::DiodeParameters::BreakdownCurrent, "A"},
    {"CJO", &Core::DiodeParameters::JunctionCapacitance, "F"},
    {"VJ", &Core::DiodeParameters::JunctionPotential, "V"},
    {"M", &Core::DiodeParameters::GradingCoefficient, ""},
    {"TT", &Core::DiodeParameters::TransitTime, "s"},
    {"IF max", &Core::DiodeParameters::MaxForwardCurrent, "A"},
    {"P max", &Core::DiodeParameters::MaxPower, "W"},
});

constexpr auto BJT_FIELDS = std::to_array<ParameterField<Core::BJTParameters>>({
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

constexpr auto MOSFET_FIELDS = std::to_array<ParameterField<Core::MOSFETParameters>>({
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

/// Datasheet specifications, from which the macromodel is built.
constexpr auto OP_AMP_FIELDS = std::to_array<ParameterField<Core::OpAmpParameters>>({
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
    {"VOS", &Core::OpAmpParameters::InputOffsetVoltage, "V"},
});

/**
 * @brief   Draws the model combo of a part that offers ready models, models imported from libraries or custom
 *          parameters, its description or its custom fields, and applies a change; returns whether the part changed.
 * @details Custom starts from the parameters of the model it replaces, so a ready part can be tweaked.
 */
template <typename Part, typename Model, typename Parameters, std::size_t Count>
bool DrawModelChoice(Part &part, const std::span<const Model> models, const char *custom_name,
                     const std::array<ParameterField<Parameters>, Count> &fields,
                     std::array<ValueField, Count> &value_fields,
                     const std::span<const Core::LibraryModel> library_models = {}) {
    bool changed = false;
    DrawFieldLabel("Model");
    // A library model may share its name with a ready one, so it says where it comes from
    const std::string preview =
        part.HasLibraryModel() ? std::format("{} (library)", part.GetModelName()) : std::string(part.GetModelName());
    if (ImGui::BeginCombo("##Model", preview.c_str())) {
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
        // The models imported from libraries that fit the part, after the ready ones
        for (const Core::LibraryModel &model : library_models) {
            if (!Core::CanUseLibraryModel(part.GetType(), model.Kind)) {
                continue;
            }
            const bool selected = part.GetLibraryModel() == model.Name;
            if (ImGui::Selectable(std::format("{} (library)", model.Name).c_str(), selected) && !selected) {
                part.SetLibraryModel(model.Name);
                changed = true;
            }
        }
        if (ImGui::Selectable(custom_name, part.IsCustom()) && !part.IsCustom()) {
            part.SetCustom();
            LoadParameterFields(fields, value_fields, part.GetParameters());
            changed = true;
        }
        ImGui::EndCombo();
    }

    if (part.HasLibraryModel()) {
        ImGui::TextDisabled("From a SPICE library: its parameters are those of its .model text");
        return changed;
    }
    if (!part.IsCustom()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * DESCRIPTION_WRAP_FONT_SIZES);
        ImGui::TextDisabled("%s", part.GetModel()->Description);
        ImGui::PopTextWrapPos();
        return changed;
    }
    Parameters parameters = part.GetParameters();
    const auto is_valid = [&part](const Parameters &candidate) { return part.IsValidParameters(candidate); };
    // A long list scrolls, so the editor stays small enough for a popover
    ImGui::SetNextWindowSizeConstraints({0.0f, 0.0f},
                                        {FLT_MAX, ImGui::GetFrameHeightWithSpacing() * MAX_VISIBLE_PARAMETERS});
    ImGui::BeginChild("parameters", {0.0f, 0.0f}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    if (DrawParameterFields(fields, value_fields, "", is_valid, parameters)) {
        part.SetCustomParameters(parameters);
        changed = true;
    }
    ImGui::EndChild();
    return changed;
}

/**
 * @brief   The transistors of the input pair of a custom macromodel, which the specification fields leave out since
 *          they are not a number.
 */
void DrawOpAmpInputStage(Core::OpAmp &op_amp, Schematic &schematic) {
    Core::OpAmpParameters parameters = op_amp.GetParameters();
    DrawFieldLabel("Input stage");
    if (ImGui::BeginCombo("##input_stage", Core::GetOpAmpInputStageName(parameters.InputStage))) {
        for (const Core::OpAmpInputStage stage : Core::GetOpAmpInputStages()) {
            const bool is_selected = stage == parameters.InputStage;
            if (ImGui::Selectable(Core::GetOpAmpInputStageName(stage), is_selected) && !is_selected) {
                parameters.InputStage = stage;
                op_amp.SetCustomParameters(parameters);
                schematic.MarkModified();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

/**
 * @brief   The custom parameters of diodes, transistors and op-amps, and the bandwidth of the ideal op-amp.
 */
void LoadModelFields(ModelFields &fields, const Core::Component &component, ValueField &main_value) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        LoadParameterFields(DIODE_FIELDS, fields.Diode, static_cast<const Core::Diode &>(component).GetParameters());
    } else if (Core::IsBJT(type)) {
        LoadParameterFields(BJT_FIELDS, fields.BJT, static_cast<const Core::BJT &>(component).GetParameters());
    } else if (Core::IsMOSFET(type)) {
        LoadParameterFields(MOSFET_FIELDS, fields.MOSFET, static_cast<const Core::MOSFET &>(component).GetParameters());
    } else if (type == Core::ComponentType::OP_AMP) {
        const auto &op_amp = static_cast<const Core::OpAmp &>(component);
        LoadParameterFields(OP_AMP_FIELDS, fields.OpAmp, op_amp.GetParameters());
        main_value.Load(op_amp.GetIdealBandwidth());
    }
}

void DrawDiode(Core::Diode &diode, ModelFields &fields, Schematic &schematic) {
    if (DrawModelChoice(diode, Core::GetDiodeModels(diode.GetType()), Core::CUSTOM_DIODE_MODEL_NAME, DIODE_FIELDS,
                        fields.Diode, std::span(schematic.GetLibraryModels()))) {
        schematic.MarkModified();
    }
    if (diode.IsCustom()) {
        ImGui::TextDisabled("%s", diode.GetType() == Core::ComponentType::ZENER_DIODE
                                      ? "BV is the voltage the Zener regulates at"
                                      : "BV is the reverse voltage the diode is rated for");
    }
}

void DrawBJT(Core::BJT &bjt, ModelFields &fields, Schematic &schematic) {
    if (DrawModelChoice(bjt, Core::GetBJTModels(bjt.GetType()), Core::CUSTOM_BJT_MODEL_NAME, BJT_FIELDS, fields.BJT,
                        std::span(schematic.GetLibraryModels()))) {
        schematic.MarkModified();
    }
    if (bjt.IsCustom()) {
        ImGui::TextDisabled("A VAF or IKF of 0 turns that effect off");
        ImGui::TextDisabled("VCEO, IC max and P max are checked after each simulation");
    }
}

void DrawMOSFET(Core::MOSFET &mosfet, ModelFields &fields, Schematic &schematic) {
    if (DrawModelChoice(mosfet, Core::GetMOSFETModels(mosfet.GetType()), Core::CUSTOM_MOSFET_MODEL_NAME, MOSFET_FIELDS,
                        fields.MOSFET, std::span(schematic.GetLibraryModels()))) {
        schematic.MarkModified();
    }
    if (mosfet.IsCustom()) {
        ImGui::TextDisabled("Level 1: ID = KP/2 W/L (VGS - VTO)^2; VTO is negative for PMOS");
        ImGui::TextDisabled("The max ratings are checked after each simulation");
    }
}

/**
 * @brief   The ideal op-amp has only its gain-bandwidth product to set; a macromodel shows its specifications when
 *          custom.
 */
void DrawOpAmp(Core::OpAmp &op_amp, ModelFields &fields, ValueField &main_value, Schematic &schematic) {
    if (DrawModelChoice(op_amp, Core::GetOpAmpModels(op_amp.GetType()), Core::CUSTOM_OP_AMP_MODEL_NAME, OP_AMP_FIELDS,
                        fields.OpAmp)) {
        schematic.MarkModified();
    }
    if (op_amp.IsIdeal()) {
        if (const std::optional<double> bandwidth = main_value.Draw("GBW", "Hz", Core::IsValidIdealBandwidth)) {
            op_amp.SetIdealBandwidth(*bandwidth);
            schematic.MarkModified();
        }
        ImGui::TextDisabled("The open-loop gain falls to 1 at the GBW");
    } else if (op_amp.IsCustom()) {
        DrawOpAmpInputStage(op_amp, schematic);
        ImGui::TextDisabled("Headrooms: how close the output gets to V+ and V-");
        ImGui::TextDisabled("VOS adds to the + input; PNP and JFET inputs reach V-");
        ImGui::TextDisabled("Built as a Boyle macromodel from these specs");
    }
}

} // namespace GUI
