/**
 * @file    spice_block_check.h
 * @brief   Remembers what the engine says about a block kept as SPICE text, so the editor does not expand it again.
 */

#ifndef IMCSIM_SPICE_BLOCK_CHECK_H
#define IMCSIM_SPICE_BLOCK_CHECK_H

#include <nodalys/subcircuit_text.h>

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/// Answers kept before the cache starts again; far more than the blocks of a schematic, so it only bounds the edits
/// that change a text or a parameter one keystroke at a time.
constexpr std::size_t MAX_SPICE_BLOCK_CHECK_ENTRIES = 256;

/**
 * @class   SpiceBlockCheck
 * @brief   A cache of the pre-run check of a block kept as SPICE text.
 * @details The check expands the text with its parameters, so the answer depends on the name, the ports and the body
 *          of the text and on the value of each parameter, nested definitions being part of the body; those four are
 *          the key. Both the editor thread and the worker ask, so a mutex guards the entries. The check runs outside
 *          the lock, so two threads asking a new key at once may both run it, which gives the same answer twice.
 */
class SpiceBlockCheck {
public:
    /// Callback checking a SPICE subcircuit block with resolved parameters, returning a failure message or
    /// `std::nullopt`.
    using Checker = std::function<std::optional<std::string>(const Nodalys::SubcircuitText &text,
                                                             const std::map<std::string, double> &parameters)>;

    explicit SpiceBlockCheck(Checker checker);

    // Asking
    [[nodiscard]] std::optional<std::string> Find(const Nodalys::SubcircuitText &text,
                                                  const std::map<std::string, double> &parameters);
    std::size_t GetEntryCount() const;

private:
    struct Key {
        std::string Name;
        std::vector<std::string> Ports;
        std::string Body;
        std::map<std::string, double> Parameters;

        auto operator<=>(const Key &) const = default;
    };

    Checker m_Checker;
    mutable std::mutex m_Mutex;
    std::map<Key, std::optional<std::string>> m_Answers;
};

// Engine check
SpiceBlockCheck &GetNodalysSpiceBlockCheck();

} // namespace Core

#endif // IMCSIM_SPICE_BLOCK_CHECK_H
