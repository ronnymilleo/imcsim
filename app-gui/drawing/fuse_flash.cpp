/**
 * @file    fuse_flash.cpp
 * @brief   How strong the flash of a blowing fuse is, from the time since it blew.
 */

#include "drawing/fuse_flash.h"

#include <algorithm>

namespace GUI {

/**
 * @brief   Works out the strength of the flash that marks the instant a fuse blows.
 * @param[in] seconds_since_blow  Time on the screen clock since the fuse was first seen open.
 * @return  1 at the instant it blew, falling in a straight line to 0 after FUSE_FLASH_SECONDS; 0 before it blew too.
 */
float GetFuseFlashLevel(const double seconds_since_blow) {
    if (seconds_since_blow < 0.0) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(1.0 - seconds_since_blow / FUSE_FLASH_SECONDS, 0.0, 1.0));
}

} // namespace GUI
