/**
 * @file    behavioral_expression.h
 * @brief   Reads the expression of a behavioral source and writes it for ngspice, with its nets and currents resolved.
 */

#ifndef IMCSIM_BEHAVIORAL_EXPRESSION_H
#define IMCSIM_BEHAVIORAL_EXPRESSION_H

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  BehavioralReference
 * @brief   A voltage or current an expression reads, and where it is written in the text.
 */
struct BehavioralReference {
    enum class Kind {
        VOLTAGE,
        CURRENT
    };

    Kind Type = Kind::VOLTAGE;
    /// One or two nets for a voltage, V(OUT) or V(IN, OUT); the name of the current, R1 or Q1.C, for a current.
    std::vector<std::string> Names;
    /// First character of the reference, the V or the I.
    std::size_t Start = 0;
    /// Characters from Start to the closing parenthesis, included.
    std::size_t Length = 0;

    bool operator==(const BehavioralReference &) const = default;
};

/// Finds the node of a net name, or no value when no label gives that name.
using NetNodeFinder = std::function<std::optional<int>(std::string_view)>;
/// Finds the voltage source whose current ngspice reads for a current, such as Vprobe-R1, or no value.
using CurrentSourceFinder = std::function<std::optional<std::string>(std::string_view)>;

// Reading
std::expected<std::vector<BehavioralReference>, std::string> ParseBehavioralExpression(std::string_view text);
std::vector<std::string> ListBehavioralCurrents(std::string_view text);

// Writing
std::expected<std::string, std::string> TranslateBehavioralExpression(std::string_view text,
                                                                      const NetNodeFinder &find_node,
                                                                      const CurrentSourceFinder &find_source);

} // namespace Core

#endif // IMCSIM_BEHAVIORAL_EXPRESSION_H
