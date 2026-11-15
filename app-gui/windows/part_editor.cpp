/**
 * @file    part_editor.cpp
 * @brief   Fields that show and edit the component selected in the schematic.
 */

#include "windows/part_editor.h"

#include "base/theme.h"
#include "schematic/element_factory.h"
#include "schematic/live_input.h"
#include "schematic/part_properties.h"
#include "windows/part_editor_dependent_sources.h"
#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

namespace GUI {

/**
 * @brief   Creates the editor for a schematic.
 * @param[in] schematic    Schematic whose selection is edited; it must outlive the editor.
 * @param[in] preferences  Gives the series of standard values; it must outlive the editor.
 */
PartEditor::PartEditor(Schematic &schematic, const Preferences &preferences)
    : m_Schematic(&schematic), m_Preferences(preferences) {
}

/**
 * @brief   Draws the type and name of the selected component and the fields of its values, model or waveform.
 * @note    Edits apply as soon as a field holds a valid value, and mark the schematic modified.
 */
void PartEditor::Draw() {
    UIElement *element = m_Schematic->GetSelectedElement();
    if (element == nullptr) {
        DrawSelectionSummary();
        return;
    }

    Core::Component &component = element->GetComponent();
    ImGui::TextUnformatted(GetPartName(component.GetType()));
    if (m_LoadedSelection != m_Schematic->GetSelectionVersion()) {
        LoadFields(component);
    }
    DrawName(component);
    const Core::ComponentType type = component.GetType();
    if (!HasPropertyFields(component) && !HasModelChoice(type)) {
        return;
    }
    // Most edits end a live run and drop what it made, which should never come as a surprise
    if (const char *live_note = m_Schematic->IsLiveRun() ? GetLiveEditNote(type) : nullptr) {
        ImGui::TextColored(GetWarningTextColor(), "%s", live_note);
    }
    DrawFields(component);
    if (!ShowsValueFields(component)) {
        return;
    }
    ImGui::TextDisabled("Suffixes: T G M k m u n p f (case sensitive)");
}

/**
 * @brief   Makes the editor edit the parts of another schematic, such as the contents of a block being edited.
 * @param[in] schematic  Schematic whose selected part the editor shows; it must outlive its use here.
 * @note    Back on the document, call SetBlockParameters() with no names too.
 */
void PartEditor::SetSchematic(Schematic &schematic) {
    m_Schematic = &schematic;
    m_LoadedSelection.reset();
}

/**
 * @brief   Sets the parameters that the values of the parts being edited can follow.
 * @param[in] names  Parameters of the block being edited; none on the document, whose parts cannot follow any.
 */
void PartEditor::SetBlockParameters(std::vector<std::string> names) {
    m_BlockParameters = std::move(names);
}

/**
 * @brief   Tells whether the selected part has a main value that typing can start editing.
 * @return  True for parts with a value (resistors, capacitors, inductors, VCC, controlled sources), DC sources and
 *          ideal op-amps; false without a selected part, and for parts set by a model or a waveform.
 */
bool PartEditor::CanTypeValue() const {
    const UIElement *element = m_Schematic->GetSelectedElement();
    if (element == nullptr) {
        return false;
    }
    return CanStartTypingValue(element->GetComponent());
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
    m_Blocks.IsNetFocusRequested = true;
}

/**
 * @brief   Loads the texts of every field from the component that was selected.
 */
void PartEditor::LoadFields(const Core::Component &component) {
    m_Name.fill('\0');
    component.GetName().copy(m_Name.data(), m_Name.size() - 1);
    m_NameError.clear();
    m_Value.Load(component.GetValue());
    LoadPassiveFields(m_Passives, component);
    LoadBlockFields(m_Blocks, component, *m_Schematic);
    if (Core::IsSource(component.GetType())) {
        LoadSourceFields(m_Sources, static_cast<const Core::Source &>(component));
    }
    if (Core::IsBehavioralSource(component.GetType())) {
        m_ExpressionText = static_cast<const Core::BehavioralSource &>(component).GetExpression();
    }
    LoadModelFields(m_Models, component, m_Value);
    LoadLogicFields(m_Logic, component);
    LoadElectromechanicalFields(m_Electromechanical, component);
    m_LoadedSelection = m_Schematic->GetSelectionVersion();
}

/**
 * @brief   What there is to edit when no single part is selected: how many items are, or a hint.
 */
void PartEditor::DrawSelectionSummary() {
    const std::size_t count = m_Schematic->GetSelectedElements().size() + m_Schematic->GetSelectedWires().size();
    if (count > 1) {
        ImGui::TextUnformatted(std::format("{} items selected", count).c_str());
    } else if (count == 1) {
        ImGui::TextUnformatted("Wire");
    } else {
        ImGui::TextDisabled("Select a component to edit it");
    }
}

/**
 * @brief   The name of a part that has one, which a new name replaces when the field is left with Enter or a click
 *          elsewhere; a name that cannot be used says why and goes back to the one the part has.
 */
void PartEditor::DrawName(Core::Component &component) {
    if (std::string_view(component.GetNamePrefix()).empty()) {
        return;
    }
    DrawFieldLabel("Name");
    ImGui::InputText("##name", m_Name.data(), m_Name.size(), ImGuiInputTextFlags_CharsNoBlank);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::size_t index = *m_Schematic->GetSelectedElements().begin();
        const auto renamed = m_Schematic->RenameElement(index, m_Name.data());
        m_NameError = renamed ? std::string() : renamed.error();
        m_Name.fill('\0');
        component.GetName().copy(m_Name.data(), m_Name.size() - 1);
    }
    if (!m_NameError.empty()) {
        ImGui::TextColored(GetErrorTextColor(), "%s", m_NameError.c_str());
    }
}

/**
 * @brief   The fields of the kind of part: its net name, model, waveform, gain or value.
 */
void PartEditor::DrawFields(Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (type == Core::ComponentType::NET_LABEL || Core::IsSubcircuitPin(type)) {
        DrawNetLabel(static_cast<Core::NetLabel &>(component), m_Blocks, *m_Schematic);
        return;
    }
    if (type == Core::ComponentType::SUBCIRCUIT) {
        auto &instance = static_cast<Core::SubcircuitInstance &>(component);
        if (DrawSubcircuit(instance, m_Blocks, *m_Schematic)) {
            LoadFields(instance);
        }
        return;
    }
    if (DrawModeledPart(component) || DrawSettingPart(component)) {
        return;
    }
    if (Core::IsSource(type)) {
        DrawSource(static_cast<Core::Source &>(component), m_Sources, m_Value, *m_Schematic);
    } else if (Core::IsControlledSource(type)) {
        DrawControlledSource(static_cast<Core::ControlledSource &>(component), m_Value, *m_Schematic);
    } else if (Core::IsBehavioralSource(type)) {
        DrawBehavioralSource(static_cast<Core::BehavioralSource &>(component), m_ExpressionText, *m_Schematic);
    } else if (type == Core::ComponentType::TRANSFORMER) {
        DrawValue(component, m_Value, m_BlockParameters, m_Preferences, *m_Schematic);
        DrawTransformer(static_cast<Core::Transformer &>(component), m_Electromechanical, *m_Schematic);
    } else {
        DrawValue(component, m_Value, m_BlockParameters, m_Preferences, *m_Schematic);
        if (type == Core::ComponentType::POTENTIOMETER) {
            DrawWiper(static_cast<Core::Potentiometer &>(component), *m_Schematic);
        } else if (component.SupportsInitialCondition()) {
            DrawInitialCondition(component, m_Passives, *m_Schematic);
        }
    }
    if (component.SupportsTolerance()) {
        DrawTolerance(component, m_Passives, m_Preferences, *m_Schematic);
    }
}

/**
 * @brief   Diodes, transistors and op-amps, which a model sets; false for the other parts.
 */
bool PartEditor::DrawModeledPart(Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsDiode(type)) {
        DrawDiode(static_cast<Core::Diode &>(component), m_Models, *m_Schematic);
    } else if (Core::IsBJT(type)) {
        DrawBJT(static_cast<Core::BJT &>(component), m_Models, *m_Schematic);
    } else if (Core::IsMOSFET(type)) {
        DrawMOSFET(static_cast<Core::MOSFET &>(component), m_Models, *m_Schematic);
    } else if (type == Core::ComponentType::OP_AMP) {
        DrawOpAmp(static_cast<Core::OpAmp &>(component), m_Models, m_Value, *m_Schematic);
    } else {
        return false;
    }
    return true;
}

/**
 * @brief   Switches, relays and the digital parts, which have a state or parameters instead of a value; false for the
 *          others.
 */
bool PartEditor::DrawSettingPart(Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if (Core::IsSwitch(type)) {
        DrawSwitch(static_cast<Core::Switch &>(component), *m_Schematic);
    } else if (type == Core::ComponentType::V_SWITCH) {
        DrawControlledSwitch(static_cast<Core::VoltageControlledSwitch &>(component), m_Electromechanical,
                             *m_Schematic);
    } else if (type == Core::ComponentType::RELAY) {
        DrawRelay(static_cast<Core::Relay &>(component), m_Electromechanical, *m_Schematic);
    } else if (type == Core::ComponentType::FUSE) {
        DrawFuse(static_cast<Core::Fuse &>(component), m_Electromechanical, *m_Schematic);
    } else if (Core::IsLogicPart(type)) {
        DrawLogicPart(static_cast<Core::LogicPart &>(component), m_Logic, *m_Schematic);
    } else if (type == Core::ComponentType::CLOCK) {
        DrawClock(static_cast<Core::Clock &>(component), m_Logic, *m_Schematic);
    } else if (type == Core::ComponentType::LOGIC_INPUT) {
        DrawLogicInput(static_cast<Core::LogicInput &>(component), m_Logic, *m_Schematic);
    } else if (type == Core::ComponentType::LOGIC_OUTPUT) {
        DrawLogicOutput(static_cast<Core::LogicOutput &>(component), m_Logic, *m_Schematic);
    } else if (type == Core::ComponentType::SEVEN_SEGMENT) {
        DrawSevenSegment(static_cast<Core::SevenSegmentDisplay &>(component), m_Logic, *m_Schematic);
    } else {
        return false;
    }
    return true;
}

} // namespace GUI
