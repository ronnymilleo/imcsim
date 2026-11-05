/**
 * @file    fuse_flash.h
 * @brief   How strong the flash of a blowing fuse is, from the time since it blew.
 */

#ifndef IMCSIM_FUSE_FLASH_H
#define IMCSIM_FUSE_FLASH_H

namespace GUI {

/// The flash of a blown fuse lasts this long on the clock of the screen, whatever the speed of the playback.
inline constexpr double FUSE_FLASH_SECONDS = 0.15;

float GetFuseFlashLevel(double seconds_since_blow);

} // namespace GUI

#endif // IMCSIM_FUSE_FLASH_H
