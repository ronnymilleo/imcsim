/**
 * @file    preferences_window.h
 * @brief   Window where the user sets their own choices, and the View menu shortcuts to the most used of them.
 */

#ifndef IMCSIM_PREFERENCES_WINDOW_H
#define IMCSIM_PREFERENCES_WINDOW_H

#include "preferences.h"
#include "windows/app_window.h"

namespace GUI {

/**
 * @class   PreferencesWindow
 * @brief   Gathers every preference by topic: appearance, the schematic while showing results, parts and results.
 * @details A change applies at once; the Application saves the preferences when they change. Wire colors, live effects
 *          and the theme, which change while looking at results, also have shortcuts in the View menu. The window
 *          starts closed and floats; Edit > Preferences opens it.
 */
class PreferencesWindow : public AppWindow {
public:
    explicit PreferencesWindow(Preferences &preferences);
    ~PreferencesWindow() override = default;

    void DrawViewMenuItems();

private:
    Preferences &m_Preferences;

    void Draw() override;
    void DrawAppearance();
    void DrawSchematic();
    void DrawParts();
    void DrawResults();
};

} // namespace GUI

#endif // IMCSIM_PREFERENCES_WINDOW_H
