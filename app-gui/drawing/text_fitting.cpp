/**
 * @file    text_fitting.cpp
 * @brief   Shortens a line of text to a width, at a word, with an ellipsis.
 */

#include "drawing/text_fitting.h"

namespace GUI {

namespace {

/// The ellipsis, encoded in UTF-8.
constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";
/// Punctuation left out before the ellipsis, which would read oddly next to it.
constexpr std::string_view TRAILING_PUNCTUATION = " ,;:.";

/**
 * @brief   Whether a byte continues a UTF-8 character, so a cut there would split it.
 */
bool IsContinuationByte(const char byte) {
    constexpr unsigned char CONTINUATION_MASK = 0xC0;
    constexpr unsigned char CONTINUATION_BITS = 0x80;
    return (static_cast<unsigned char>(byte) & CONTINUATION_MASK) == CONTINUATION_BITS;
}

/**
 * @brief   The text cut at a length, without the punctuation it would end on, followed by the ellipsis.
 */
std::string CutAt(const std::string_view text, const std::size_t length) {
    std::string_view kept = text.substr(0, length);
    const std::size_t last = kept.find_last_not_of(TRAILING_PUNCTUATION);
    kept = last == std::string_view::npos ? std::string_view() : kept.substr(0, last + 1);
    return std::string(kept) + std::string(ELLIPSIS);
}

} // namespace

/**
 * @brief   Shortens a line to fit a width, cutting after the last whole word that fits and adding an ellipsis, so a
 *          status line never ends in the middle of a word.
 * @param[in] text       Line to fit.
 * @param[in] max_width  Width available, in pixels.
 * @param[in] measure    Width of a piece of text, in the units of max_width.
 * @return  The text itself when it fits; otherwise the words that fit followed by an ellipsis, or a cut between
 *          characters when even the first word does not fit, and empty when not even the ellipsis fits.
 */
std::string ShortenToWidth(const std::string_view text, const float max_width, const TextMeasure &measure) {
    if (measure(text) <= max_width) {
        return std::string(text);
    }
    if (measure(ELLIPSIS) > max_width) {
        return {};
    }
    // Words first, from the longest cut down
    for (std::size_t space = text.rfind(' '); space != std::string_view::npos && space > 0;
         space = text.rfind(' ', space - 1)) {
        const std::string candidate = CutAt(text, space);
        if (measure(candidate) <= max_width) {
            return candidate;
        }
    }
    // A first word longer than the space is cut between characters
    for (std::size_t length = text.size() - 1; length > 0; --length) {
        if (IsContinuationByte(text[length])) {
            continue;
        }
        const std::string candidate = CutAt(text, length);
        if (measure(candidate) <= max_width) {
            return candidate;
        }
    }
    return std::string(ELLIPSIS);
}

/**
 * @brief   The first line of a message, for a place that has room for one.
 * @param[in] text  Message, possibly of several lines.
 * @return  The text up to its first line break, or all of it when it has none.
 */
std::string_view GetFirstLine(const std::string_view text) {
    return text.substr(0, text.find('\n'));
}

} // namespace GUI
