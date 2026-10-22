/**
 * @file    part_editor.h
 * @brief   Fields that show and edit the component selected in the schematic.
 */

#ifndef IMCSIM_PART_EDITOR_H
#define IMCSIM_PART_EDITOR_H

#include "components/bjt.h"
#include "components/clock.h"
#include "components/component.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/logic_input.h"
#include "components/logic_part.h"
#include "components/mosfet.h"
#include "components/net_label.h"
#include "components/op_amp.h"
#include "components/potentiometer.h"
#include "components/relay.h"
#include "components/source.h"
#include "components/switch.h"
#include "components/transformer.h"
#include "components/voltage_controlled_switch.h"
#include "schematic.h"
#include "standard_values.h"
#include "value_field.h"
#include <array>
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   PartEditor
 * @brief   Shows the type and name of the selected component and edits its values with SPICE suffixes.
 * @details Voltage and current sources also switch between their waveforms and show the parameters of the chosen
 *          one, edited as fields except the piecewise linear points, which are one text of "time value" pairs. The
 *          Application owns one instance, which the Properties window and the editor popover both draw, so the
 *          texts being typed are the same in both.
 */
class PartEditor {
public:
    explicit PartEditor(Schematic &schematic);

    void Draw();

    // Typing a value straight onto the selected part
    bool CanTypeValue() const;
    void StartTypingValue(char first);

    // The name of a selected net label gets the focus on the next Draw(), as for a label just placed
    void FocusNetName();

    // Standard values for resistors, capacitors and inductors, or any value
    Core::ValueSeries GetValueSeries() const;
    void SetValueSeries(Core::ValueSeries series);

private:
    Schematic &m_Schematic;
    Core::ValueSeries m_ValueSeries = Core::ValueSeries::Free;
    // Texts being edited, and the selection they were loaded from
    std::optional<std::size_t> m_LoadedSelection;
    ValueField m_Value;
    // Voltage a capacitor or current an inductor starts a transient from; empty when none is set
    ValueField m_InitialCondition;
    // The net name being typed for a net label, and whether the next Draw() focuses it
    std::array<char, 40> m_Net{};
    bool m_FocusNet = false;
    // One per member of Core::ACParameters, Core::PulseParameters, Core::EXPParameters, Core::SFFMParameters and
    // Core::AMParameters, in the order the window lists them
    std::array<ValueField, 4> m_ACFields;
    std::array<ValueField, 7> m_PulseFields;
    std::array<ValueField, 6> m_EXPFields;
    std::array<ValueField, 5> m_SFFMFields;
    std::array<ValueField, 5> m_AMFields;
    // The "time value" points of a PWL source being typed, as long as the user wants them
    std::string m_PWLText;
    // One per member of the parameters of custom diodes and transistors
    std::array<ValueField, 9> m_DiodeFields;
    std::array<ValueField, 16> m_BJTFields;
    std::array<ValueField, 13> m_MOSFETFields;
    std::array<ValueField, 11> m_OpAmpFields;
    // One per member of Core::SwitchParameters and Core::RelayParameters
    std::array<ValueField, 4> m_SwitchFields;
    std::array<ValueField, 5> m_RelayFields;
    // One per member of Core::TransformerParameters
    std::array<ValueField, 3> m_TransformerFields;
    // One per member of Core::LogicParameters
    std::array<ValueField, 2> m_LogicFields;
    // One per member of Core::ClockParameters and Core::LogicInputParameters
    std::array<ValueField, 4> m_ClockFields;
    std::array<ValueField, 1> m_LogicInputFields;

    void LoadFields(const Core::Component &component);
    void DrawSelectionSummary();
    void DrawFields(Core::Component &component);
    bool DrawModeledPart(Core::Component &component);
    void DrawValue(Core::Component &component);
    void DrawInitialCondition(Core::Component &component);
    void DrawWiper(Core::Potentiometer &pot);
    void DrawSwitch(Core::Switch &contact);
    void DrawControlledSwitch(Core::VoltageControlledSwitch &controlled);
    void DrawRelay(Core::Relay &relay);
    void DrawTransformer(Core::Transformer &transformer);
    void DrawLogicPart(Core::LogicPart &part);
    void DrawClock(Core::Clock &clock);
    void DrawLogicInput(Core::LogicInput &input);
    void DrawNetLabel(Core::NetLabel &label);
    void DrawDiode(Core::Diode &diode);
    void DrawBJT(Core::BJT &bjt);
    void DrawMOSFET(Core::MOSFET &mosfet);
    void DrawOpAmp(Core::OpAmp &op_amp);
    void DrawSource(Core::Source &source);
    void DrawSine(Core::Source &source);
    void DrawPulse(Core::Source &source);
    void DrawEXP(Core::Source &source);
    void DrawSFFM(Core::Source &source);
    void DrawAM(Core::Source &source);
    void DrawPWL(Core::Source &source);
    void DrawControlledSource(Core::ControlledSource &source);
    void DrawControllingCurrent(Core::ControlledSource &source);
};

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_H
