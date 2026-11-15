/**
 * @file    part_editor.h
 * @brief   Fields that show and edit the component selected in the schematic.
 */

#ifndef IMCSIM_PART_EDITOR_H
#define IMCSIM_PART_EDITOR_H

#include "base/value_field.h"
#include "circuit/components/component.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"
#include "windows/part_editor_blocks.h"
#include "windows/part_editor_electromechanical.h"
#include "windows/part_editor_logic.h"
#include "windows/part_editor_models.h"
#include "windows/part_editor_passives.h"
#include "windows/part_editor_sources.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   PartEditor
 * @brief   Shows the type and name of the selected component and edits its values with SPICE suffixes.
 * @details Voltage and current sources also switch between their waveforms and show the parameters of the chosen
 *          one, edited as fields except the piecewise linear points, which are one text of "time value" pairs. The
 *          Application owns one instance, which the Properties window and the editor popover both draw, so the
 *          texts being typed are the same in both. Subcircuit instances expose one field per definition parameter,
 *          showing an override or its default. SetSchematic follows the editor into and out of a block definition,
 * where SetBlockParameters offers the block's parameters to the values of its resistors, capacitors and inductors.
 */
class PartEditor {
public:
    PartEditor(Schematic &schematic, const Preferences &preferences);

    void Draw();

    // The schematic edited: the document, or the contents of a block and the parameters its parts can follow
    void SetSchematic(Schematic &schematic);
    void SetBlockParameters(std::vector<std::string> names);

    // Typing a value straight onto the selected part
    bool CanTypeValue() const;
    void StartTypingValue(char first);

    // The name of a selected net label gets the focus on the next Draw(), as for a label just placed
    void FocusNetName();

private:
    Schematic *m_Schematic;
    /// The standard series that resistors, capacitors and inductors take their values from.
    const Preferences &m_Preferences;
    // Texts being edited
    /// Selection the texts were loaded from.
    std::optional<std::size_t> m_LoadedSelection;
    /// The main value of the part, which the sources, the models and the controlled sources use too.
    ValueField m_Value;
    /// The name being typed for the part, applied when the field is left.
    std::array<char, 40> m_Name{};
    /// Why the name typed last cannot be used, or empty.
    std::string m_NameError;
    PassiveFields m_Passives;
    SourceFields m_Sources;
    ModelFields m_Models;
    ElectromechanicalFields m_Electromechanical;
    LogicFields m_Logic;
    BlockFields m_Blocks;
    /// The expression of a behavioral source being typed, kept while it does not parse.
    std::string m_ExpressionText;
    /// Parameters of the block being edited, which a resistor, capacitor or inductor can take its value from.
    std::vector<std::string> m_BlockParameters;

    void LoadFields(const Core::Component &component);
    void DrawSelectionSummary();
    void DrawName(Core::Component &component);
    void DrawFields(Core::Component &component);
    bool DrawModeledPart(Core::Component &component);
    bool DrawSettingPart(Core::Component &component);
};

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_H
