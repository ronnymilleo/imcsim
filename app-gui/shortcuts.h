/**
 * @file    shortcuts.h
 * @brief   The modifier key of the shortcuts, as the labels of menus and tooltips name it.
 */

#ifndef IMCSIM_SHORTCUTS_H
#define IMCSIM_SHORTCUTS_H

// ImGui reads Cmd as ImGuiMod_Ctrl on macOS (io.ConfigMacOSXBehaviors), so the same shortcuts are labeled with the
// key pressed there. A macro, so labels stay string literals: IMCSIM_SHORTCUT "N" is "Ctrl+N" or "Cmd+N"
#ifdef __APPLE__
#define IMCSIM_SHORTCUT "Cmd+"
#else
#define IMCSIM_SHORTCUT "Ctrl+"
#endif

#endif // IMCSIM_SHORTCUTS_H
