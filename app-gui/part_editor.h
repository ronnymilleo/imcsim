/**
 * @file    part_editor.h
 * @brief   Fields that show and edit the component selected in the schematic.
 */

#ifndef IMCSIM_PART_EDITOR_H
#define IMCSIM_PART_EDITOR_H

#include "components/bjt.h"
#include "components/component.h"
#include "components/controlled_source.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/op_amp.h"
#include "components/source.h"
#include "schematic.h"
#include "value_field.h"
#include <array>
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   PartEditor
 * @brief   Shows the type and name of the selected component and edits its values with SPICE suffixes.
 * @details Voltage and current sources also switch between DC, AC and pulse, and show the parameters of the
 *          chosen type. The Application owns one instance, which the Properties window and the editor popover both
 *          draw, so the texts being typed are the same in both.
 */
class PartEditor {
public:
    explicit PartEditor(Schematic &schematic);

    void Draw();

    // Typing a value straight onto the selected part
    bool CanTypeValue() const;
    void StartTypingValue(char first);

private:
    Schematic &m_Schematic;
    // Texts being edited, and the selection they were loaded from
    std::optional<std::size_t> m_LoadedSelection;
    ValueField m_Value;
    // One per member of Core::ACParameters and Core::PulseParameters, in the order the window lists them
    std::array<ValueField, 4> m_ACFields;
    std::array<ValueField, 7> m_PulseFields;
    // One per member of the parameters of custom diodes and transistors
    std::array<ValueField, 9> m_DiodeFields;
    std::array<ValueField, 16> m_BJTFields;
    std::array<ValueField, 13> m_MOSFETFields;
    std::array<ValueField, 11> m_OpAmpFields;

    void LoadFields(const Core::Component &component);
    void DrawValue(Core::Component &component);
    void DrawDiode(Core::Diode &diode);
    void DrawBJT(Core::BJT &bjt);
    void DrawMOSFET(Core::MOSFET &mosfet);
    void DrawOpAmp(Core::OpAmp &op_amp);
    void DrawSource(Core::Source &source);
    void DrawSine(Core::Source &source);
    void DrawPulse(Core::Source &source);
    void DrawControlledSource(Core::ControlledSource &source);
    void DrawControllingCurrent(Core::ControlledSource &source);
};

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_H
