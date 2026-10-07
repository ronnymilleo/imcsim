/**
 * @file    properties_window.h
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#ifndef IMCSIM_PROPERTIES_WINDOW_H
#define IMCSIM_PROPERTIES_WINDOW_H

#include "components/bjt.h"
#include "components/component.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/source.h"
#include "schematic.h"
#include "value_field.h"
#include "windows/app_window.h"
#include <array>
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   PropertiesWindow
 * @brief   Shows the type and name of the selected component and edits its values with SPICE suffixes.
 * @details Voltage and current sources also switch between DC, AC and pulse, and show the parameters of the
 *          chosen type.
 */
class PropertiesWindow : public AppWindow {
public:
    explicit PropertiesWindow(Schematic &schematic);
    ~PropertiesWindow() override = default;

private:
    Schematic &m_Schematic;
    // Texts being edited, and the selection they were loaded from
    std::optional<std::size_t> m_LoadedSelection;
    ValueField m_Value;
    // One per member of Core::ACParameters and Core::PulseParameters, in the order the window lists them
    std::array<ValueField, 3> m_ACFields;
    std::array<ValueField, 7> m_PulseFields;
    // One per member of the parameters of custom diodes and transistors
    std::array<ValueField, 9> m_DiodeFields;
    std::array<ValueField, 13> m_BJTFields;
    std::array<ValueField, 9> m_MOSFETFields;

    void Draw() override;
    void LoadFields(const Core::Component &component);
    void DrawValue(Core::Component &component);
    void DrawDiode(Core::Diode &diode);
    void DrawBJT(Core::BJT &bjt);
    void DrawMOSFET(Core::MOSFET &mosfet);
    void DrawSource(Core::Source &source);
    void DrawSine(Core::Source &source);
    void DrawPulse(Core::Source &source);
};

} // namespace GUI

#endif // IMCSIM_PROPERTIES_WINDOW_H
