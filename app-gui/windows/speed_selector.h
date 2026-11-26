/**
 * @file    speed_selector.h
 * @brief   Compact speed multiplier selector shared by transient playback and live simulation controls.
 */

#ifndef IMCSIM_SPEED_SELECTOR_H
#define IMCSIM_SPEED_SELECTOR_H

namespace GUI {

// Speed selector
float GetSpeedSelectorWidth();
bool DrawSpeedSelector(const char *id, double &speed, const char *tooltip);

} // namespace GUI

#endif // IMCSIM_SPEED_SELECTOR_H
