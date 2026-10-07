/**
 * @file    properties_window.h
 * @brief   Window that shows and edits the component selected in the schematic.
 */

#ifndef IMCSIM_PROPERTIES_WINDOW_H
#define IMCSIM_PROPERTIES_WINDOW_H

#include "components/component.h"
#include "components/voltage_source.h"
#include "schematic.h"
#include "value_field.h"
#include "windows/app_window.h"
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @class   PropertiesWindow
 * @brief   Shows the type and name of the selected component and edits its values with SPICE suffixes.
 * @details Voltage sources also switch between DC and AC, and show the sine parameters when AC.
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
    ValueField m_Amplitude;
    ValueField m_Frequency;
    ValueField m_Offset;

    void Draw() override;
    void LoadFields(const Core::Component &component);
    void DrawValue(Core::Component &component);
    void DrawVoltageSource(Core::VoltageSource &source);
};

} // namespace GUI

#endif // IMCSIM_PROPERTIES_WINDOW_H
