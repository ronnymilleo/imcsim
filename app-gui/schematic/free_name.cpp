/**
 * @file    free_name.cpp
 * @brief   Finds a name nothing has yet, from a base name and numbered suffixes such as "_2".
 */

#include "schematic/free_name.h"

#include <format>

namespace GUI {

namespace {

/// The first suffix tried once the base name is taken, so the second of a name reads "_2".
constexpr int FIRST_SUFFIX = 2;

} // namespace

/**
 * @brief   Finds the first free name among the base name and the base name with a suffix, such as "block", "block_2",
 *          "block_3".
 * @param[in] base      The name wanted.
 * @param[in] is_taken  Tells whether a name is in use already.
 * @return  The base name when it is free; otherwise the base name with the lowest suffix that is free.
 */
std::string FindFreeName(const std::string_view base, const std::function<bool(std::string_view)> &is_taken) {
    std::string candidate(base);
    for (int suffix = FIRST_SUFFIX; is_taken(candidate); ++suffix) {
        candidate = std::format("{}_{}", base, suffix);
    }
    return candidate;
}

} // namespace GUI
