/**
 * @file    net_label.cpp
 * @brief   A net label in the simulation model: a name that joins every point labeled with it into one node.
 */

#include "net_label.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Core {

namespace {

constexpr std::size_t MAX_NET_LENGTH = 32;

bool IsNetCharacter(const char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '_' || character == '-' || character == '.';
}

} // namespace

/**
 * @brief   Creates a label with no name, which joins nothing until it is named.
 */
NetLabel::NetLabel() : Component(ComponentType::NET_LABEL, 0.0) {
}

/**
 * @brief   Returns the name of the net, as the user typed it.
 * @return  The name, empty while the label has none.
 */
const std::string &NetLabel::GetNet() const {
    return m_Net;
}

/**
 * @brief   Sets the name of the net.
 * @param[in] net  New name; check it with IsValidNet() first. An empty name leaves the label unnamed.
 */
void NetLabel::SetNet(std::string net) {
    m_Net = std::move(net);
}

/**
 * @brief   Tells whether a text can name a net.
 * @param[in] net  Candidate name.
 * @return  True for 1 to 32 letters, digits, underscores, hyphens and dots, so the name is safe as a SPICE node.
 */
bool NetLabel::IsValidNet(const std::string_view net) {
    return !net.empty() && net.size() <= MAX_NET_LENGTH && std::ranges::all_of(net, IsNetCharacter);
}

/**
 * @brief   Turns a net name into the form nets are compared in.
 * @param[in] net  A name as typed.
 * @return  The name in lower case, so "Vout" and "VOUT" are the same net.
 */
std::string NetLabel::NormalizeNet(const std::string_view net) {
    std::string normalized(net);
    std::ranges::transform(normalized, normalized.begin(),
                           [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return normalized;
}

} // namespace Core
