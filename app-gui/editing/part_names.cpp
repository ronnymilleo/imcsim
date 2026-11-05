/**
 * @file    part_names.cpp
 * @brief   Renames a part: whether a new name is allowed, and the names of currents that follow it.
 */

#include "editing/part_names.h"

#include "components/component.h"
#include <algorithm>
#include <cctype>
#include <format>

namespace GUI {

namespace {

/// Where a current of a part with several terminals names the terminal, as in Q1.C.
constexpr char TERMINAL_SEPARATOR = '.';

bool AreSameIgnoringCase(const std::string_view first, const std::string_view second) {
    return std::ranges::equal(first, second, [](const unsigned char left, const unsigned char right) {
        return std::tolower(left) == std::tolower(right);
    });
}

} // namespace

/**
 * @brief   Tells whether a part may take a name.
 * @param[in] elements  Every part of the schematic.
 * @param[in] index     The part being renamed.
 * @param[in] name      The name it would take.
 * @return  Why it may not, in words for the user: the name must keep the SPICE letter of the part, which tells
 *          ngspice what the part is, then go on with letters, digits or _, and no other part may have it, case aside,
 *          since SPICE ignores case; none when it may.
 */
std::optional<std::string> CheckPartName(const std::vector<std::unique_ptr<UIElement>> &elements,
                                         const std::size_t index, const std::string_view name) {
    const std::string_view prefix = elements[index]->GetComponent().GetNamePrefix();
    if (prefix.empty()) {
        return "This part has no name";
    }
    if (!Core::IsValidName(name, prefix)) {
        return std::format("Start with {}, then letters, digits or _", prefix);
    }
    for (std::size_t other = 0; other < elements.size(); ++other) {
        if (other != index && AreSameIgnoringCase(elements[other]->GetComponent().GetName(), name)) {
            return std::format("{} already has this name", elements[other]->GetComponent().GetName());
        }
    }
    return std::nullopt;
}

/**
 * @brief   The name of a current after its part is renamed.
 * @param[in] current   A current name as the results give it, such as "R1" or "Q1.C".
 * @param[in] old_name  The name the part had.
 * @param[in] new_name  The name it has now.
 * @return  The current with the part's new name, its terminal kept; the current as it was when it is not the part's.
 */
std::string RenameCurrent(const std::string_view current, const std::string_view old_name,
                          const std::string_view new_name) {
    if (current == old_name) {
        return std::string(new_name);
    }
    if (current.starts_with(old_name) && current.size() > old_name.size() &&
        current[old_name.size()] == TERMINAL_SEPARATOR) {
        return std::format("{}{}", new_name, current.substr(old_name.size()));
    }
    return std::string(current);
}

/**
 * @brief   An expression of a math channel after a part is renamed.
 * @param[in] expression  Such as "I(R1)*V(OUT)" or "I(Q1.C)".
 * @param[in] old_name    The name the part had.
 * @param[in] new_name    The name it has now.
 * @return  The expression with each current of the part, I(name) or I(name.terminal), under the new name.
 */
std::string RenameCurrentsIn(const std::string_view expression, const std::string_view old_name,
                             const std::string_view new_name) {
    std::string renamed;
    std::size_t position = 0;
    while (position < expression.size()) {
        const std::size_t open = expression.find("I(", position);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t start = open + 2;
        // Only the I of a current, not the last letter of a longer name
        if (open > 0 &&
            (std::isalnum(static_cast<unsigned char>(expression[open - 1])) != 0 || expression[open - 1] == '_')) {
            renamed += expression.substr(position, start - position);
            position = start;
            continue;
        }
        const std::size_t close = expression.find(')', start);
        if (close == std::string_view::npos) {
            break;
        }
        renamed += expression.substr(position, start - position);
        renamed += RenameCurrent(expression.substr(start, close - start), old_name, new_name);
        position = close;
    }
    renamed += expression.substr(position);
    return renamed;
}

} // namespace GUI
