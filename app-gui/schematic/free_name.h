/**
 * @file    free_name.h
 * @brief   Finds a name nothing has yet, from a base name and numbered suffixes such as "_2".
 */

#ifndef IMCSIM_FREE_NAME_H
#define IMCSIM_FREE_NAME_H

#include <functional>
#include <string>
#include <string_view>

namespace GUI {

std::string FindFreeName(std::string_view base, const std::function<bool(std::string_view)> &is_taken);

} // namespace GUI

#endif // IMCSIM_FREE_NAME_H
