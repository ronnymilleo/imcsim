/**
 * @file    text_fitting.h
 * @brief   Shortens a line of text to a width, at a word, with an ellipsis.
 */

#ifndef IMCSIM_TEXT_FITTING_H
#define IMCSIM_TEXT_FITTING_H

#include <functional>
#include <string>
#include <string_view>

namespace GUI {

/// Width of a piece of text on screen, in pixels.
using TextMeasure = std::function<float(std::string_view)>;

std::string ShortenToWidth(std::string_view text, float max_width, const TextMeasure &measure);
std::string_view GetFirstLine(std::string_view text);

} // namespace GUI

#endif // IMCSIM_TEXT_FITTING_H
