/**
 * @file    spice_block_check.cpp
 * @brief   Remembers what the engine says about a block kept as SPICE text, so the editor does not expand it again.
 */

#include "backends/nodalys/spice_block_check.h"

#include <utility>

namespace Core {

/**
 * @brief   Makes a cache that asks the given check on a miss.
 * @param[in] checker  The check to remember the answers of.
 */
SpiceBlockCheck::SpiceBlockCheck(Checker checker) : m_Checker(std::move(checker)) {
}

/**
 * @brief   Tells what the check says about a text used with some parameters, running it only the first time.
 * @param[in] text        The block as a library file gives it.
 * @param[in] parameters  The value of each parameter, defaults and overrides settled.
 * @return  The reason the engine cannot simulate the block, or none when it can.
 * @note    Safe to call from any thread.
 */
std::optional<std::string> SpiceBlockCheck::Find(const Nodalys::SubcircuitText &text,
                                                 const std::map<std::string, double> &parameters) {
    Key key{.Name = text.Name, .Ports = text.Ports, .Body = text.Body, .Parameters = parameters};
    {
        const std::lock_guard lock(m_Mutex);
        const auto found = m_Answers.find(key);
        if (found != m_Answers.end()) {
            return found->second;
        }
    }

    std::optional<std::string> answer = m_Checker(text, parameters);

    const std::lock_guard lock(m_Mutex);
    if (m_Answers.size() >= MAX_SPICE_BLOCK_CHECK_ENTRIES) {
        m_Answers.clear();
    }
    m_Answers.insert_or_assign(std::move(key), answer);
    return answer;
}

/**
 * @brief   Counts the answers kept.
 * @return  The number of entries, at most `MAX_SPICE_BLOCK_CHECK_ENTRIES`.
 */
std::size_t SpiceBlockCheck::GetEntryCount() const {
    const std::lock_guard lock(m_Mutex);
    return m_Answers.size();
}

/**
 * @brief   Gives the cache of the check that Nodalys runs.
 * @return  The one cache of the process, which keeps its answers between calls.
 */
SpiceBlockCheck &GetNodalysSpiceBlockCheck() {
    static SpiceBlockCheck check(Nodalys::CheckSubcircuitText);
    return check;
}

} // namespace Core
