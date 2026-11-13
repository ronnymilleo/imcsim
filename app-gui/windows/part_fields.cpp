/**
 * @file    part_fields.cpp
 * @brief   Fields shared by the property editors of the parts: one value per member of a parameter set.
 */

#include "windows/part_fields.h"

namespace GUI {

/**
 * @brief   The validity check of a value that has no limit.
 */
bool AnyValue(double /*value*/) {
    return true;
}

} // namespace GUI
